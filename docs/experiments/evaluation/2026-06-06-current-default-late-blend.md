# Current Default Late Light Blend

This report records a small `current_default.eval` late-phase scalar adjustment
intended to reduce the known exact-best rank regression and search overhead from
the NTest pairwise full-v2 default promotion while preserving the learned
phase-specific pattern tables.

The exact solver, search modes, pattern tables, and built-in file-free fallback
were not changed.

## Hypothesis

The learned full-v2 pattern tables improved NTest teacher agreement but made
late exact-best rank and search overhead worse. A light late scalar blend toward
the historical teacher-aggressive probe should keep the useful teacher bias
while restoring better exact-best root ordering and keeping depth-6/7 search
overhead within +10%.

Only late scalar weights changed:

| feature | before | after |
|---|---:|---:|
| disc_difference | 4 | 3 |
| mobility | 6 | 8 |
| potential_mobility | 2 | 4 |
| corner_occupancy | 45 | 56 |
| corner_access | 20 | 30 |
| x_square_danger | 20 | 23 |
| frontier | 3 | 10 |
| corner_local_2x3 | 0 | 4 |
| edge_stability_lite | 8 | 15 |

## Candidate Matrix

Command:

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build/release \
  --labels /Users/mnishizaki/Project/vibe-othello-datasets/teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --baseline-config data/eval/current_default.eval \
  --candidates runs/eval-tuning/late_aggressive_light.eval \
  --out runs/eval-tuning/matrix-blends \
  --search-depths 5,6,7 \
  --positions evaluation \
  --repetitions 1 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis
```

| metric | current_default | late blend |
|---|---:|---:|
| exact sign agreement | 7641 / 10000 | 8005 / 10000 |
| wrong direction | 2006 | 1650 |
| high-confidence wrong | 168 | 220 |
| exact-best top group | 5121 | 5582 |
| exact-best rank sum | 19790 | 18434 |
| avg exact-best rank | 1.979 | 1.843 |
| search nodes | 212240 | 214792 |
| search node delta | baseline | +1.20% |
| search elapsed ms | 88.028 | 85.071 |
| search elapsed delta | baseline | -3.36% |

The exact-best rank regression and search overhead both improved on this
matrix. The increased high-confidence wrong count is the main new risk.

Depth-specific node overhead from the same matrix:

| depth | baseline nodes | late blend nodes | delta |
|---:|---:|---:|---:|
| 5 | 16938 | 18290 | +7.98% |
| 6 | 42433 | 45627 | +7.53% |
| 7 | 152869 | 150875 | -1.30% |

## NTest Teacher Smoke

Command:

```sh
python3 tools/scripts/teacher_label_mistake_mining.py \
  --teacher-labels runs/eval-tuning/validation-3000.jsonl \
  --exact-labels /Users/mnishizaki/Project/vibe-othello-datasets/teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --config current_default=data/eval/current_default.eval \
  --config late_aggressive_light=runs/eval-tuning/late_aggressive_light.eval \
  --out runs/eval-tuning/teacher-mine-500-blends \
  --build-dir build/release \
  --depth 1 \
  --exact-endgame-threshold 0 \
  --limit 500
```

| metric | current_default | late blend |
|---|---:|---:|
| validation sample rows | 500 | 500 |
| teacher agreements | 227 | 236 |
| teacher rank sum | 1229 | 1185 |
| exact rows in sample | 16 | 16 |
| exact agreements in sample | 5 | 4 |
| exact-best rank sum in sample | 48 | 42 |
| late_disc_count_greed mismatches | 94 | 86 |

This smoke sample improved teacher agreement and teacher rank while reducing
late disc-count-greed mismatches.

The same rows had 18 recovered teacher top-1 choices and 9 regressions. Most
recoveries were late disc-count-greed cases where the older default selected a
greedy edge or corner move and the adjusted default selected the teacher move.

## Source-Controlled Final Check

After applying the light blend to `data/eval/current_default.eval`, the final
candidate matrix compared it against `data/eval/ntest_pairwise_full_v2.eval`,
which is equivalent to the previous `current_default` scalar/table shape.

Command:

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build/release \
  --labels /Users/mnishizaki/Project/vibe-othello-datasets/teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --baseline-config data/eval/ntest_pairwise_full_v2.eval \
  --candidates data/eval/current_default.eval \
  --out runs/eval-tuning/matrix-final-light \
  --search-depths 5,6,7 \
  --positions evaluation \
  --repetitions 1 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis
```

| metric | previous default shape | adjusted current_default |
|---|---:|---:|
| exact sign agreement | 7641 / 10000 | 8005 / 10000 |
| wrong direction | 2006 | 1650 |
| high-confidence wrong | 168 | 220 |
| exact-best top group | 5121 | 5582 |
| exact-best rank sum | 19790 | 18434 |
| avg exact-best rank | 1.979 | 1.843 |
| aggregate search nodes | 212240 | 214792 |
| aggregate search node delta | baseline | +1.20% |
| aggregate search elapsed ms | 70.184 | 76.095 |
| aggregate search elapsed delta | baseline | +8.42% |

