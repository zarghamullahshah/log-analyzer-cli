#include "log_analyzer.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace loganalyzer {

namespace {

std::string upper(const std::string& text) {
    std::string out = text;
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// "2026-11-24 10:15:03" — strict shape check, digits and separators only.
bool looksLikeTimestamp(const std::string& text) {
    if (text.size() != 19) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (i == 4 || i == 7) {
            if (c != '-') return false;
        } else if (i == 10) {
            if (c != ' ') return false;
        } else if (i == 13 || i == 16) {
            if (c != ':') return false;
        } else if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::optional<Level> parseLevel(const std::string& text) {
    const std::string name = upper(trim(text));
    if (name == "TRACE") return Level::Trace;
    if (name == "DEBUG") return Level::Debug;
    if (name == "INFO") return Level::Info;
    if (name == "WARN" || name == "WARNING") return Level::Warn;
    if (name == "ERROR") return Level::Error;
    if (name == "FATAL") return Level::Fatal;
    return std::nullopt;
}

std::string levelName(Level level) {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
        case Level::Fatal: return "FATAL";
    }
    return "INFO";
}

std::optional<LogEntry> parseLine(const std::string& line) {
    std::string rest = trim(line);
    if (rest.empty()) return std::nullopt;

    LogEntry entry;

    // Optional leading timestamp.
    if (rest.size() >= 19 && looksLikeTimestamp(rest.substr(0, 19))) {
        entry.timestamp = rest.substr(0, 19);
        rest = trim(rest.substr(19));
    }

    // Required "[LEVEL]" tag.
    if (rest.size() < 3 || rest[0] != '[') return std::nullopt;
    const std::size_t close = rest.find(']');
    if (close == std::string::npos) return std::nullopt;
    const std::optional<Level> level = parseLevel(rest.substr(1, close - 1));
    if (!level.has_value()) return std::nullopt;
    entry.level = *level;

    entry.message = trim(rest.substr(close + 1));
    if (entry.message.empty()) return std::nullopt;
    return entry;
}

Summary analyze(const std::vector<std::string>& lines, std::size_t topN) {
    Summary summary;
    summary.totalLines = lines.size();

    std::map<std::string, std::size_t> messageCounts;
    for (const std::string& line : lines) {
        const std::optional<LogEntry> entry = parseLine(line);
        if (!entry.has_value()) {
            ++summary.malformedLines;
            continue;
        }
        ++summary.parsedLines;
        ++summary.byLevel[entry->level];
        ++messageCounts[entry->message];
    }

    summary.topMessages.assign(messageCounts.begin(), messageCounts.end());
    std::sort(summary.topMessages.begin(), summary.topMessages.end(),
              [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second > b.second;
                  return a.first < b.first;  // deterministic tie-break
              });
    if (topN > 0 && summary.topMessages.size() > topN) {
        summary.topMessages.resize(topN);
    }
    return summary;
}

std::vector<LogEntry> filterByLevel(const std::vector<LogEntry>& entries, Level minLevel) {
    std::vector<LogEntry> kept;
    for (const LogEntry& entry : entries) {
        if (static_cast<int>(entry.level) >= static_cast<int>(minLevel)) {
            kept.push_back(entry);
        }
    }
    return kept;
}

}  // namespace loganalyzer
