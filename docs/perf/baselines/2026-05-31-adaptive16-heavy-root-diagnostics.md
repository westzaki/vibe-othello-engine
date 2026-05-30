# Adaptive16 Heavy Exact Root Diagnostics

Date: 2026-05-31

Base commit: `ea742a9`

Build: Release

## Scope

This report diagnoses heavy exact roots seen after the adaptive16 match-runner
work. It does not change `solve_exact_endgame()` or exact-search semantics:
there is no forward pruning, MPC, LMR, ProbCut, or eval-based move skipping in
the exact core.

## Commands

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --target othello_tests othello_match_runner othello_endgame_bench -j 8
ctest --test-dir build/release --output-on-failure
```

Targeted opening subset:

```sh
rg "^(g12_p0_5_s0|g27_p0_6_s3|g15_p0_5_s3|g25_p0_6_s1|g0_p0_4_s0|g1_p0_4_s1|g24_p0_6_s0):" \
  data/openings/eval_regression_openings.txt > /private/tmp/adaptive16_diagnostic_openings.txt

./build/release/othello_match_runner \
  --black search:depth=5,tt=on,pvs=on,exact=14 \
  --white search:depth=5,tt=on,pvs=on,exact=adaptive16 \
  --games 14 \
  --swap-sides true \
  --seed 135031 \
  --openings /private/tmp/adaptive16_diagnostic_openings.txt \
  --output build/release/adaptive16-heavy-diagnostics.jsonl \
  --quiet

python3 tools/scripts/match_summary.py \
  --input build/release/adaptive16-heavy-diagnostics.jsonl \
  --by-opening
```

Root breakdown:

```sh
for pos in \
  adaptive16-heavy-g27-ply45 \
  adaptive16-heavy-g12-ply44 \
  adaptive16-heavy-g12-ply45 \
  adaptive16-heavy-g25-ply45 \
  adaptive16-heavy-g27-ply44
do
  ./build/release/othello_endgame_bench \
    --positions endgame \
    --breakdown-position "$pos" \
    --repetitions 1 \
    --root-breakdown \
    --expand-worst-candidate
