# 2026-05-23 PR70 Exact Endgame Interior PVS Baseline

This snapshot records the exact endgame benchmark profile after the recent
standalone exact solver improvements through PR70.

## Context

- Date: 2026-05-23
- Reference PR: #70
- Reference commit: `0c89ba40de1d5f78f2c79b425f2d9782b776781e`
- Branch used for measurement: `codex/endgame-baseline-interior-pvs`
- Benchmark tool: `othello_endgame_bench`
- Build type: Release
- Exact solver features included:
  - endgame-specific move ordering
  - private exact endgame transposition table
  - compact exact TT entries
  - incremental exact endgame hashing
  - root PVS / null-window search
  - interior PVS / null-window search
- Fixture set includes 14, 16, 18, and expanded 20-empty mixed/stress-lite
  positions.

These are local benchmark numbers for comparison and interpretation. They are
not CI thresholds.

## Commands

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release

./build-release/othello_endgame_bench --empties 14 --repetitions 1
./build-release/othello_endgame_bench --empties 16 --repetitions 1
./build-release/othello_endgame_bench --empties 18 --repetitions 1
./build-release/othello_endgame_bench --empties 20 --repetitions 1

./build-release/othello_endgame_bench --positions endgame --repetitions 1
./build-release/othello_endgame_bench --empties 20 --repetitions 1 --root-breakdown

./build-release/othello_endgame_bench --empties 18 --repetitions 3
./build-release/othello_endgame_bench --empties 20 --repetitions 3

