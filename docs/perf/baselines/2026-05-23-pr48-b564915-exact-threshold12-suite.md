# 2026-05-23 PR48 Exact Threshold-12 Search Suite Baseline

This snapshot records the search benchmark suite after PR48 integrated root-only
exact endgame solving into the depth-limited search entry points.

Status: historical baseline snapshot. Recommendations in this snapshot describe
the profile used at the time of collection; they are evidence, not current
instructions.

## Context

- Date: 2026-05-23
- Reference PR: #48
- Reference commit: `b5649155601b9fc7ef24d0c86bbea92d3defe86e`
- Benchmark suite: search suite, 25 positions
- Benchmark tool: `othello_search_bench`
- Build type: Release
- TT entries: default `262144`
- Depth 10 repetitions: 3
- Depth 12 repetitions: 1
- Default exact endgame threshold: `12`
- Comparison exact endgame threshold: `0`
- Output mode: `--by-position`

These numbers are local benchmark snapshots. They are intended as comparison
references for future search and evaluation work, not as CI thresholds.

## Recent Search Context

This baseline includes the PR48 root exact endgame integration:

- `SearchOptions::exact_endgame_empty_threshold`
- default exact endgame threshold `12`
- root-only exact solving in `search()` and `search_iterative()`
- exact threshold control in `othello_search_bench` and `othello_analyze_position`

The exact endgame solver remains separate from the alpha-beta search
implementation.

## Commands

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release

./build-release/othello_search_bench --positions suite --mode both --depths 10 --repetitions 3 --tt on --by-position --exact-endgame-threshold 0
./build-release/othello_search_bench --positions suite --mode both --depths 10 --repetitions 3 --tt on --by-position --exact-endgame-threshold 12
./build-release/othello_search_bench --positions suite --mode both --depths 10 --repetitions 3 --tt off --by-position --exact-endgame-threshold 12

./build-release/othello_endgame_bench --empties 12 --repetitions 1
./build-release/othello_endgame_bench --empties 14 --repetitions 1

./build-release/othello_search_bench --positions suite --mode both --depths 12 --repetitions 1 --tt on --by-position --exact-endgame-threshold 12
```

The benchmark's per-position summary reports totals across repetitions. The
elapsed and node values below are per-position totals from the benchmark summary.
The stats table keeps raw totals across all searches in each benchmark run.

## Depth 10 Summary

| Exact threshold | Mode | TT | Avg ms | P50 ms | P95 ms | Max ms | Avg nodes | P50 nodes | P95 nodes | Max nodes |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | fixed | on | 100.401 | 85.168 | 288.404 | 317.239 | 577,459 | 490,239 | 1,774,620 | 1,801,506 |
| 0 | iterative | on | 154.655 | 122.988 | 407.098 | 549.658 | 950,165 | 769,146 | 2,385,351 | 3,433,149 |
| 12 | fixed | on | 99.423 | 84.912 | 291.504 | 321.853 | 579,326 | 490,239 | 1,774,620 | 1,801,506 |
| 12 | iterative | on | 152.594 | 122.919 | 406.997 | 544.500 | 951,351 | 769,146 | 2,385,351 | 3,433,149 |
| 12 | fixed | off | 121.987 | 99.828 | 348.689 | 365.101 | 747,371 | 702,804 | 2,093,379 | 2,283,354 |
| 12 | iterative | off | 293.324 | 158.357 | 579.414 | 3,313.190 | 1,220,733 | 1,055,670 | 3,326,202 | 4,573,353 |

## Depth 10 Search Stats

| Exact threshold | Mode | TT | TT lookups | TT hits | TT hit rate | TT stores | TT collisions | TT rejected stores | Dynamic ordering nodes | Dynamic ordering moves |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | fixed | on | 14,436,483 | 741,327 | 5.135% | 11,895,012 | 3,747,546 | 1,800,144 | 530,316 | 4,507,203 |
| 0 | iterative | on | 23,754,129 | 1,264,440 | 5.323% | 18,828,390 | 6,743,817 | 3,661,299 | 790,530 | 6,774,057 |
| 12 | fixed | on | 14,401,665 | 737,952 | 5.124% | 11,863,803 | 3,747,207 | 1,799,910 | 529,341 | 4,502,004 |
| 12 | iterative | on | 23,702,271 | 1,260,153 | 5.317% | 18,781,053 | 6,743,502 | 3,661,065 | 788,208 | 6,761,133 |
| 12 | fixed | off | 0 | 0 | 0.000% | 0 | 0 | 0 | 649,902 | 5,444,580 |
| 12 | iterative | off | 0 | 0 | 0.000% | 0 | 0 | 0 | 952,440 | 8,075,481 |

## Depth 12 Reference

Depth 12 was also run as a higher-cost reference with the default exact endgame
threshold enabled.

| Exact threshold | Mode | TT | Avg ms | P50 ms | P95 ms | Max ms | Avg nodes | P50 nodes | P95 nodes | Max nodes |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 12 | fixed | on | 310.922 | 194.624 | 1,101.947 | 1,114.704 | 1,847,880 | 1,418,985 | 6,243,256 | 6,665,518 |
| 12 | iterative | on | 501.178 | 366.738 | 1,579.373 | 2,021.776 | 3,128,945 | 2,210,102 | 10,421,143 | 12,771,546 |

## Depth 12 Search Stats

| Exact threshold | Mode | TT | TT lookups | TT hits | TT hit rate | TT stores | TT collisions | TT rejected stores | Dynamic ordering nodes | Dynamic ordering moves |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 12 | fixed | on | 46,169,838 | 1,355,659 | 2.936% | 18,605,851 | 13,522,308 | 26,208,328 | 1,679,065 | 14,899,118 |
| 12 | iterative | on | 78,196,463 | 1,985,488 | 2.539% | 26,541,900 | 18,601,956 | 49,669,075 | 2,581,352 | 22,971,846 |

## Exact Endgame References

| Empties | Count | Total ms | Avg ms | P50 ms | P95 ms | Max ms | Avg nodes | P50 nodes | P95 nodes | Max nodes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 12 | 2 | 38.584 | 19.292 | 5.620 | 32.964 | 32.964 | 206,934 | 24,623 | 389,245 | 389,245 |
| 14 | 13 | 5,463.929 | 420.302 | 71.728 | 1,886.312 | 1,886.312 | 5,522,350 | 851,508 | 24,036,129 | 24,036,129 |

## Historical Analysis Profile

At the time of this snapshot, the analysis profile was:

- fixed-depth search
- depth 10
- transposition table on
- `exact_endgame_empty_threshold = 12`

Threshold `12` gave exact final margins for small endgames while keeping the
suite latency practical in this snapshot. Threshold `0` was useful for comparing
against the old depth-limited behavior without exact root solving.

## Interpretation

- Threshold `12` slightly changes the endgame rows because they now use exact
  final margins and exact principal variations at the root.
- The depth-10 suite remains practical with the default threshold enabled.
- The 12-empty exact reference remains small enough for the default threshold in
  this fixture set.
- The 14-empty reference remains explicit and experimental because
  high-mobility, corner, and parity-like fixtures can reach seconds-scale p95/max
  latency and tens of millions of nodes.
- These are local measurements and should not be treated as hard CI thresholds.
