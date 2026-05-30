# Search Benchmark

Status: historical performance reference.

This document describes historical search benchmark results as performance
evidence. It is not current project guidance, a task queue, or a permanent
benchmark standard. Individual measurement snapshots live under
[`docs/perf/baselines/`](baselines/) so benchmark history can be compared without
overwriting older results.

General benchmark commands and baseline policy live in
[`docs/benchmarks.md`](../benchmarks.md).

Baselines are append-only snapshots. Add a new file for each meaningful search
performance checkpoint rather than editing older snapshots in place.

At the time of these snapshots, depth 10 search was practical enough for move
recommendation and single-position analysis on the curated search benchmark
suite. The current example analysis setting was:

- fixed-depth search
- depth 10
- transposition table on
- `exact_endgame_empty_threshold = 12`

The `othello_analyze_position` CLI uses that setting by default. This matches
the library-level `SearchOptions` default at this checkpoint. Treat these values
as profile parameters and historical reference points, not permanent standards.

## Benchmark Commands

Use the by-position search benchmark to inspect both aggregate behavior and tail
latency across the suite:

```sh
./build-release/othello_search_bench --positions suite --mode both --depths 10 --repetitions 3 --tt on --by-position --exact-endgame-threshold 0
./build-release/othello_search_bench --positions suite --mode both --depths 10 --repetitions 3 --tt on --by-position --exact-endgame-threshold 12
./build-release/othello_search_bench --positions suite --mode both --depths 10 --repetitions 3 --tt off --by-position --exact-endgame-threshold 12
```

## Baseline Snapshots

Historical snapshots include:

- [2026-05-30 PR129 exact threshold gate](baselines/2026-05-30-pr129-exact-threshold-gate.md)
- [2026-05-23 PR48 exact threshold-12 suite baseline](baselines/2026-05-23-pr48-b564915-exact-threshold12-suite.md)
- [2026-05-17 PR44 depth-10 suite baseline](baselines/2026-05-17-pr44-bedc534-depth10-suite.md)
- [2026-05-17 PR37 depth-10 suite baseline](baselines/2026-05-17-pr37-5568457-depth10-suite.md)

## Notes

- Baseline measurements are for the search benchmark suite and the local
  environment where they were collected.
- They are not hard pass/fail thresholds for CI.
- Fixed-depth search with the transposition table enabled was the most
  straightforward analysis setting for these depth-10 snapshots.
- Exact endgame threshold `12` gave exact final margins for small endgames while
  keeping the suite latency practical at this checkpoint.
- Exact endgame threshold `0` was useful for comparing old depth-limited
  behavior.
- Iterative deepening was useful context for future time-control work in these
  snapshots, but it did more cumulative work than fixed-depth search at the same
  final depth in the recorded runs.
- Sequencing notes captured in these snapshots are historical evidence, not
  current blockers for evaluation, search, or performance work.