Depth-specific final search overhead:

| depth | baseline nodes | adjusted nodes | node delta | baseline ms | adjusted ms | elapsed delta |
|---:|---:|---:|---:|---:|---:|---:|
| 5 | 16938 | 18290 | +7.98% | 13.520 | 17.368 | +28.46% |
| 6 | 42433 | 45627 | +7.53% | 17.564 | 19.576 | +11.46% |
| 7 | 152869 | 150875 | -1.30% | 39.100 | 39.151 | +0.13% |

Depth-6 node overhead is within +10%; elapsed exceeded +10% slightly on this
short bench, so deterministic self-play was collected as supporting evidence.

## Deterministic Self-Play

Command:

```sh
build/release/othello_match_runner \
  --black search:depth=6,eval_config=data/eval/current_default.eval \
  --white search:depth=6,eval_config=data/eval/ntest_pairwise_full_v2.eval \
  --games 200 \
  --swap-sides true \
  --seed 20260606 \
  --openings data/openings/eval_regression_openings.txt \
  --output runs/eval-tuning/current-default-light-vs-ntest-pairwise-depth6-200.jsonl \
  --format jsonl \
  --quiet
```

| metric | result |
|---|---:|
| games | 200 |
| adjusted current_default wins | 114 |
| previous default-shape wins | 86 |
| draws | 0 |
| adjusted win rate | 57.00% |
| avg disc diff from adjusted side | +4.76 |
| errors | 0 |
| avg nodes adjusted | 401716.32 |
| avg nodes previous | 385035.97 |
| avg ms adjusted | 87.62 |
| avg ms previous | 86.03 |

## High-Confidence Wrong Regression Audit

The final exact-overlap check improved total wrong direction count
(`2006 -> 1650`) and exact-best rank, but high-confidence wrong direction
count increased (`168 -> 220`). To audit that risk, both configs were rerun
through the existing `othello_eval_vs_exact` report with `--top 10000` and
`--high-confidence-threshold 250`, then the high-confidence disagreement
position sets were compared by `position_id`.

Commands:

```sh
build/release/othello_eval_vs_exact \
  --labels /Users/mnishizaki/Project/vibe-othello-datasets/teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --output runs/eval-tuning/high-confidence-audit-baseline.md \
  --eval-config data/eval/ntest_pairwise_full_v2.eval \
  --top 10000 \
  --high-confidence-threshold 250 \
  --move-rank-analysis

build/release/othello_eval_vs_exact \
  --labels /Users/mnishizaki/Project/vibe-othello-datasets/teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --output runs/eval-tuning/high-confidence-audit-current.md \
  --eval-config data/eval/current_default.eval \
  --top 10000 \
  --high-confidence-threshold 250 \
  --move-rank-analysis
```

| set comparison | positions |
|---|---:|
| previous high-confidence wrong | 168 |
| adjusted high-confidence wrong | 220 |
| still high-confidence wrong in both | 141 |
| recovered below threshold | 27 |
| newly crossed into high-confidence wrong | 79 |
| net increase | +52 |

All 79 newly high-confidence wrong rows were already wrong-direction under the
previous default shape; they crossed the `abs(eval_score) >= 250` threshold
after the late scalar adjustment. The 27 recovered rows likewise remained
wrong-direction but moved below the high-confidence threshold. This means the
regression is mostly confidence calibration around existing late mistakes, not
a new class of pass-root failures.

### Empties Bucket

| set | 7-8 empties | 9-10 empties | 11-12 empties |
|---|---:|---:|---:|
| recovered below threshold | 10 | 5 | 12 |
| newly high-confidence wrong | 31 | 22 | 26 |
| persistent high-confidence wrong | 49 | 44 | 48 |

### Move-Family / Tag Breakdown

The table uses exact `best_moves` from the labels, not the evaluator-selected
move. `legal corner` and `legal x-square` count whether that move family was
available at the root.

| set | best move family | count |
|---|---|---:|
| recovered | corner | 8 |
| recovered | edge | 8 |
| recovered | interior | 6 |
| recovered | corner+edge | 3 |
| recovered | x-square | 1 |
| recovered | pass | 1 |
| newly wrong | edge | 34 |
| newly wrong | corner | 24 |
| newly wrong | interior | 9 |
| newly wrong | corner+edge | 5 |
| newly wrong | edge+x-square | 3 |
| newly wrong | edge+interior | 2 |
| newly wrong | corner+edge+interior | 1 |
| newly wrong | x-square | 1 |

