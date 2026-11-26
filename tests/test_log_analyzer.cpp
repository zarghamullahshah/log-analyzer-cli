// Tests for log-analyzer-cli. No framework — plain asserts with a small
// CHECK macro so the suite stays dependency-free. Run with `make test`.

#include <cassert>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "../src/log_analyzer.h"

using namespace loganalyzer;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& name) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

// --- Minimal JSON value + parser ------------------------------------------
// Just enough to verify summaryToJson output really parses as JSON and
// round-trips the numbers and strings. No external dependency.

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string str;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    const JsonValue* find(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& [k, v] : object) {
            if (k == key) return &v;
        }
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    bool parse(JsonValue& out) {
        skipWs();
        if (!parseValue(out)) return false;
        skipWs();
        return pos_ == text_.size();
    }

private:
    const std::string& text_;
    std::size_t pos_ = 0;

    void skipWs() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool consume(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool consumeLiteral(const char* literal) {
        const std::size_t len = std::string(literal).size();
        if (text_.compare(pos_, len, literal) == 0) {
            pos_ += len;
            return true;
        }
        return false;
    }

    static void appendUtf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool parseHex4(unsigned& code) {
        if (pos_ + 4 > text_.size()) return false;
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') code |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool parseString(std::string& out) {
        if (!consume('"')) return false;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= text_.size()) return false;
                const char esc = text_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        unsigned code = 0;
                        if (!parseHex4(code)) return false;
                        appendUtf8(out, code);
                        break;
                    }
                    default: return false;
                }
            } else if (static_cast<unsigned char>(c) < 0x20) {
                return false;  // unescaped control character
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool parseValue(JsonValue& out) {
        skipWs();
        if (pos_ >= text_.size()) return false;
        const char c = text_[pos_];
        if (c == '{') {
            ++pos_;
            out.type = JsonValue::Type::Object;
            skipWs();
            if (consume('}')) return true;
            while (true) {
                skipWs();
                std::string key;
                if (!parseString(key)) return false;
                skipWs();
                if (!consume(':')) return false;
                JsonValue value;
                if (!parseValue(value)) return false;
                out.object.emplace_back(std::move(key), std::move(value));
                skipWs();
                if (consume('}')) return true;
                if (!consume(',')) return false;
            }
        }
        if (c == '[') {
            ++pos_;
            out.type = JsonValue::Type::Array;
            skipWs();
            if (consume(']')) return true;
            while (true) {
                JsonValue value;
                if (!parseValue(value)) return false;
                out.array.push_back(std::move(value));
                skipWs();
                if (consume(']')) return true;
                if (!consume(',')) return false;
            }
        }
        if (c == '"') {
            out.type = JsonValue::Type::String;
            return parseString(out.str);
        }
        if (c == 't') {
            if (!consumeLiteral("true")) return false;
            out.type = JsonValue::Type::Bool;
            out.boolean = true;
            return true;
        }
        if (c == 'f') {
            if (!consumeLiteral("false")) return false;
            out.type = JsonValue::Type::Bool;
            out.boolean = false;
            return true;
        }
        if (c == 'n') {
            if (!consumeLiteral("null")) return false;
            out.type = JsonValue::Type::Null;
            return true;
        }
        // Number.
        const std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
        bool digits = false;
        while (pos_ < text_.size() &&
               (std::isdigit(static_cast<unsigned char>(text_[pos_])) ||
                text_[pos_] == '.' || text_[pos_] == 'e' ||
                text_[pos_] == 'E' || text_[pos_] == '+' ||
                text_[pos_] == '-')) {
            if (std::isdigit(static_cast<unsigned char>(text_[pos_]))) digits = true;
            ++pos_;
        }
        if (!digits || pos_ == start) return false;
        try {
            out.number = std::stod(text_.substr(start, pos_ - start));
        } catch (...) {
            return false;
        }
        out.type = JsonValue::Type::Number;
        return true;
    }
};

bool parseJson(const std::string& text, JsonValue& out) {
    return JsonParser(text).parse(out);
}

// Number field of a parsed JSON object, or -1 when missing/not a number.
double jsonNumber(const JsonValue& obj, const std::string& key) {
    const JsonValue* value = obj.find(key);
    if (value == nullptr || value->type != JsonValue::Type::Number) return -1;
    return value->number;
}

std::size_t levelCount(const Summary& summary, Level level) {
    const auto it = summary.byLevel.find(level);
    return it == summary.byLevel.end() ? 0 : it->second;
}

}  // namespace

