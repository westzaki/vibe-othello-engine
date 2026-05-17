# Search Baseline

This document records the current depth-10 search baseline so future search and
evaluation changes can be compared against a stable reference.

Depth 10 search is currently practical enough for move recommendation and
single-position analysis on the curated search benchmark suite. The recommended
analysis setting is:

- fixed-depth search
- depth 10
- transposition table on

The new `othello_analyze_position` CLI uses that setting by default. This does
not change the library-level `SearchOptions` defaults.

## Benchmark Commands

Use the by-position search benchmark to inspect both aggregate behavior and
tail latency across the suite:

```sh
./build-release/othello_search_bench --positions suite --mode both --depths 10 --repetitions 3 --tt off --by-position
./build-release/othello_search_bench --positions suite --mode both --depths 10 --repetitions 3 --tt on --by-position
```

## Current Baseline

These numbers are from the latest local search benchmark suite run at depth 10.
They are intended as a comparison reference, not as CI thresholds.

| Mode | TT | Avg ms | P50 ms | P95 ms | Max ms | Avg nodes |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| fixed | off | 45.3 | 33.5 | 123.4 | 127.6 | 290,215 |
| fixed | on | 37.8 | 29.0 | 102.7 | 120.4 | 220,426 |
| iterative | off | 69.4 | 49.2 | 197.7 | 248.2 | 467,844 |
| iterative | on | 56.4 | 40.8 | 150.1 | 190.7 | 348,604 |

## Notes

- These measurements are for the search benchmark suite and the local
  environment where they were collected.
- They are not hard pass/fail thresholds for CI.
- Fixed-depth search with the transposition table enabled is currently the most
  straightforward depth-10 analysis setting.
- Iterative deepening is useful for future time control work, but it currently
  does more cumulative work than fixed-depth search at the same final depth.
- Evaluation tuning should wait until analysis tooling and baseline comparison
  are stable.