| set | legal corner roots | legal x-square roots | pass roots |
|---|---:|---:|---:|
| recovered | 16 | 11 | 1 |
| newly wrong | 46 | 30 | 0 |
| persistent | 86 | 44 | 0 |

The newly high-confidence wrong rows are mostly late edge/corner roots. There
were no newly high-confidence pass roots. X-squares were available in 30 of 79
new rows, but only one new row had an exact-best x-square family, so this does
not currently look like a primary X-square accident. The dominant component
tags in new rows were corner/edge features (`corner+edge`: 41, `frontier+edge`:
10, `corner+frontier+edge`: 9). That is consistent with the intended
late_disc_count_greed correction: the stronger corner/edge/frontier late scalar
blend reduces greedy disc-count choices, but it can overstate some already
wrong edge/corner positions.

### Representative Rows

| set | position | empties | exact score | old eval | new eval | exact best | legal family note |
|---|---|---:|---:|---:|---:|---|---|
| recovered | pos-001545 | 12 | 16 | -307 | -210 | H5 D8 | edge best; x-square available |
| recovered | pos-007045 | 8 | 12 | -299 | -221 | F7 | interior best; no corner/X root |
| recovered | pos-005227 | 11 | 6 | -299 | -227 | H3 | edge best; x-square available |
| recovered | pos-006742 | 10 | -20 | 276 | 244 | PASS | pass root moved below threshold |
| recovered | pos-006232 | 12 | 8 | -292 | -238 | B1 H1 B8 | corner/edge best; corner available |
| newly wrong | pos-006190 | 7 | 10 | -249 | -384 | F8 | edge best; corner available |
| newly wrong | pos-008876 | 9 | 12 | -216 | -363 | A8 | corner best; corner available |
| newly wrong | pos-007116 | 11 | 8 | -200 | -337 | H8 | corner best; corner and x-square available |
| newly wrong | pos-001238 | 12 | 2 | -226 | -332 | A8 | corner best; x-square available |
| newly wrong | pos-000645 | 8 | -8 | 223 | 324 | C6 C8 | edge/interior best; x-square available |

## Strong-v1 Suite Smoke

Because `current_default.eval` is also the normal evaluator behind the
`strong-v1` practical play preset, one suite smoke was run with strong-v1-like
search options: iterative search, TT on, PVS on, score-delta-aware aspiration,
and adaptive16 exact root. This is a smoke check, not a speed claim.

Commands:

```sh
build/release/othello_search_bench \
  --mode iterative \
  --depths 4 \
  --positions suite \
  --by-position \
  --tt on \
  --pvs on \
  --aspiration on \
  --aspiration-profile score-delta-aware \
  --exact-endgame-threshold adaptive16 \
  --eval-config data/eval/ntest_pairwise_full_v2.eval \
  --repetitions 1 \
  --format jsonl \
  > runs/eval-tuning/strong-v1-suite-smoke-baseline.jsonl

build/release/othello_search_bench \
  --mode iterative \
  --depths 4 \
  --positions suite \
  --by-position \
  --tt on \
  --pvs on \
  --aspiration on \
  --aspiration-profile score-delta-aware \
  --exact-endgame-threshold adaptive16 \
  --eval-config data/eval/current_default.eval \
  --repetitions 1 \
  --format jsonl \
  > runs/eval-tuning/strong-v1-suite-smoke-current.jsonl
```

| metric | previous default shape | adjusted current_default |
|---|---:|---:|
| positions | 25 | 25 |
| exact-root positions | 4 | 4 |
| aggregate nodes | 215217 | 214972 |
| node delta | baseline | -0.11% |
| aggregate elapsed ms | 51.458 | 59.412 |
| elapsed delta | baseline | +15.46% |
| aspiration searches | 63 | 63 |
| aspiration researches | 50 | 49 |
| aspiration fallbacks | 0 | 0 |
| PVS researches | 44 | 36 |
| positions with changed best move | baseline | 1 |

Changed late-position rows:

| position | old best | new best | old score | new score | old nodes | new nodes |
|---|---|---|---:|---:|---:|---:|
| late-corner-swing | h5 | a5 | 30 | 82 | 767 | 671 |
| late-edge-heavy | a6 | a6 | -186 | -123 | 469 | 441 |
| late-open-corner | h1 | h1 | 402 | 593 | 700 | 605 |
| late-wide-mobility | h8 | h8 | 364 | 619 | 644 | 618 |

The only best-move change in this suite was `late-corner-swing`; all exact-root
rows stayed exact and therefore unchanged by evaluation. Nodes were essentially
flat, while elapsed time was noisier on this single short smoke.

## Known Risks

- The exact-overlap high-confidence wrong count increased from 168 to 220.
- Teacher agreement was checked on a 500-row validation smoke because the
  existing mining script is intentionally simple and runs `analyze_position`
  serially.
- Deterministic self-play should be used before claiming broad strength.
