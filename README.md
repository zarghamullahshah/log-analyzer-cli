# log-analyzer-cli

Analyse a log file from the command line: entries per severity level and the
most frequent messages. C++17, standard library only — no dependencies.

## What it does

- Parses log lines with an optional timestamp and a required `[LEVEL]` tag:

  ```
  2026-11-24 10:15:03 [ERROR] Database connection failed
  [WARN] Disk usage above 90%
  ```

- Counts entries per level (TRACE, DEBUG, INFO, WARN, ERROR, FATAL).
- Ranks the most frequent messages, so the noisiest failure surfaces first.
- Filters by minimum severity (`--level ERROR` keeps only errors and fatals).
- Counts malformed lines instead of crashing on them.
- Prints the same summary as machine-readable JSON with `--json`, for
  scripts and pipelines.

## Build

Requires a C++17 compiler (g++ 9+ or clang++ 10+).

```sh
make
```

## Run

```sh
./log-analyzer tests/sample.log
./log-analyzer --level ERROR app.log
./log-analyzer --top 5 --summary-only app.log
./log-analyzer --json app.log | jq .
tail -f app.log | ./log-analyzer
```

Example output:

```
Total lines:    10
Parsed lines:   9
Malformed lines:1

Entries by level:
  TRACE: 0
  DEBUG: 1
  INFO: 4
  WARN: 1
  ERROR: 2
  FATAL: 1

Most frequent messages:
  2x  Database connection failed
  1x  Server started
```

## JSON output

`--json` prints the same summary as a single JSON object on stdout, with
exactly the same numbers as the human-readable report:

```sh
./log-analyzer --json tests/sample.log
```

```json
{
  "total_lines": 10,
  "parsed_lines": 9,
  "malformed_lines": 1,
  "by_level": {"TRACE": 0, "DEBUG": 1, "INFO": 4, "WARN": 1, "ERROR": 2, "FATAL": 1},
  "top_messages": [
    {"message": "Database connection failed", "count": 2},
    {"message": "Server started", "count": 1}
  ]
}
```

It composes with the other flags: `--level` filters before analysing and
`--summary-only` omits `top_messages`. An empty log still yields valid JSON
with all counts at zero.

## Test

```sh
make test
```

Runs the dependency-free test suite in `tests/` (67 checks covering parsing,
level handling, counting, top-message ranking, filtering and JSON output).

## License

MIT
