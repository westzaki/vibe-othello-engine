# Soft Exact-Score Sign Anchor PatternOnly

This report records a PatternOnly `exact-aware-listwise` follow-up to the PR323
soft exact-score target objective. It is trainer-foundation evidence only, not a
strength claim, not an Elo estimate, and not a default-promotion recommendation.

`data/eval/current_default.eval` was not changed. No retained preset or
committed eval table was added. Generated candidates and measurement artifacts
remain under `runs/`.

## Hypothesis

PR323 improved external exact-overlap move-rank behavior, but exact sign
agreement regressed and deterministic match remained 0-40 against
`current_default`. A small auxiliary sign anchor might preserve the soft exact
score-shape objective while making PatternOnly less willing to score exact wins
as losses, or exact losses as wins.

The runtime candidate remains scalar-free:

- `--candidate-eval-shape pattern-only`
- `--no-base-margin`
- `--exact-score-soft-target`
- no `current_default` distillation
- no base-plus-delta candidate
- no hard exact-best rank pressure

## Implementation

`tools/scripts/regularized_pairwise_pattern_train.py` gained optional sign
anchor controls:

```sh
--soft-sign-anchor-weight
--soft-sign-anchor-margin
--soft-sign-anchor-mode off|selected|expected
```

Default behavior remains `off`. When enabled, the anchor is only available for
`--objective exact-aware-listwise --exact-score-soft-target` and rows with
non-zero `exact_root_score`.

`selected` anchors the current selected/top candidate score with a small hinge:

```text
max(0, margin - sign(exact_root_score) * selected_score)
```

`expected` anchors the softmax-probability-weighted expected score. Both modes
leave the soft target cross entropy as the primary objective.

Added diagnostics:

- `sign_anchor_rows`
- `sign_anchor_updates`
- `sign_anchor_loss`

Existing diagnostics are retained:

- `exact_sign_agreement`
- `wrong_direction`
- `wrong_direction_by_phase`
- `high_confidence_wrong_direction`
- `avg_exact_best_rank`
- `exact_best_top_group_rate`
- `selected_exact_score_regret`
- `top_move_exact_score_gap`

## 10K Smoke

All 10K runs used shards `labels-0000.jsonl` through `labels-0009.jsonl`, the
PR323 seed/options, `families=all`, and the shared analysis cache.

| run | mode | weight | margin | quantized exact sign agreement | quantized wrong direction | quantized avg exact-best rank | quantized exact-best top-group rate | quantized selected exact-score regret |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| PR323 baseline | off | 0 | 1 | 76.44% | 41 | 1.534 | 90.17% | 6.769 |
| weak | selected | 0.05 | 1 | 78.03% | 38 | 1.538 | 91.45% | 6.701 |
| medium | selected | 0.10 | 1 | 78.95% | 36 | 1.530 | 91.45% | 6.667 |
| medium | expected | 0.10 | 2 | 77.84% | 39 | 1.543 | 90.60% | 6.667 |

The selected `0.10 / margin 1` setting was chosen for 50K because it gave the
best 10K quantized wrong-direction and exact sign agreement result while
preserving exact-best rank/top-group behavior.

## 50K Run

Command shape:

