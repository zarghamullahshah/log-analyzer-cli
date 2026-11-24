// Tests for log-analyzer-cli. No framework — plain asserts with a small
// CHECK macro so the suite stays dependency-free. Run with `make test`.

#include <cassert>
#include <iostream>
#include <string>
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

    if (g_failures == 0) {
        std::cout << "All " << g_checks << " checks passed.\n";
        return 0;
    }
    std::cerr << g_failures << " of " << g_checks << " checks failed.\n";
    return 1;
}
