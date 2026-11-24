#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace loganalyzer {

// Severity levels, ordered from least to most severe.
enum class Level { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Fatal = 5 };

// Parse a level name such as "ERROR" (case-insensitive). Returns nullopt
// for anything that is not a known level.
std::optional<Level> parseLevel(const std::string& text);

// Canonical upper-case name of a level ("INFO", "ERROR", ...).
std::string levelName(Level level);

// One parsed log line.
struct LogEntry {
    std::string timestamp;  // "2026-11-24 10:15:03", empty when the line has none
    Level level = Level::Info;
    std::string message;
};

// Parse one log line.
//
// Accepted forms (timestamp is optional, level is required):
//   2026-11-24 10:15:03 [ERROR] Database connection failed
//   [WARN] Disk usage above 90%
//
// Lines that do not match return nullopt and are counted as malformed.
std::optional<LogEntry> parseLine(const std::string& line);

// Summary of a whole log stream.
struct Summary {
    std::size_t totalLines = 0;
    std::size_t parsedLines = 0;
    std::size_t malformedLines = 0;
    std::map<Level, std::size_t> byLevel;             // only levels actually seen
    std::vector<std::pair<std::string, std::size_t>> topMessages;  // most frequent first
};

// Analyse log lines. `topN` caps how many of the most frequent messages are
// reported (0 keeps all of them).
Summary analyze(const std::vector<std::string>& lines, std::size_t topN = 10);

// Keep only entries at or above `minLevel`.
std::vector<LogEntry> filterByLevel(const std::vector<LogEntry>& entries, Level minLevel);

}  // namespace loganalyzer
