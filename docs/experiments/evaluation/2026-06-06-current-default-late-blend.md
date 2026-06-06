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

## Known Risks

- The exact-overlap high-confidence wrong count increased from 168 to 220.
- Teacher agreement was checked on a 500-row validation smoke because the
  existing mining script is intentionally simple and runs `analyze_position`
  serially.
- Deterministic self-play should be used before claiming broad strength.
