# Phase-Aware Evaluation v1 Baseline

Status: historical baseline snapshot.

Recommendations in this snapshot describe follow-up suggested at the time of
collection. They are evidence, not current instructions, unless referenced by
the current user task, an active issue, or current project guidance.

## Environment

- Date: 2026-05-24
- Base commit SHA at collection: c4c7d00b18bbfd0309b804c038634ed9ab430e0d
- Measured tree: `codex/phase-aware-eval-v1` working tree with this PR's evaluation changes
- Machine: local arm64 macOS development machine
- OS: Darwin 24.6.0
- Compiler: Apple clang 17.0.0 (clang-1700.0.13.5)
- Build type: Release
- Exact endgame threshold for search benchmarks: 0

Machine note: hostnames and other personally identifying machine details are
intentionally omitted from this snapshot.

## Change Type

This is a strength-changing evaluation PR. Best moves, scores, PVs, result
checksums, and work checksums are expected to change from PR109.

The exact terminal evaluator score remains:

```text
score(board, side) * 1000
```

## Phase Definitions

| phase | occupied discs |
| :--- | :--- |
| opening | `occupied <= 20` |
| midgame | `21 <= occupied <= 44` |
| late | `occupied > 44` |

## Weights

Raw feature values are positive when good for the evaluated side.

| phase | disc diff | mobility | potential mobility | corner occupancy | corner access | X-square danger | frontier |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| opening | 0 | 8 | 4 | 35 | 30 | 25 | 3 |
| midgame | 1 | 10 | 5 | 40 | 35 | 30 | 4 |
| late | 4 | 6 | 2 | 45 | 20 | 20 | 2 |

## Commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```sh
./build/othello_search_bench \
  --mode both \
  --depths 1,2,3,4,5 \
  --positions smoke \
  --repetitions 1 \
  --exact-endgame-threshold 0
```

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --aspiration on \
  --exact-endgame-threshold 0
```

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 8,9 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --aspiration on \
  --exact-endgame-threshold 0
```

Analyze smoke:

```sh
printf '........\n........\n........\n...BW...\n...WB...\n........\n........\n........\nside=B\n' \
  | ./build/othello_analyze_position \
      --stdin \
      --depth 3 \
      --mode iterative \
      --tt on \
      --exact-endgame-threshold 0
```

Base/head match smoke used an `origin/main` PR109 worktree as `base` and this PR
working tree as `head`, both through `othello_nboard_engine`. The quick smoke
used depth 4; the deeper smoke used depth 10.
Commands are shown with generic paths:

```sh
git worktree add /tmp/vibe-othello-base-pr109 origin/main
cmake -S /tmp/vibe-othello-base-pr109 -B /tmp/vibe-othello-base-pr109/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/vibe-othello-base-pr109/build --target othello_nboard_engine
```

```sh
./build/othello_match_runner \
  --black external:head \
  --white external:base \
  --games 12 \
  --swap-sides true \
  --seed 20260524 \
  --openings data/openings/smoke_openings.txt \
  --engines /tmp/phase-aware-eval-engines.txt \
  --external-timeout-ms 5000 \
  --output /tmp/phase-aware-eval-match.jsonl \
  --quiet

python3 tools/scripts/match_summary.py \
  --input /tmp/phase-aware-eval-match.jsonl \
  --by-opening
```

Depth 10 used the same base/head binaries and openings with a depth-10 engine
config and a longer external timeout:

```sh
./build/othello_match_runner \
  --black external:head \
  --white external:base \
  --games 12 \
  --swap-sides true \
  --seed 20260524 \
  --openings data/openings/smoke_openings.txt \
  --engines /tmp/phase-aware-eval-engines-depth10.txt \
  --external-timeout-ms 30000 \
  --output /tmp/phase-aware-eval-depth10-match-12.jsonl \
  --quiet

python3 tools/scripts/match_summary.py \
  --input /tmp/phase-aware-eval-depth10-match-12.jsonl \
  --by-opening