int main() {
    // --- parseLevel -------------------------------------------------------
    check(parseLevel("ERROR") == Level::Error, "parseLevel ERROR");
    check(parseLevel("error") == Level::Error, "parseLevel lowercase error");
    check(parseLevel("Warning") == Level::Warn, "parseLevel WARNING alias");
    check(parseLevel("WARN") == Level::Warn, "parseLevel WARN");
    check(parseLevel("TRACE") == Level::Trace, "parseLevel TRACE");
    check(parseLevel("FATAL") == Level::Fatal, "parseLevel FATAL");
    check(parseLevel("INFO") == Level::Info, "parseLevel INFO");
    check(parseLevel("DEBUG") == Level::Debug, "parseLevel DEBUG");
    check(!parseLevel("CRITICAL").has_value(), "parseLevel unknown rejected");
    check(!parseLevel("").has_value(), "parseLevel empty rejected");
    check(levelName(Level::Error) == "ERROR", "levelName ERROR");
    check(levelName(Level::Warn) == "WARN", "levelName WARN");

    // --- parseLine --------------------------------------------------------
    const auto full = parseLine("2026-11-24 10:15:03 [ERROR] Database connection failed");
    check(full.has_value(), "parseLine full line parses");
    check(full->timestamp == "2026-11-24 10:15:03", "parseLine keeps timestamp");
    check(full->level == Level::Error, "parseLine keeps level");
    check(full->message == "Database connection failed", "parseLine keeps message");

    const auto bare = parseLine("[warn] Disk usage above 90%");
    check(bare.has_value(), "parseLine bare line parses");
    check(bare->timestamp.empty(), "parseLine bare line has no timestamp");
    check(bare->level == Level::Warn, "parseLine lowercase level accepted");

    check(!parseLine("").has_value(), "parseLine empty line rejected");
    check(!parseLine("no level here at all").has_value(), "parseLine no level rejected");
    check(!parseLine("[INFO]").has_value(), "parseLine empty message rejected");
    check(!parseLine("[NOPE] Something").has_value(), "parseLine bad level rejected");
    check(!parseLine("2026-11-24 [INFO] Missing time part").has_value(),
          "parseLine short timestamp rejected");
    check(!parseLine("[INFO Something").has_value(), "parseLine unclosed bracket rejected");
    check(parseLine("  [INFO]   Padded message  ")->message == "Padded message",
          "parseLine trims message");

    // --- analyze ----------------------------------------------------------
    const std::vector<std::string> lines = {
        "2026-11-24 10:00:00 [INFO] Server started",
        "2026-11-24 10:01:00 [ERROR] Database connection failed",
        "2026-11-24 10:02:00 [ERROR] Database connection failed",
        "[WARN] Disk usage above 90%",
        "this line is garbage",
        "",
        "2026-11-24 10:03:00 [DEBUG] Cache miss for key user:42",
    };
    const Summary s = analyze(lines, 10);
    check(s.totalLines == 7, "analyze total lines");
    check(s.parsedLines == 5, "analyze parsed lines");
    check(s.malformedLines == 2, "analyze malformed lines");
    check(s.byLevel.at(Level::Info) == 1, "analyze INFO count");
    check(s.byLevel.at(Level::Error) == 2, "analyze ERROR count");
    check(s.byLevel.at(Level::Warn) == 1, "analyze WARN count");
    check(s.byLevel.at(Level::Debug) == 1, "analyze DEBUG count");
    check(s.topMessages.size() == 4, "analyze distinct messages");
    check(s.topMessages[0].first == "Database connection failed" &&
              s.topMessages[0].second == 2,
          "analyze top message first");

    const Summary capped = analyze(lines, 2);
    check(capped.topMessages.size() == 2, "analyze topN caps results");

    const Summary empty = analyze({}, 10);
    check(empty.totalLines == 0 && empty.parsedLines == 0 &&
              empty.malformedLines == 0 && empty.topMessages.empty(),
          "analyze empty input");

    // --- filterByLevel ----------------------------------------------------
    std::vector<LogEntry> entries;
    for (const std::string& line : lines) {
        const auto entry = parseLine(line);
        if (entry.has_value()) entries.push_back(*entry);
    }
    const std::vector<LogEntry> errors = filterByLevel(entries, Level::Error);
    check(errors.size() == 2, "filterByLevel ERROR keeps 2");
    check(errors[0].level == Level::Error, "filterByLevel keeps level");
    check(filterByLevel(entries, Level::Trace).size() == 5,
          "filterByLevel TRACE keeps all");
    check(filterByLevel(entries, Level::Fatal).empty(),
          "filterByLevel FATAL keeps none");

    // --- summaryToJson ------------------------------------------------------
    // Same numbers as the normal output, on the real sample log.
    std::vector<std::string> sampleLines;
    {
        std::ifstream in("tests/sample.log");
        check(in.good(), "json test can open tests/sample.log");
        std::string line;
        while (std::getline(in, line)) sampleLines.push_back(line);
    }
    const Summary sampleSummary = analyze(sampleLines, 10);
    const std::string json = summaryToJson(sampleSummary);
    JsonValue root;
    check(parseJson(json, root), "json output parses as JSON");
    check(root.type == JsonValue::Type::Object, "json root is an object");
    check(jsonNumber(root, "total_lines") == static_cast<double>(sampleSummary.totalLines),
          "json total_lines matches normal output");
    check(jsonNumber(root, "parsed_lines") == static_cast<double>(sampleSummary.parsedLines),
          "json parsed_lines matches normal output");
    check(jsonNumber(root, "malformed_lines") == static_cast<double>(sampleSummary.malformedLines),
          "json malformed_lines matches normal output");
    const JsonValue* byLevel = root.find("by_level");
    check(byLevel != nullptr && byLevel->type == JsonValue::Type::Object,
          "json by_level is an object");
    if (byLevel != nullptr) {
        for (Level level : {Level::Trace, Level::Debug, Level::Info,
                            Level::Warn, Level::Error, Level::Fatal}) {
            check(jsonNumber(*byLevel, levelName(level)) ==
                      static_cast<double>(levelCount(sampleSummary, level)),
                  "json by_level " + levelName(level) + " matches normal output");
        }
    }
    const JsonValue* topMessages = root.find("top_messages");
    check(topMessages != nullptr && topMessages->type == JsonValue::Type::Array,
          "json top_messages is an array");
    if (topMessages != nullptr) {
        check(topMessages->array.size() == sampleSummary.topMessages.size(),
              "json top_messages size matches normal output");
        bool messagesMatch = topMessages->array.size() == sampleSummary.topMessages.size();
        for (std::size_t i = 0; messagesMatch && i < sampleSummary.topMessages.size(); ++i) {
            const JsonValue& entry = topMessages->array[i];
            const JsonValue* message = entry.find("message");
            const JsonValue* count = entry.find("count");
            messagesMatch = message != nullptr &&
                            message->type == JsonValue::Type::String &&
                            message->str == sampleSummary.topMessages[i].first &&
                            count != nullptr &&
                            count->type == JsonValue::Type::Number &&
                            count->number ==
                                static_cast<double>(sampleSummary.topMessages[i].second);
        }
        check(messagesMatch, "json top_messages entries match normal output");
    }

    // Empty log still produces valid JSON with zero counts.
    const std::string emptyJson = summaryToJson(analyze({}, 10));
    JsonValue emptyRoot;
    check(parseJson(emptyJson, emptyRoot), "json empty log parses as JSON");
    check(jsonNumber(emptyRoot, "total_lines") == 0, "json empty log total_lines 0");
    check(jsonNumber(emptyRoot, "parsed_lines") == 0, "json empty log parsed_lines 0");
    check(jsonNumber(emptyRoot, "malformed_lines") == 0, "json empty log malformed_lines 0");
    const JsonValue* emptyTop = emptyRoot.find("top_messages");
    check(emptyTop != nullptr && emptyTop->type == JsonValue::Type::Array &&
              emptyTop->array.empty(),
          "json empty log top_messages empty");

    // Messages with quotes, backslashes and tabs round-trip through JSON.
    const Summary tricky = analyze({
        "[INFO] Said \"hello\" and \\ backslash",
        "[INFO] Tab\there",
    });
    JsonValue trickyRoot;
    check(parseJson(summaryToJson(tricky), trickyRoot), "json tricky messages parse");
    const JsonValue* trickyTop = trickyRoot.find("top_messages");
    bool trickyOk = trickyTop != nullptr && trickyTop->array.size() == 2;
    if (trickyOk) {
        const JsonValue* m0 = trickyTop->array[0].find("message");
        const JsonValue* m1 = trickyTop->array[1].find("message");
        // Both messages occur once; ties sort alphabetically, so the
        // backslash message ("Said ...") sorts before "Tab...".
        trickyOk = m0 != nullptr && m0->str == "Said \"hello\" and \\ backslash" &&
                   m1 != nullptr && m1->str == "Tab\there";
    }
    check(trickyOk, "json escapes quotes, backslashes and tabs");

    // --summary-only equivalent: messages omitted, counts unchanged.
    const std::string summaryOnlyJson = summaryToJson(sampleSummary, false);
    JsonValue summaryOnlyRoot;
    check(parseJson(summaryOnlyJson, summaryOnlyRoot),
          "json summary-only parses as JSON");
    const JsonValue* summaryOnlyTop = summaryOnlyRoot.find("top_messages");
    check(summaryOnlyTop != nullptr && summaryOnlyTop->type == JsonValue::Type::Array &&
              summaryOnlyTop->array.empty(),
          "json summary-only top_messages empty");
    check(jsonNumber(summaryOnlyRoot, "total_lines") ==
              static_cast<double>(sampleSummary.totalLines),
          "json summary-only counts unchanged");

    if (g_failures == 0) {
        std::cout << "All " << g_checks << " checks passed.\n";
        return 0;
    }
    std::cerr << g_failures << " of " << g_checks << " checks failed.\n";
    return 1;
}