done > build/release/adaptive16-heavy-root-breakdown.txt
```

## Match Subset

| metric | exact14 player A | adaptive16 player B |
|---|---:|---:|
| games | 14 | 14 |
| wins | 4 | 10 |
| average nodes | 488,590.50 | 1,773,256.79 |
| average elapsed ms | 107.91 | 328.04 |
| total exact roots | 95 | 115 |
| average exact roots | 6.79 | 8.21 |

All 14 games were valid. There were no illegal moves or errors.

## Heaviest Exact Root Events

These rows come from the new match JSONL `exact_root_events` payload. They are
single root solve timings from the targeted subset, not full-game timings.

| opening | game | ply | side | empties | legal cur/opp | elapsed ms | nodes | best | score | TT hits | TT stores | TT collisions | TT rejects | TT order used |
|---|---:|---:|---|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|
| `g27_p0_6_s3` | 12 | 45 | white | 15 | 10/5 | 661.437 | 1,716,640 | h2 | 16,000 | 44,399 | 285,593 | 963 | 53 | 16,210 |
| `g12_p0_5_s0` | 5 | 44 | black | 16 | 10/9 | 423.083 | 2,805,785 | a2 | 10,000 | 67,341 | 513,720 | 14,401 | 1,416 | 12,424 |
| `g12_p0_5_s0` | 4 | 45 | white | 15 | 10/8 | 349.066 | 2,321,925 | b1 | -6,000 | 65,746 | 390,472 | 3,707 | 387 | 21,093 |
| `g25_p0_6_s1` | 10 | 45 | white | 15 | 8/5 | 295.724 | 2,037,039 | g5 | 32,000 | 25,890 | 293,172 | 1,524 | 64 | 6,735 |
| `g27_p0_6_s3` | 13 | 44 | black | 16 | 6/10 | 267.485 | 1,567,030 | h2 | -14,000 | 30,391 | 240,901 | 649 | 33 | 5,517 |

## Root Candidate Breakdown

The benchmark fixtures `adaptive16-heavy-*` capture the board at the exact root
event, then solve each root candidate independently with root-breakdown.

| fixture | normal solve ms | normal nodes | root candidates | total candidate ms | total candidate nodes | worst candidate | worst ms | worst nodes | worst margin rank | worst is best |
|---|---:|---:|---:|---:|---:|---|---:|---:|---:|---|
| `adaptive16-heavy-g27-ply45` | 214.406 | 1,716,640 | 10 | 725.819 | 6,250,692 | d8 | 97.015 | 745,458 | 10 | no |
| `adaptive16-heavy-g12-ply44` | 327.408 | 2,805,785 | 10 | 2,464.570 | 20,857,696 | a2 | 430.369 | 3,508,419 | 1 | yes |
| `adaptive16-heavy-g12-ply45` | 292.485 | 2,321,925 | 10 | 579.628 | 4,874,878 | b7 | 97.874 | 954,226 | 10 | no |
| `adaptive16-heavy-g25-ply45` | 253.614 | 2,037,039 | 8 | 610.293 | 5,045,756 | g7 | 163.649 | 1,311,379 | 8 | no |
| `adaptive16-heavy-g27-ply44` | 192.908 | 1,567,030 | 6 | 1,656.740 | 14,170,789 | g7 | 445.205 | 3,897,313 | 4 | no |

## Expanded Worst Candidate

| fixture | expanded root | children | total child ms | total child nodes | worst child | worst ms | worst nodes | worst rank | worst is best |
|---|---|---:|---:|---:|---|---:|---:|---:|---|
| `adaptive16-heavy-g27-ply45` | d8 | 6 | 276.085 | 2,345,103 | g7 | 63.743 | 574,935 | 4 | no |
| `adaptive16-heavy-g12-ply44` | a2 | 10 | 765.680 | 5,776,218 | g7 | 139.106 | 1,024,361 | 7 | no |
| `adaptive16-heavy-g12-ply45` | b7 | 11 | 330.500 | 3,137,268 | g7 | 41.223 | 377,587 | 10 | no |
| `adaptive16-heavy-g25-ply45` | g7 | 9 | 473.262 | 3,825,346 | g3 | 73.625 | 609,100 | 2 | no |
| `adaptive16-heavy-g27-ply44` | g7 | 11 | 1,163.568 | 10,083,760 | h8 | 170.882 | 1,238,910 | 5 | no |

## Regression Coverage

Existing tests already cover exact root pass/PV behavior, deterministic
tie-breaks, selected root PVS regressions, TT stats exposure, TT best-move
ordering correctness, and pass positions with PVS/TT. This change adds a
match-runner regression that verifies an exact root event records the board,
ply, mobility, best move, depth, nodes, TT stats, and PV without changing the
search result.

`ctest` passed: 179/179 tests.

## Diagnosis

The heavy roots are not explained by `legal_moves_current` alone. The largest
events include 15-empty roots with 10 current moves and 16-empty roots with 6-10
current moves, but several have high opponent mobility or very uneven root
candidate costs. The root-breakdown rows show that the slowest candidate is
usually not the best candidate by margin, so root move ordering can still leave
expensive losing candidates near the front. `adaptive16-heavy-g12-ply44` is the
exception: the best candidate is also the slowest and has notably high TT
collisions/rejected stores, which points more toward TT pressure or deep
subtree shape than a simple ordering failure.

## Next Candidates

- Add a diagnostic-only root candidate cost sampler for adaptive16 roots, then
  use it to validate whether current move ordering repeatedly puts expensive
  non-best candidates early.
- Compare stricter adaptive gates using opponent mobility and edge-heavy or
  large-region metrics; `legal_moves_current <= 10` is not enough by itself.
- Revisit exact TT replacement/capacity for 16-empty roots with collision-heavy
  shapes such as `adaptive16-heavy-g12-ply44`.
- Inspect parity/region ordering on these fixtures specifically before changing
  the production weights again.

