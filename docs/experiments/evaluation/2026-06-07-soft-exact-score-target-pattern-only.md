# Soft Exact-Score Target PatternOnly

This report records a PatternOnly `exact-aware-listwise` experiment with a soft
exact-score target over legal root moves. It is trainer-foundation evidence
only, not a strength claim, not an Elo estimate, and not a default-promotion
recommendation.

`data/eval/current_default.eval` was not changed. No retained preset or
committed eval table was added.

## Hypothesis

Earlier PatternOnly candidates could improve trainer-local metrics while
collapsing on external exact-overlap and match checks. A soft target shaped by
exact root move scores should give PatternOnly a cleaner objective than teacher
one-hot or hard exact-best margin pressure: exact-best moves receive the most
probability, near-best moves remain non-zero, and clearly bad moves sit near a
small floor.

## Implementation

`tools/scripts/regularized_pairwise_pattern_train.py` gained optional
`exact-aware-listwise` controls:

```sh
--exact-score-soft-target
--exact-score-temperature
--exact-score-target-floor
--exact-score-near-best-window
```

Rows with complete exact `move_scores` use a normalized exact-score target
distribution over all legal moves. Rows without complete exact `move_scores`
fall back to the teacher move. The final candidate shape used
`--candidate-eval-shape pattern-only --no-base-margin`. Pairwise-logistic
defaults were not changed, and no `current_default` distillation was added.

New trainer diagnostics include:

- `soft_target_cross_entropy`
- `selected_exact_score_regret`
- `top_move_exact_score_gap`
- `wrong_direction_by_phase`

The existing listwise diagnostics still report `avg_exact_best_rank`,
`exact_best_top_group_rate`, `exact_sign_agreement`, `wrong_direction`, and
`high_confidence_wrong_direction`.

## 10K Smoke

Command shape:

```sh
python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --dataset-root /path/to/vibe-othello-datasets \
  --teacher-labels dataset:teacher/ntest-balanced300k-v0/labels/ntest12-local/shards/labels-0000.jsonl,...,dataset:teacher/ntest-balanced300k-v0/labels/ntest12-local/shards/labels-0009.jsonl \
  --exact-labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config data/eval/current_default.eval \
  --analyze-position build/release/othello_analyze_position \
  --out-dir runs/pattern-training/ntest-balanced300k-v0/soft_exact_score_target_10k \
  --analysis-cache-dir runs/pattern-training/ntest-balanced300k-v0/analysis-cache \
  --analysis-cache-mode read-write \
  --analysis-runner batch \
  --analysis-jobs 8 \
  --families all \
  --split train \
  --objective exact-aware-listwise \
  --exact-score-soft-target \
  --exact-score-temperature 4 \
  --exact-score-target-floor 0.0001 \
  --exact-score-near-best-window 8 \
  --pair-mode best-vs-all \
  --pair-weighting score-margin \
  --max-pairs-per-position 8 \
  --loss logistic \
  --l2 1e-3 \
  --epochs 5 \
  --learning-rate 0.05 \
  --no-base-margin \
  --candidate-eval-shape pattern-only \
  --seed 20260607
```

10K completed and wrote
`runs/pattern-training/ntest-balanced300k-v0/soft_exact_score_target_10k/candidate.eval`.

| metric | final float | quantized final |
| --- | ---: | ---: |
| rows | 7031 | 7031 |
| soft target rows | 206 | 206 |
| soft target cross entropy | 0.945 | 1.692 |
| selected exact-score regret | 0.363 | 6.769 |
| exact-best top-group rate | 99.15% | 90.17% |
| avg exact-best rank | 1.158 | 1.534 |
| exact sign agreement | 158 / 228 | 133 / 174 |
| wrong direction | 70 | 41 |
| high-confidence wrong direction | 0 | 0 |

The float model learns the soft target shape on the training slice, but
quantization weakens that signal.

## 50K Run

The 50K command used the same options and shards `labels-0000.jsonl` through
`labels-0049.jsonl`, writing:

- `runs/pattern-training/ntest-balanced300k-v0/soft_exact_score_target_50k/candidate.eval`
- `runs/pattern-training/ntest-balanced300k-v0/soft_exact_score_target_50k/report.md`
- `runs/pattern-training/ntest-balanced300k-v0/soft_exact_score_target_50k/summary.json`

Analysis cache behavior:

| hits | misses | writes | analysis elapsed |
| ---: | ---: | ---: | ---: |
| 7036 | 27831 | 27831 | 117.521 s |

