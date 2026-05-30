# 2026-05-24 PR78 Exact Endgame Child Breakdown Baseline

This snapshot records the exact endgame benchmark profile on `main` at
collection time after PR78 added expanded worst-root child breakdown diagnostics
to `othello_endgame_bench`.

Status: historical baseline snapshot. Follow-up ideas in this snapshot describe
what looked useful at the time of collection; they are evidence, not current
instructions.

## Context

- Date: 2026-05-24
- Reference PR: #78
- Reference commit: `8d31a40e86cfa3abe3786f737ca8a8a6eb40cc37`
- Branch used for measurement: `codex/endgame-benchmark-docs-after-78`
- Benchmark tools:
  - `othello_endgame_bench`
  - `othello_rule_core_bench`
- Build type: Release
- Exact solver features included:
  - endgame-specific move ordering
  - private exact endgame transposition table
  - compact exact TT entries
  - incremental exact endgame hashing
  - root PVS / null-window search
  - interior PVS / null-window search
  - last-N exact endgame specialization
- Exact benchmark observability included:
  - position metrics
  - root candidate breakdown
  - expanded worst-root child breakdown

These are local benchmark numbers for comparison and interpretation. They are
not CI thresholds.

## Commands

Standard exact endgame benchmarks:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release

./build-release/othello_endgame_bench --empties 14 --repetitions 1
./build-release/othello_endgame_bench --empties 16 --repetitions 1
./build-release/othello_endgame_bench --empties 18 --repetitions 1
./build-release/othello_endgame_bench --empties 20 --repetitions 1
```

Diagnostic exact endgame commands:

```sh
./build-release/othello_endgame_bench --empties 20 --repetitions 1 --root-breakdown
./build-release/othello_endgame_bench --empties 20 --repetitions 1 --root-breakdown --expand-worst-candidate
```

Rule-core context:

```sh
./build-release/othello_rule_core_bench --positions suite --iterations 100000
```

The diagnostic runs solve root candidates, and then selected child candidates,
separately. Use them to understand where time goes; do not compare their total
candidate time directly to a single normal root solve.

## Exact Endgame Summary

| Empties | Count | Avg ms | P50 ms | P95 ms | Max ms | Avg nodes | P50 nodes | P95 nodes | Max nodes | TT hit rate | TT collisions | TT rejected stores |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 14 | 13 | 67.896 | 39.389 | 268.556 | 268.556 | 790,600 | 425,040 | 3,619,059 | 3,619,059 | 17.03% | 54,651 | 37,669 |
| 16 | 8 | 238.576 | 122.898 | 620.705 | 620.705 | 2,569,142 | 1,259,663 | 7,062,312 | 7,062,312 | 10.60% | 331,057 | 217,445 |
| 18 | 10 | 1,709.996 | 1,755.447 | 3,692.558 | 3,692.558 | 16,325,763 | 16,267,255 | 35,465,088 | 35,465,088 | 8.58% | 7,435,892 | 8,808,229 |
| 20 | 9 | 1,886.089 | 1,292.678 | 5,960.363 | 5,960.363 | 17,234,234 | 13,177,212 | 54,508,358 | 54,508,358 | 6.48% | 7,159,408 | 12,247,436 |

## 20-Empty Per-Position Summary

| Position | Elapsed ms | Nodes | Best move | Margin | TT hit rate | Tags |
| --- | ---: | ---: | --- | ---: | ---: | --- |
| 20-empty-low-mobility | 1,877.909 | 16,692,266 | h3 | -54 | 7.41% | `experimental_20`, `low_mobility`, `low_branching` |
| 20-empty-root-pass | 44.666 | 306,481 | - | -64 | 11.85% | `experimental_20`, `pass`, `edge_heavy`, `low_branching` |
| 20-empty-edge-heavy-low-branching | 3,294.531 | 32,667,823 | a7 | -55 | 7.57% | `experimental_20`, `edge_heavy`, `low_branching` |
| 20-empty-corner-available-low-branching | 168.997 | 1,370,609 | h7 | 22 | 8.89% | `experimental_20`, `corner_available`, `low_branching` |
| 20-empty-normal-mobility | 982.999 | 9,023,031 | b8 | 28 | 6.09% | `experimental_20`, `mixed_20`, `normal_mobility`, `edge_heavy` |
| 20-empty-high-mobility-lite | 5,960.363 | 54,508,358 | h5 | 12 | 4.95% | `experimental_20`, `stress_lite_20`, `high_mobility`, `corner_available`, `edge_heavy`, `x_square_risk` |
| 20-empty-corner-race-lite | 2,072.048 | 16,315,992 | a1 | -2 | 7.82% | `experimental_20`, `stress_lite_20`, `corner_race`, `corner_available`, `edge_heavy` |
| 20-empty-edge-heavy-stress-lite | 1,292.678 | 11,046,330 | b1 | 16 | 5.73% | `experimental_20`, `stress_lite_20`, `edge_heavy`, `normal_mobility` |
| 20-empty-parity-ish | 1,280.610 | 13,177,212 | c5 | 26 | 8.21% | `experimental_20`, `mixed_20`, `parity-ish`, `opponent_pass_after_move`, `low_mobility` |

## 20-Empty Root Breakdown

Root breakdown solves each root candidate independently. The table below is from
`--empties 20 --repetitions 1 --root-breakdown`.

| Position | Candidates | Total candidate ms | Total nodes | Worst move | Worst ms | Worst nodes | Worst margin | Worst rank | Worst candidate is best | Avg TT hit rate |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | --- | ---: |
| 20-empty-corner-available-low-branching | 3 | 256.648 | 2,267,889 | g8 | 101.876 | 921,887 | -7 | 3 | no | 8.16% |
| 20-empty-corner-race-lite | 6 | 8,924.235 | 66,618,044 | a6 | 1,906.036 | 13,391,743 | -6 | 3 | no | 9.22% |
| 20-empty-edge-heavy-low-branching | 3 | 2,726.851 | 26,881,421 | b6 | 1,254.095 | 11,961,129 | -64 | 3 | no | 9.28% |
| 20-empty-edge-heavy-stress-lite | 6 | 7,299.356 | 59,924,527 | a4 | 1,963.918 | 17,078,714 | 16 | 2 | no | 6.52% |
| 20-empty-high-mobility-lite | 9 | 15,007.988 | 137,713,172 | f1 | 2,944.462 | 27,389,015 | 2 | 5 | no | 7.77% |
| 20-empty-low-mobility | 1 | 1,800.233 | 16,692,265 | h3 | 1,800.233 | 16,692,265 | -54 | 1 | yes | 7.41% |
| 20-empty-normal-mobility | 5 | 3,678.568 | 32,546,901 | b8 | 1,021.988 | 8,809,938 | 28 | 1 | yes | 6.40% |
| 20-empty-parity-ish | 4 | 2,494.954 | 25,891,743 | c5 | 945.327 | 9,921,644 | 26 | 1 | yes | 9.42% |
| 20-empty-root-pass | 1 | 37.568 | 306,480 | pass | 37.568 | 306,480 | -64 | 1 | yes | 11.85% |

## 20-Empty Expanded Worst-Root Child Breakdown

Expanded child breakdown solves the slowest root candidate's child candidates
independently. The table below is from
`--empties 20 --repetitions 1 --root-breakdown --expand-worst-candidate`.

| Position | Expanded root | Children | Total child ms | Total nodes | Worst child | Worst ms | Worst nodes | Node margin | Worst rank | Worst child is best | Avg TT hit rate |
| --- | --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | --- | ---: |
| 20-empty-corner-available-low-branching | g8 | 7 | 836.883 | 5,838,641 | d6 | 335.279 | 2,067,947 | 2 | 5 | no | 7.80% |
| 20-empty-corner-race-lite | a1 | 11 | 11,263.661 | 90,655,111 | f7 | 2,346.121 | 19,131,957 | -28 | 11 | no | 9.29% |
| 20-empty-edge-heavy-low-branching | b6 | 12 | 14,826.604 | 131,897,666 | g2 | 2,357.910 | 21,995,239 | 24 | 10 | no | 9.13% |
| 20-empty-edge-heavy-stress-lite | a4 | 13 | 9,912.854 | 87,632,532 | g7 | 2,821.645 | 26,413,177 | -28 | 10 | no | 8.09% |
| 20-empty-high-mobility-lite | f1 | 12 | 13,201.954 | 122,461,573 | b2 | 3,804.728 | 36,879,282 | -10 | 8 | no | 9.38% |
| 20-empty-low-mobility | h3 | 10 | 4,470.700 | 40,439,765 | f7 | 985.300 | 9,295,231 | 48 | 6 | no | 8.47% |
| 20-empty-normal-mobility | g4 | 11 | 4,197.566 | 39,950,161 | f8 | 719.063 | 6,950,940 | -34 | 11 | no | 7.53% |
| 20-empty-parity-ish | c5 | 1 | 946.080 | 9,921,643 | pass | 946.080 | 9,921,643 | -26 | 1 | yes | 8.28% |
| 20-empty-root-pass | pass | 7 | 410.492 | 3,705,938 | e7 | 107.613 | 1,012,650 | 64 | 3 | no | 12.45% |

## Rule-Core Benchmark Summary

This rule-core context was collected with
`./build-release/othello_rule_core_bench --positions suite --iterations 100000`.

| Operation | Total calls | Total elapsed ms | Avg ns/call |
| --- | ---: | ---: | ---: |
| legal_moves | 1,200,000 | 15.587 | 12.989 |
| flips_for_move | 76,800,000 | 252.456 | 3.287 |
| apply_move | 76,800,000 | 333.676 | 4.345 |
| pass_turn | 1,200,000 | 17.702 | 14.752 |
| is_game_over | 1,200,000 | 17.627 | 14.689 |
| disc_count | 2,400,000 | 2.322 | 0.968 |
| score | 2,400,000 | 2.438 | 1.016 |

## Interpretation

- `exact_endgame_empty_threshold = 12` remains the default for normal search
  integration.
- 14 empty remains comfortable for strong CPU / analysis-style use on this
  fixture set.
- 16 empty is practical on this local run, with max around 0.62s.
- 18 empty is useful for analysis, but the heavy tail still reaches about 3.7s.
- 20 mixed/stress-lite remained the optimization target at this checkpoint. The
  standard max is about 6s in this snapshot, and diagnostic root/child
  breakdowns show expensive non-best subtrees.
- `20-empty-high-mobility-lite` is still the standard-run tail and has the
  lowest 20-empty TT hit rate in this snapshot.
- Expanded child diagnostics show several costly non-best child candidates:
  `20-empty-high-mobility-lite` / `f1` / `b2`, `20-empty-edge-heavy-stress-lite`
  / `a4` / `g7`, and `20-empty-edge-heavy-low-branching` / `b6` / `g2`.

Historical follow-up ideas:

- collect PVS scout/research and ordering-position counters for heavy 20
  candidates
- use those counters before adding another gated ordering heuristic
- consider TT/cache work where low hit rate and rejected stores remain high
- delay 22-light fixture expansion until the 20-empty tail is better understood
