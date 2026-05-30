# 2026-05-17 PR44 Depth-10 Suite Baseline

This snapshot records the search benchmark suite after the recent search
observability and move-ordering improvements through PR44.

Status: historical baseline snapshot. Recommendations in this snapshot describe
the profile used at the time of collection; they are evidence, not current
instructions.

## Context

- Date: 2026-05-17
- Reference PR: #44
- Reference commit: `bedc5345561f4363c97431e1ed63bb78edbf61d2`
- Benchmark suite: search suite, 25 positions
- Benchmark tool: `othello_search_bench`
- Build type: Release
- TT entries: default `262144`
- Depth 10 repetitions: 3
- Depth 12 repetitions: 1
- Output mode: `--by-position`

These numbers are local benchmark snapshots. They are intended as comparison
references for future search and evaluation work, not as CI thresholds.

## Recent Search Context

This baseline includes the following recent search changes:

- `SearchStats` / TT stats reporting
- depth-preferred transposition table replacement
- rejected TT store stats
- explicit private `MoveOrderingParams`
- `dynamic_opponent_mobility_penalty` tuned from `300` to `500`

## Commands

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release

./build-release/othello_search_bench --positions suite --mode both --depths 10 --repetitions 3 --tt off --by-position
./build-release/othello_search_bench --positions suite --mode both --depths 10 --repetitions 3 --tt on --by-position
./build-release/othello_search_bench --positions suite --mode both --depths 12 --repetitions 1 --tt on --by-position
```

The benchmark's per-position summary reports totals across repetitions. The
elapsed and node values below are normalized to one search for easier comparison.
The stats table keeps raw totals across all searches in each benchmark run.

## Depth 10 Summary

| Mode | TT | Avg ms | P50 ms | P95 ms | Max ms | Avg nodes | P50 nodes | P95 nodes | Max nodes |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| fixed | off | 41.361 | 33.921 | 118.541 | 123.627 | 248,763 | 234,268 | 697,793 | 761,118 |
| iterative | off | 63.616 | 53.934 | 187.677 | 234.324 | 406,847 | 351,890 | 1,108,734 | 1,524,451 |
| fixed | on | 36.975 | 31.568 | 103.641 | 114.978 | 192,486 | 163,413 | 591,540 | 600,502 |
| iterative | on | 56.028 | 44.011 | 147.998 | 195.791 | 316,722 | 256,382 | 795,117 | 1,144,383 |

## Depth 10 Search Stats

| Mode | TT | TT lookups | TT hits | TT hit rate | TT stores | TT collisions | TT rejected stores | Dynamic ordering nodes | Dynamic ordering moves |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| fixed | off | 0 | 0 | 0.000% | 0 | 0 | 0 | 651,132 | 5,451,093 |
| iterative | off | 0 | 0 | 0.000% | 0 | 0 | 0 | 955,335 | 8,091,381 |
| fixed | on | 14,436,483 | 741,327 | 5.135% | 11,895,012 | 3,747,546 | 1,800,144 | 530,316 | 4,507,203 |
| iterative | on | 23,754,129 | 1,264,440 | 5.323% | 18,828,390 | 6,743,817 | 3,661,299 | 790,530 | 6,774,057 |

## Depth 12 Summary

Depth 12 was also run as a higher-cost search-improvement check.

| Mode | TT | Avg ms | P50 ms | P95 ms | Max ms | Avg nodes | P50 nodes | P95 nodes | Max nodes |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| fixed | on | 335.906 | 214.270 | 1,172.843 | 1,200.785 | 1,847,247 | 1,418,985 | 6,243,256 | 6,665,518 |
| iterative | on | 556.028 | 400.612 | 1,693.957 | 2,212.838 | 3,129,330 | 2,210,102 | 10,421,143 | 12,771,546 |

## Depth 12 Search Stats

| Mode | TT | TT lookups | TT hits | TT hit rate | TT stores | TT collisions | TT rejected stores | Dynamic ordering nodes | Dynamic ordering moves |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| fixed | on | 46,181,176 | 1,356,665 | 2.938% | 18,616,096 | 13,522,410 | 26,208,415 | 1,679,350 | 14,900,643 |
| iterative | on | 78,233,254 | 1,988,635 | 2.542% | 26,575,216 | 18,602,272 | 49,669,403 | 2,582,640 | 22,978,925 |

## Historical Analysis Profile

At the time of this snapshot, the analysis profile was:

- fixed-depth search
- depth 10
- transposition table on

Depth 10 fixed search with TT on was fast and stable enough for single-position
analysis and move recommendation workflows in this snapshot. Iterative deepening
remained useful for future time-control-like workflows, but it did more
cumulative work than fixed-depth search at the same final depth in this run.

## Interpretation

- Dynamic move ordering and TT replacement tuning significantly reduced node
  counts compared with earlier depth-10 baselines.
- TT hit rate is not the only success metric. Lower collisions, rejected stores,
  and total node count can matter more than raw hit rate alone.
- Future search PRs were expected to compare p50, p95, max, nodes, TT stats, and
  dynamic ordering stats using the same benchmark commands when this snapshot
  was written.
- These are local measurements and should not be treated as hard CI thresholds.
