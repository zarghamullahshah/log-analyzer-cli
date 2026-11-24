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

## Test

```sh
make test
```

Runs the dependency-free test suite in `tests/` (41 checks covering parsing,
level handling, counting, top-message ranking and filtering).

## License

MIT
