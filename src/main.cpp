#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "log_analyzer.h"

namespace {

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options] [file]\n"
              << "\n"
              << "Analyse a log file (or stdin when no file is given) and print a\n"
              << "summary: entries per severity level and the most frequent messages.\n"
              << "\n"
              << "Log line format (timestamp optional, level required):\n"
              << "  2026-11-24 10:15:03 [ERROR] Database connection failed\n"
              << "  [WARN] Disk usage above 90%\n"
              << "\n"
              << "Options:\n"
              << "  --level <LEVEL>   Only show entries at or above LEVEL\n"
              << "                      (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)\n"
              << "  --top <N>         Show the N most frequent messages (default 10,\n"
              << "                      0 shows all)\n"
              << "  --summary-only    Print only the per-level counts, no messages\n"
              << "  --json            Print the same summary as JSON on stdout,\n"
              << "                      instead of the human-readable report\n"
              << "  -h, --help        Show this help\n";
}

int fail(const std::string& message) {
    std::cerr << "log-analyzer: " << message << "\n";
    return 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    using namespace loganalyzer;

    std::string filePath;
    std::optional<Level> minLevel;
    std::size_t topN = 10;
    bool summaryOnly = false;
    bool jsonOutput = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--summary-only") {
            summaryOnly = true;
        } else if (arg == "--json") {
            jsonOutput = true;
        } else if (arg == "--level" || arg == "--top") {
            if (i + 1 >= argc) return fail("missing value for " + arg);
            const std::string value = argv[++i];
            if (arg == "--level") {
                minLevel = parseLevel(value);
                if (!minLevel.has_value()) return fail("unknown level: " + value);
            } else {
                try {
                    topN = static_cast<std::size_t>(std::stoul(value));
                } catch (...) {
                    return fail("invalid number for --top: " + value);
                }
            }
        } else if (!arg.empty() && arg[0] == '-') {
            return fail("unknown option: " + arg);
        } else if (filePath.empty()) {
            filePath = arg;
        } else {
            return fail("unexpected extra argument: " + arg);
        }
    }

    std::vector<std::string> lines;
    if (filePath.empty()) {
        std::string line;
        while (std::getline(std::cin, line)) lines.push_back(line);
    } else {
        std::ifstream in(filePath);
        if (!in) return fail("cannot open file: " + filePath);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }

    // Apply the level filter before analysing, so the summary reflects it.
    if (minLevel.has_value()) {
        std::vector<LogEntry> entries;
        for (const std::string& line : lines) {
            const std::optional<LogEntry> entry = parseLine(line);
            if (entry.has_value()) entries.push_back(*entry);
        }
        const std::vector<LogEntry> kept = filterByLevel(entries, *minLevel);
        lines.clear();
        for (const LogEntry& entry : kept) {
            std::string rebuilt = entry.timestamp.empty() ? "" : entry.timestamp + " ";
            rebuilt += "[" + levelName(entry.level) + "] " + entry.message;
            lines.push_back(rebuilt);
        }
    }

    const Summary summary = analyze(lines, topN);

    if (jsonOutput) {
        // Same numbers as the human-readable report, as machine-readable JSON.
        std::cout << summaryToJson(summary, /*includeMessages=*/!summaryOnly);
        return 0;
    }

    std::cout << "Total lines:    " << summary.totalLines << "\n"
              << "Parsed lines:   " << summary.parsedLines << "\n"
              << "Malformed lines:" << summary.malformedLines << "\n\n"
              << "Entries by level:\n";
    for (Level level : {Level::Trace, Level::Debug, Level::Info,
                        Level::Warn, Level::Error, Level::Fatal}) {
        const auto it = summary.byLevel.find(level);
        std::cout << "  " << levelName(level) << ": "
                  << (it == summary.byLevel.end() ? 0 : it->second) << "\n";
    }

    if (!summaryOnly && !summary.topMessages.empty()) {
        std::cout << "\nMost frequent messages:\n";
        for (const auto& [message, count] : summary.topMessages) {
            std::cout << "  " << count << "x  " << message << "\n";
        }
    }
    return 0;
}