```sh
python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --dataset-root /path/to/vibe-othello-datasets \
  --teacher-labels dataset:teacher/ntest-balanced300k-v0/labels/ntest12-local/shards/labels-0000.jsonl,...,dataset:teacher/ntest-balanced300k-v0/labels/ntest12-local/shards/labels-0049.jsonl \
  --exact-labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config data/eval/current_default.eval \
  --analyze-position build/release/othello_analyze_position \
  --out-dir runs/pattern-training/ntest-balanced300k-v0/sign_anchor_50k_selected_w01_m1 \
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
  --soft-sign-anchor-mode selected \
  --soft-sign-anchor-weight 0.1 \
  --soft-sign-anchor-margin 1 \
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

Output:

- `runs/pattern-training/ntest-balanced300k-v0/sign_anchor_50k_selected_w01_m1/candidate.eval`
- `runs/pattern-training/ntest-balanced300k-v0/sign_anchor_50k_selected_w01_m1/report.md`
- `runs/pattern-training/ntest-balanced300k-v0/sign_anchor_50k_selected_w01_m1/summary.json`

Analysis cache behavior:

| hits | misses | writes | analysis elapsed |
| ---: | ---: | ---: | ---: |
| 34867 | 0 | 0 | 0.502 s |

50K trainer diagnostics:

| metric | PR323 quantized final | sign anchor quantized final |
| --- | ---: | ---: |
| soft target cross entropy | 1.695 | 1.683 |
| avg exact-best rank | 1.561 | 1.525 |
| exact-best top-group rate | 92.11% | 93.46% |
| exact sign agreement rate | 73.84% | 77.87% |
| wrong direction | 243 | 195 |
| high-confidence wrong direction | 0 | 0 |
| selected exact-score regret | 6.325 | 6.340 |
| top move exact-score gap | 6.325 | 6.340 |
| sign anchor rows | n/a | 456 |
| sign anchor updates | n/a | 456 |
| sign anchor loss | n/a | 1.632 |

The trainer-local quantized diagnostics improved sign direction and did not
damage exact-best rank/top-group metrics.

## External Exact Overlap

Command:

```sh
build/release/othello_eval_vs_exact \
  --labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config runs/pattern-training/ntest-balanced300k-v0/sign_anchor_50k_selected_w01_m1/candidate.eval \
  --top 10000 \
  --move-rank-analysis \
  --phase-breakdown \
  --output runs/eval/soft_exact_score_sign_anchor_50k_exact.md
```

| metric | PR323 baseline | sign anchor 50K |
| --- | ---: | ---: |
| analyzed | 10000 | 10000 |
| exact sign agreement | 4532 | 4725 |
| wrong direction | 2031 | 1741 |
| high-confidence wrong direction | 0 | 0 |
| exact-best top group | 6898 | 6709 |
| exact-best top-group rate | 68.98% | 67.09% |
| exact-best rank sum | 18351 | 18836 |
| avg exact-best rank | 1.835 | 1.884 |

The completion condition is satisfied: external exact sign agreement improved
and wrong direction decreased. PR323's move-rank improvements were partially
preserved but slightly degraded: avg exact-best rank worsened by 0.049, and
exact-best top group dropped by 189 / 10000.

Phase breakdown:

| phase | count | sign agreement | wrong direction |
| --- | ---: | ---: | ---: |
| late | 10000 | 47.250% | 1741 |

Move-rank details:

| metric | value |
| --- | ---: |
| evaluator top-score group exact-best rate | 67.090% |
| mean exact-best move rank under evaluator | 1.884 |
| mean exact score gap exact-best minus top group | 3.927 discs |

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
  --eval-config runs/pattern-training/ntest-balanced300k-v0/sign_anchor_50k_selected_w01_m1/candidate.eval \
  --format jsonl
```

| depth | nodes | elapsed ms | eval calls | best move | result checksum |
| ---: | ---: | ---: | ---: | --- | --- |
| 5 | 12495 | 14.012 | 11564 | d6 | 16363230256699643497 |
| 6 | 32163 | 17.752 | 30175 | b2 | 1873949051240075749 |
| 7 | 96340 | 35.269 | 90092 | d6 | 16215182189361450227 |

The candidate loads and searches successfully. This is runtime evidence only.

## Deterministic Match

Command:

```sh
build/release/othello_match_runner \
  --black search:depth=6,eval_config=runs/pattern-training/ntest-balanced300k-v0/sign_anchor_50k_selected_w01_m1/candidate.eval \
  --white search:depth=6,eval_config=data/eval/current_default.eval \
  --games 40 \
  --swap-sides true \
  --seed 20260607 \
  --openings data/openings/eval_regression_openings.txt \
  --output runs/matches/soft-exact-score-sign-anchor-50k-vs-current-default-depth6-40.jsonl \
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
| avg disc diff from PatternOnly | -44.60 |
| avg nodes PatternOnly | 193673.52 |
| avg nodes current_default | 397194.75 |
| avg ms PatternOnly | 55.94 |
| avg ms current_default | 88.30 |

The deterministic match is still strongly negative. This candidate is not
promotion-ready and must not be described as stronger.

## Decision

Small selected sign anchoring did what it was designed to do: exact sign
agreement improved and wrong-direction decreased while preserving most of the
PR323 exact-rank improvement. It did not solve standalone PatternOnly playing
strength. Keep this as objective-foundation evidence only; do not add a
retained preset, committed eval table, or default promotion.