50K trainer diagnostics:

| metric | final float | quantized final |
| --- | ---: | ---: |
| rows | 34833 | 34833 |
| soft target rows | 1029 | 1029 |
| soft target cross entropy | 1.277 | 1.695 |
| selected exact-score regret | 2.382 | 6.325 |
| exact-best top-group rate | 90.07% | 92.11% |
| avg exact-best rank | 1.576 | 1.561 |
| exact sign agreement | 806 / 1142 | 686 / 929 |
| wrong direction | 336 | 243 |
| wrong direction by phase | late: 336 | late: 243 |
| high-confidence wrong direction | 0 | 0 |

## External Exact Overlap

Command:

```sh
build/release/othello_eval_vs_exact \
  --labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config runs/pattern-training/ntest-balanced300k-v0/soft_exact_score_target_50k/candidate.eval \
  --top 10000 \
  --move-rank-analysis \
  --phase-breakdown \
  --output runs/eval/soft_exact_score_target_50k_exact.md
```

| metric | PR318 PatternOnly 50K baseline | soft exact-score target 50K |
| --- | ---: | ---: |
| analyzed | 10000 | 10000 |
| sign agreements | 5138 | 4532 |
| wrong direction | 2400 | 2031 |
| high-confidence wrong direction | 0 | 0 |
| exact-best top group | 6017 | 6898 |
| exact-best rank sum | 20119 | 18351 |
| avg exact-best rank | 2.012 | 1.835 |

The completion gate is satisfied: external exact-overlap improves both
`avg exact-best rank` and `wrong direction` versus the PR318 PatternOnly 50K
baseline. The sign-agreement regression is negative evidence.

Phase breakdown for the 10K exact-overlap sample:

| phase | count | sign agreement | wrong direction |
| --- | ---: | ---: | ---: |
| late | 10000 | 45.320% | 2031 |

Move-rank details:

| metric | value |
| --- | ---: |
| evaluator top-score group exact-best rate | 68.980% |
| mean exact-best move rank under evaluator | 1.835 |
| mean evaluator score gap top minus exact-best | 0.363 heuristic units |
| mean exact score gap exact-best minus top group | 3.768 discs |

## Search Bench

Command:

```sh
build/release/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions evaluation \
  --repetitions 1 \
  --tt on \
  --pvs on \
  --exact-endgame-threshold 0 \
  --eval-config runs/pattern-training/ntest-balanced300k-v0/soft_exact_score_target_50k/candidate.eval \
  --format jsonl
```

| depth | nodes | elapsed ms | eval calls | best move | result checksum |
| ---: | ---: | ---: | ---: | --- | --- |
| 5 | 12533 | 14.042 | 11613 | d6 | 16363230256699643497 |
| 6 | 33254 | 18.542 | 31175 | b2 | 11097321088094851550 |
| 7 | 97256 | 34.026 | 91045 | d6 | 16215745139314871539 |

The candidate loads and searches successfully. This is runtime evidence only.

## Deterministic Match

Command:

```sh
build/release/othello_match_runner \
  --black search:depth=6,eval_config=runs/pattern-training/ntest-balanced300k-v0/soft_exact_score_target_50k/candidate.eval \
  --white search:depth=6,eval_config=data/eval/current_default.eval \
  --games 40 \
  --swap-sides true \
  --seed 20260607 \
  --openings data/openings/eval_regression_openings.txt \
  --output runs/matches/soft-exact-score-target-50k-vs-current-default-depth6-40.jsonl \
  --format jsonl \
  --quiet
```

| metric | value |
| --- | ---: |
| games | 40 |
| PatternOnly wins | 0 |
| PatternOnly losses | 40 |
| draws | 0 |
| errors | 0 |
| avg disc diff from PatternOnly | -45.40 |
| avg nodes PatternOnly | 201752.10 |
| avg nodes current_default | 395552.47 |
| avg ms PatternOnly | 56.21 |
| avg ms current_default | 88.02 |

The deterministic match is strongly negative. This candidate should not be
promoted or described as stronger.

## Decision

Soft exact-score targets are useful as a cleaner PatternOnly objective
foundation: they improved external exact-overlap rank and wrong-direction
metrics versus the PR318 PatternOnly 50K baseline. They did not produce a
playable standalone PatternOnly evaluator. Keep this as experimental objective
evidence and do not add a retained preset or committed eval table.
