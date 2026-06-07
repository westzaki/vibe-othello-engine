# Pattern Exact-Best Rank Objective

Date: 2026-06-07

## Purpose

Use the PR318 50K `exact-aware-listwise` candidate as the gate baseline, then
test whether an additional exact-best rank pressure can protect exact-best move
ranking without claiming evaluator strength or promoting a default.

This report does not modify `current_default.eval`, does not add a retained
preset, and does not commit a generated eval table.

## Implementation

- Added `--exact-best-rank-penalty` for `exact-aware-listwise`.
- Added `--exact-best-margin` as the target model-score margin between the best
  exact-best candidate and the best non-exact candidate.
- Kept `pairwise-logistic` as the default objective.
- Reused the PR318 compact listwise feature cache; the cache stores child-board
  pattern features and is independent of the rank-penalty weight.
- Extended diagnostics:
  - `avg_exact_best_margin`
  - `avg_exact_best_rank_loss`
  - `wrong_direction_by_phase`
  - `exact_best_miss_high_confidence`

The existing exact move-score ordering penalty remains controlled by
`--sign-penalty`. The 50K comparison used `--sign-penalty 2` to strengthen that
ordering pressure while adding the new rank pressure.

## Commands

Commands below use a local dataset root placeholder to avoid embedding
machine-specific paths in repository history.

```sh
DATASET_ROOT=<dataset-root>

python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --teacher-labels runs/pattern-training/objective-smoke-inputs/teacher-50000.jsonl \
  --exact-labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --dataset-root "$DATASET_ROOT" \
  --eval-config data/eval/current_default.eval \
  --analyze-position build/othello_analyze_position \
  --out-dir runs/pattern-training/exact-best-rank-objective/rank-penalty-50k \
  --analysis-cache-dir runs/pattern-training/objective-smoke/analysis-cache \
  --analysis-cache-mode read-write \
  --analysis-runner batch \
  --analysis-jobs 4 \
  --listwise-feature-cache-dir runs/pattern-training/listwise-speed/feature-cache \
  --listwise-feature-cache-mode read-only \
  --families all \
  --split all \
  --objective exact-aware-listwise \
  --pair-mode best-vs-all \
  --pair-weighting score-margin \
  --max-pairs-per-position 8 \
  --loss logistic \
  --l2 1e-3 \
  --epochs 5 \
  --learning-rate 0.05 \
  --tie-penalty 0.25 \
  --target-top-group-size 1 \
  --exact-best-rank-penalty 2 \
  --exact-best-margin 2 \
  --sign-penalty 2 \
  --no-base-margin \
  --candidate-eval-shape pattern-only \
  --calibrate-output-scale \
  --scale-grid 1,2,4,8,16 \
  --seed 20260607
```

Gate commands:

```sh
build/othello_eval_vs_exact \
  --labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config runs/pattern-training/exact-best-rank-objective/rank-penalty-50k/candidate.eval \
  --top 10000 \
  --move-rank-analysis \
  --phase-breakdown \
  --output runs/pattern-training/exact-best-rank-objective/rank-penalty-50k/gate/eval-vs-exact.md

build/othello_search_bench \
  --mode fixed \
  --depths 5,6,7 \
  --positions evaluation \
  --repetitions 1 \
  --tt on \
  --pvs on \
  --exact-endgame-threshold 0 \
  --by-position \
  --eval-config runs/pattern-training/exact-best-rank-objective/rank-penalty-50k/candidate.eval \
  --format jsonl

build/othello_match_runner \
  --black search:depth=5,tt=on,pvs=on,exact=off,eval_config=runs/pattern-training/exact-best-rank-objective/rank-penalty-50k/candidate.eval \
  --white search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/current_default.eval \
  --games 40 \
  --swap-sides true \
  --seed 20260607 \
  --openings data/openings/smoke_openings.txt \
  --quiet
```

## 50K Baseline Gate

The baseline is the PR318 optimized 50K candidate:
`runs/pattern-training/listwise-speed/listwise-50k-cache-hit/candidate.eval`.

`eval_vs_exact --top 10000 --move-rank-analysis`:

| Metric | PR318 50K baseline |
| --- | ---: |
| exact-best top group | 30.190% |
| avg exact-best rank | 2.873 |
| exact sign agreement | 67.740% |
| wrong direction | 2,796 |
| high-confidence wrong direction | 0 |
| mean eval gap top minus exact-best | 11.930 |
| mean exact score gap exact-best minus top group | 8.902 |

Small deterministic match, PR318 candidate as player A vs `current_default`:

| Games | A wins | B wins | Draws | A win rate | Avg diff |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 40 | 0 | 40 | 0 | 0.00% | -42.75 |

## 10K Smoke

The 10K smoke compares PR318 optimized 10K to the new rank-pressure objective
with `--exact-best-rank-penalty 2 --exact-best-margin 2 --sign-penalty 2`.

| Metric | PR318 10K | Rank pressure 10K |
| --- | ---: | ---: |
| selected teacher agreement | 0.726071 | 0.711954 |
| top tie rate | 0.638767 | 0.628855 |
| exact-best top group | 0.996914 | 0.996914 |
| avg exact-best rank | 1.052469 | 1.015432 |
| exact sign agreement | 0.668790 | 0.655949 |
| wrong direction | 104 | 107 |
| high-confidence wrong direction | 0 | 1 |

The 10K smoke shows the rank pressure can improve trainer-side exact-best rank
and top tie rate, but it mildly hurts selected teacher agreement and exact sign
direction.

## 50K Trainer Metrics

The primary 50K comparison uses the same dataset, seed, families, eval config,
and PR318 cache infrastructure.

