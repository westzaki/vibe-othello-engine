# Search Benchmark

This document describes how to use the search benchmark results as a performance
reference. Individual measurement snapshots live under
[`docs/perf/baselines/`](baselines/) so benchmark history can be compared without
overwriting older results.

Baselines are append-only snapshots. Add a new file for each meaningful search
performance checkpoint rather than editing older snapshots in place.

Depth 10 search is currently practical enough for move recommendation and
single-position analysis on the curated search benchmark suite. The recommended
analysis setting is:

- fixed-depth search
- depth 10
- transposition table on

The `othello_analyze_position` CLI uses that setting by default. This does not
change the library-level `SearchOptions` defaults.

## Benchmark Commands

Use the by-position search benchmark to inspect both aggregate behavior and tail
latency across the suite:

```sh
./build-release/othello_search_bench --positions suite --mode both --depths 10 --repetitions 3 --tt off --by-position
./build-release/othello_search_bench --positions suite --mode both --depths 10 --repetitions 3 --tt on --by-position
```

## Baseline Snapshots

- Latest: [2026-05-17 PR44 depth-10 suite baseline](baselines/2026-05-17-pr44-bedc534-depth10-suite.md)
- [2026-05-17 PR37 depth-10 suite baseline](baselines/2026-05-17-pr37-5568457-depth10-suite.md)

## Notes

- Baseline measurements are for the search benchmark suite and the local
  environment where they were collected.
- They are not hard pass/fail thresholds for CI.
- Fixed-depth search with the transposition table enabled is currently the most
  straightforward depth-10 analysis setting.
- Iterative deepening is useful for future time control work, but it currently
  does more cumulative work than fixed-depth search at the same final depth.
- Evaluation tuning should wait until analysis tooling and baseline comparison
  are stable.