```

## Smoke Search

| depth | mode | best move | score | PV | nodes | nodes/search | beta cut first move % | result checksum | work checksum |
| ---: | :--- | :--- | ---: | :--- | ---: | ---: | ---: | ---: | ---: |
| 1 | fixed | d3 | -41 | d3 | 21 | 2.625 | 0.000 | 953818823048080176 | 3443042836368878797 |
| 1 | iterative | d3 | -41 | d3 | 21 | 2.625 | 0.000 | 15452757659500766856 | 12557772029292612814 |
| 2 | fixed | d3 | -4 | d3->c3 | 56 | 7.000 | 33.333 | 12877614631083726236 | 17697704249932975150 |
| 2 | iterative | d3 | -4 | d3->c3 | 77 | 9.625 | 33.333 | 3384629319404721676 | 6866599955000880434 |
| 3 | fixed | d3 | -5 | d3->c5->d6 | 149 | 18.625 | 55.556 | 13706580388666562015 | 17308854809073620782 |
| 3 | iterative | d3 | -5 | d3->c5->d6 | 212 | 26.500 | 40.000 | 12206296820731450697 | 7083526387778493491 |
| 4 | fixed | d3 | 6 | d3->c5->d6->e3 | 498 | 62.250 | 57.143 | 13639593436593613291 | 6843253729350845295 |
| 4 | iterative | d3 | 6 | d3->c5->b6->d2 | 678 | 84.750 | 45.783 | 13921793844825332049 | 16037083763534186011 |
| 5 | fixed | d3 | -7 | d3->c5->e6->e3->d6 | 1432 | 179.000 | 55.762 | 2901503366677567014 | 5975795936820113455 |
| 5 | iterative | d3 | -7 | d3->c5->e6->e3->d6 | 1870 | 233.750 | 55.305 | 6627488798797056017 | 4582133191318701787 |

## Suite Iterative TT/PVS/Aspiration

| depth | best move | score | PV | nodes | nodes/search | tt hit % | PVS scouts | PVS researches | beta cut first move % | asp researches | fail lows | fail highs | fallbacks | result checksum | work checksum |
| ---: | :--- | ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 | d2 | 69 | d2->e3->f6->c1->c5 | 106800 | 1424.000 | 8.076 | 7362 | 237 | 66.688 | 54 | 24 | 30 | 3 | 17499861738422034076 | 6828764043449378898 |
| 6 | b2 | 69 | b2->a1->d6->e3->f7->a4 | 312966 | 4172.880 | 8.740 | 21180 | 696 | 71.431 | 63 | 30 | 33 | 3 | 3368769080471942308 | 17326071302250354353 |
| 7 | d6 | 57 | d6->a5->b2->f3->b6->b3->c5 | 1013643 | 13515.240 | 8.247 | 75372 | 1575 | 65.454 | 72 | 36 | 36 | 3 | 9619237167655969401 | 1119865490256326961 |
| 8 | b2 | 49 | b2->a1->c2->g4->h4->e3->d2->a4 | 2411928 | 32159.040 | 7.599 | 169089 | 2592 | 72.279 | 87 | 51 | 36 | 6 | 11601997289268489992 | 18313224902871572881 |
| 9 | d6 | 52 | d6->e3->f3->g2->d2->d3->g3->d7->h1 | 8448441 | 112645.880 | 7.271 | 645441 | 5016 | 67.672 | 105 | 69 | 36 | 9 | 14548137334654450729 | 11221330382706720756 |

## Analyze Breakdown Sample

Initial board, black to move:

```text
evaluation_breakdown:
  side: black
  phase: opening
  occupied_count: 4
  empty_count: 60
  terminal: no
  disc_difference: 0
  disc_difference_weight: 0
  disc_difference_score: 0
  mobility: 0
  mobility_weight: 8
  mobility_score: 0
  corner_occupancy: 0
  corner_occupancy_weight: 35
  corner_occupancy_score: 0
  potential_mobility: 0
  potential_mobility_weight: 4
  potential_mobility_score: 0
  corner_access: 0
  corner_access_weight: 30
  corner_access_score: 0
  x_square_danger: 0
  x_square_danger_weight: 25
  x_square_danger_score: 0
  frontier: 0
  frontier_weight: 3
  frontier_score: 0
  terminal_disc_difference: 0
  terminal_score_weight: 1000
  terminal_score: 0
  total: 0
```

## Base/Head Match Smoke

This is a small deterministic smoke comparison, not an Elo estimate.

| depth | head | base | games | head wins | base wins | draws | head win rate | avg disc diff from head perspective | avg time ms head | avg time ms base | errors |
| ---: | :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 4 | phase-aware eval v1 | PR109 basic eval | 12 | 8 | 4 | 0 | 66.67% | 5.33 | n/a | n/a | 0 |
| 10 | phase-aware eval v1 | PR109 basic eval | 12 | 6 | 6 | 0 | 50.00% | 5.00 | 3474.93 | 1944.46 | 0 |

Per opening at depth 4:

| opening | games | head wins | base wins | draws | avg diff |
| :--- | ---: | ---: | ---: | ---: | ---: |
| initial | 4 | 4 | 0 | 0 | 34.00 |
| c4-c3 | 4 | 2 | 2 | 0 | 1.00 |
| d3-c3-c4 | 4 | 2 | 2 | 0 | -19.00 |

Per opening at depth 10:

| opening | games | head wins | base wins | draws | avg diff |
| :--- | ---: | ---: | ---: | ---: | ---: |
| initial | 4 | 2 | 2 | 0 | 5.00 |
| c4-c3 | 4 | 2 | 2 | 0 | 5.00 |
| d3-c3-c4 | 4 | 2 | 2 | 0 | 5.00 |

## Notes

- Result checksums changed as expected for a strength-changing evaluation PR.
- The initial-board static breakdown remains 0 because all raw features are
  balanced; searched leaf scores and PVs still changed after moves.
- Depth 4 and depth 10 NBoard match smokes did not show illegal moves or
  process errors.
- The depth 10 smoke was even on wins over this tiny opening set, with a small
  positive average disc margin for head. It is still too small for a strength
  claim. A larger base/head match matrix was noted as a follow-up before tuning
  weights further.