| Metric | PR318 50K | Rank pressure 50K |
| --- | ---: | ---: |
| selected teacher agreement | 0.486713 | 0.465065 |
| top tie rate | 0.727466 | 0.715310 |
| exact-best top group | 0.865918 | 0.856468 |
| avg exact-best rank | 1.622563 | 1.569403 |
| exact sign agreement | 0.696615 | 0.692355 |
| wrong direction | 493 | 499 |
| high-confidence wrong direction | 0 | 1 |
| avg exact-best margin | n/a | 0.939135 |
| avg exact-best rank loss | n/a | 1.436162 |

Trainer-side evidence is mixed: avg exact-best rank and top tie rate improve,
but selected agreement, exact-best top group, and sign direction regress.

Timing:

| Run | Label load | Exact load | Analysis | Feature construction | Listwise training | Examples/sec | Updates/sec |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| PR318 50K | 0.396s | 0.173s | 1.017s | 12.270s | 116.695s | 4069.705 | 33120.842 |
| rank pressure 50K | 0.463s | 0.182s | 1.199s | 19.258s | 151.149s | 2592.953 | 26976.317 |

The additional rank/order updates make SGD the dominant cost and reduce
updates/sec.

## 50K External Gate

`eval_vs_exact --top 10000 --move-rank-analysis`:

| Metric | PR318 50K baseline | Rank pressure 50K |
| --- | ---: | ---: |
| exact-best top group | 30.190% | 30.910% |
| avg exact-best rank | 2.873 | 2.899 |
| exact sign agreement | 67.740% | 66.650% |
| wrong direction | 2,796 | 2,839 |
| high-confidence wrong direction | 0 | 0 |
| mean eval gap top minus exact-best | 11.930 | 7.939 |
| mean exact score gap exact-best minus top group | 8.902 | 8.869 |

The external gate improves exact-best top-group count and reduces the evaluator
score gap to missed exact-best moves, but it does not improve avg exact-best
rank or wrong-direction. This is negative evidence for the current penalty as a
runtime correctness/quality fix.

## Search Smoke

`search_bench` used depths 5, 6, and 7 over the built-in evaluation positions.

| Candidate | Depth | Rows | Nodes | Time ms | Nodes/sec |
| --- | ---: | ---: | ---: | ---: | ---: |
| current_default | 5 | 8 | 12,830 | 56.520 | 226,999 |
| current_default | 6 | 8 | 27,373 | 99.821 | 274,221 |
| current_default | 7 | 8 | 129,949 | 399.374 | 325,382 |
| PR318 50K | 5 | 8 | 14,645 | 75.900 | 192,951 |
| PR318 50K | 6 | 8 | 45,974 | 196.464 | 234,008 |
| PR318 50K | 7 | 8 | 117,755 | 528.087 | 222,984 |
| rank pressure 50K | 5 | 8 | 13,055 | 76.191 | 171,347 |
| rank pressure 50K | 6 | 8 | 42,755 | 195.132 | 219,108 |
| rank pressure 50K | 7 | 8 | 107,547 | 457.799 | 234,922 |

The rank-pressure candidate changes search checksums relative to PR318 and
`current_default`. This is expected for a changed evaluator and is not a
strength claim.

## Match Smoke

Depth-5 deterministic match, candidate as player A vs `current_default` as
player B:

| Candidate | Games | A wins | B wins | Draws | A win rate | Avg diff | Avg plies |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| PR318 50K | 40 | 0 | 40 | 0 | 0.00% | -42.75 | 58.40 |
| rank pressure 50K | 40 | 0 | 40 | 0 | 0.00% | -34.35 | 58.40 |

The smaller loss margin is not treated as strength evidence; both candidates
lose all 40 smoke games.

## Stronger Penalty Check

A stronger run,
`--exact-best-rank-penalty 4 --exact-best-margin 4 --sign-penalty 2`, was also
trained at 50K to check whether more rank pressure fixes the external gate.

Trainer-side quantized metrics:

| Metric | penalty 2 margin 2 | penalty 4 margin 4 |
| --- | ---: | ---: |
| selected teacher agreement | 0.465065 | 0.437389 |
| top tie rate | 0.715310 | 0.640272 |
| avg exact-best rank | 1.569403 | 1.588305 |
| exact sign agreement | 0.692355 | 0.683855 |
| wrong direction | 499 | 515 |
| high-confidence wrong direction | 1 | 72 |

External `eval_vs_exact --top 10000` for the stronger run:

| Metric | penalty 4 margin 4 |
| --- | ---: |
| exact-best top group | 28.910% |
| avg exact-best rank | 3.012 |
| exact sign agreement | 66.320% |
| wrong direction | 2,942 |
| high-confidence wrong direction | 0 |

Increasing the rank margin/weight worsened both trainer-side sign diagnostics
and external exact-best rank. The current penalty is not simply underweighted.

## Interpretation

The rank pressure is effective inside the compact trainer representation: it
can improve avg exact-best rank and reduce top tie rate on 10K and 50K
trainer-side diagnostics.

It is not sufficient as a runtime exact-overlap fix. After rendering,
quantization, scale calibration, and C++ runtime evaluation, the primary 50K
candidate improves only exact-best top-group rate while avg exact-best rank and
wrong-direction regress. Stronger rank pressure makes this worse.

The remaining issue appears to be objective/output-shape mismatch rather than a
missing rank term alone. Next candidates should either use a softer exact-score
target distribution over all legal moves, calibrate directly against the
external exact-overlap gate, or add a smaller anchor that protects sign direction
without widening bad high-confidence moves.

Do not promote either generated candidate.

## Verification

```sh
python3 -m unittest tools/scripts/tests/test_regularized_pairwise_pattern_train.py
```

CTest was also run for this PR after the implementation changes.