./build-release/othello_search_bench --positions suite --mode fixed --depths 10 --repetitions 1 --tt on --by-position
./build-release/othello_analyze_position --help
```

The repeated 18/20-empty runs were collected as noise checks. The primary tables
below use the single-run exact benchmark commands unless explicitly labeled as a
repeated reference.

## Exact Endgame Summary

| Empties | Count | Avg ms | P50 ms | P95 ms | Max ms | Avg nodes | P50 nodes | P95 nodes | Max nodes | TT hit rate | TT collisions | TT rejected stores |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 14 | 13 | 98.493 | 57.225 | 335.770 | 335.770 | 532,164 | 324,421 | 1,935,402 | 1,935,402 | 16.64% | 891,872 | 1,089,294 |
| 16 | 8 | 389.564 | 202.247 | 1,127.021 | 1,127.021 | 2,094,168 | 1,065,103 | 5,834,276 | 5,834,276 | 9.62% | 3,674,806 | 5,852,191 |
| 18 | 5 | 1,576.138 | 1,965.582 | 3,248.731 | 3,248.731 | 8,649,574 | 10,676,086 | 18,036,402 | 18,036,402 | 4.26% | 8,144,188 | 29,030,321 |
| 20 | 9 | 3,253.203 | 2,745.674 | 10,360.153 | 10,360.153 | 16,173,951 | 11,783,597 | 52,780,292 | 52,780,292 | 2.98% | 19,608,898 | 113,158,537 |

## Full Endgame Suite Reference

The full endgame suite contained 49 positions in this snapshot.

| Empties | Count | Avg ms | P50 ms | P95 ms | Max ms | Avg nodes | P50 nodes | P95 nodes | Max nodes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 2 | 0.002 | 0.001 | 0.003 | 0.003 | 2 | 2 | 3 | 3 |
| 2 | 2 | 0.001 | 0.001 | 0.001 | 0.001 | 6 | 6 | 7 | 7 |
| 4 | 2 | 0.003 | 0.000 | 0.007 | 0.007 | 20 | 2 | 37 | 37 |
| 6 | 2 | 0.017 | 0.000 | 0.034 | 0.034 | 110 | 2 | 219 | 219 |
| 8 | 2 | 0.054 | 0.028 | 0.081 | 0.081 | 344 | 184 | 503 | 503 |
| 10 | 2 | 2.104 | 0.483 | 3.724 | 3.724 | 13,169 | 3,005 | 23,333 | 23,333 |
| 12 | 2 | 4.423 | 1.915 | 6.931 | 6.931 | 27,551 | 10,234 | 44,868 | 44,868 |
| 14 | 13 | 92.419 | 56.310 | 305.136 | 305.136 | 532,164 | 324,421 | 1,935,402 | 1,935,402 |
| 16 | 8 | 361.346 | 188.329 | 966.666 | 966.666 | 2,094,168 | 1,065,103 | 5,834,276 | 5,834,276 |
| 18 | 5 | 1,586.481 | 1,986.229 | 3,200.781 | 3,200.781 | 8,649,574 | 10,676,086 | 18,036,402 | 18,036,402 |
| 20 | 9 | 3,063.550 | 2,181.194 | 10,150.766 | 10,150.766 | 16,173,951 | 11,783,597 | 52,780,292 | 52,780,292 |

## 20-Empty Per-Position Summary

| Position | Elapsed ms | Nodes | Best move | Margin | Tags |
| --- | ---: | ---: | --- | ---: | --- |
| 20-empty-low-mobility | 2,869.968 | 15,447,073 | h3 | -54 | `experimental_20`, `low_mobility`, `low_branching` |
| 20-empty-root-pass | 41.896 | 243,161 | - | -64 | `experimental_20`, `pass`, `edge_heavy`, `low_branching` |
| 20-empty-edge-heavy-low-branching | 5,465.222 | 30,672,761 | a7 | -55 | `experimental_20`, `edge_heavy`, `low_branching` |
| 20-empty-corner-available-low-branching | 237.682 | 1,142,204 | h7 | 22 | `experimental_20`, `corner_available`, `low_branching` |
| 20-empty-normal-mobility | 1,667.112 | 8,097,006 | b8 | 28 | `experimental_20`, `mixed_20`, `normal_mobility`, `edge_heavy` |
| 20-empty-high-mobility-lite | 10,360.153 | 52,780,292 | h5 | 12 | `experimental_20`, `stress_lite_20`, `high_mobility`, `corner_available`, `edge_heavy`, `x_square_risk` |
| 20-empty-corner-race-lite | 3,592.081 | 15,360,066 | a1 | -2 | `experimental_20`, `stress_lite_20`, `corner_race`, `corner_available`, `edge_heavy` |
| 20-empty-edge-heavy-stress-lite | 2,299.042 | 10,039,398 | b1 | 16 | `experimental_20`, `stress_lite_20`, `edge_heavy`, `normal_mobility` |
| 20-empty-parity-ish | 2,745.674 | 11,783,597 | c5 | 26 | `experimental_20`, `mixed_20`, `parity-ish`, `opponent_pass_after_move`, `low_mobility` |

## 20-Empty Root Breakdown Summary

Root breakdown solves each root candidate separately. It is useful for diagnosing
candidate-level cost, but total candidate time is not directly comparable with a
single normal root solve.

| Position | Candidates | Total candidate ms | Total nodes | Worst move | Worst ms | Worst nodes | Worst rank | Worst candidate is best | Avg TT hit rate |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | --- | ---: |
| 20-empty-corner-available-low-branching | 3 | 357.688 | 1,888,312 | g8 | 140.701 | 765,677 | 3 | no | 9.41% |
| 20-empty-corner-race-lite | 6 | 11,883.216 | 62,230,821 | a1 | 2,726.037 | 13,496,775 | 1 | yes | 4.54% |
| 20-empty-edge-heavy-low-branching | 3 | 4,311.598 | 24,064,432 | b6 | 2,021.637 | 11,084,630 | 3 | no | 5.94% |
| 20-empty-edge-heavy-stress-lite | 6 | 10,221.283 | 54,619,402 | a4 | 3,051.986 | 15,743,009 | 2 | no | 3.40% |
| 20-empty-high-mobility-lite | 9 | 25,664.975 | 128,376,459 | f1 | 6,418.095 | 26,024,813 | 5 | no | 3.68% |
| 20-empty-low-mobility | 1 | 2,858.052 | 15,447,072 | h3 | 2,858.052 | 15,447,072 | 1 | yes | 4.47% |
| 20-empty-normal-mobility | 5 | 6,116.352 | 29,564,709 | g4 | 1,731.947 | 8,098,269 | 2 | no | 4.21% |
| 20-empty-parity-ish | 4 | 3,558.525 | 22,343,788 | c5 | 1,374.370 | 8,763,583 | 1 | yes | 6.56% |
| 20-empty-root-pass | 1 | 45.601 | 243,160 | pass | 45.601 | 243,160 | 1 | yes | 17.44% |

## Repeated 18/20 References

| Empties | Repetitions | Count | Avg ms | P50 ms | P95 ms | Max ms | Avg nodes | P50 nodes | P95 nodes | Max nodes | TT hit rate |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 18 | 3 | 5 | 1,492.876 | 2,010.648 | 2,980.494 | 2,980.494 | 8,649,574 | 10,676,086 | 18,036,402 | 18,036,402 | 4.26% |
| 20 | 3 | 9 | 3,530.352 | 2,960.652 | 11,246.091 | 11,246.091 | 16,173,951 | 11,783,597 | 52,780,292 | 52,780,292 | 2.98% |

## Interpretation

- `exact_endgame_empty_threshold = 12` remains the default for normal search
  integration.
- 14 empty is strong CPU / analysis-friendly on this fixture set.
- 16 empty is now much more practical after the exact solver improvements.
- 18 empty is significantly improved and useful for analysis, with the current
  p95/max around 3 seconds on these local runs.
- 20 mixed/stress-lite is measurable but still heavy. The current 20-empty
  p95/max is the main optimization target.
- The expanded 20 set shows both best-candidate-heavy and non-best-candidate
  costs. `20-empty-high-mobility-lite` remains the clear tail, with low TT hit
  rate and expensive non-best candidates in the root breakdown.
- Interior PVS was a major step forward, reducing the previous 18/20 tail enough
  to make 20-empty diagnostics practical.
- 22-light fixtures should wait until the 20-empty tail is better understood or
  further optimized.

Likely next optimization directions:

- analyze heavy 20-empty candidates after interior PVS
- try gated edge/corner-race-aware ordering
- continue TT/hash/cache work where low hit rate and rejected stores dominate
- add 22-light fixtures only after the 20-empty tail is less spiky
