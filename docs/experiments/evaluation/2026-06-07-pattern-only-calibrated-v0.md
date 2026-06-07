# PatternOnly calibrated v0

Date: 2026-06-07

## Hypothesis

PR #300 の broad pattern vocabulary で表現力は増えたが、50K broad candidate は top group tie が大きく、PatternOnly full v0 と同じく top-1 selection と実戦が崩れた。scalar anchor へ戻らず、PatternOnly のまま次を試す。

- anti-tie margin で「同点でもよい」学習を避ける。
- teacher top-1 pairs / exact-aware pairs の重みを調整する。
- output scale を上げ、量子化による score collapse を減らす。
- candidate `.eval` はすべて `mode=pattern_only`、scalar weights なしにする。

## Implementation Summary

`tools/scripts/regularized_pairwise_pattern_train.py` に scalar-free calibration knobs を追加した。

- `--anti-tie-margin`: pair margin から指定値を引いて loss/accuracy を計算し、余裕のある margin を要求する。
- `--teacher-top1-weight`: teacher preference pairs の追加倍率。
- `--exact-rank-weight`: exact-aware pairs の追加倍率。

`candidate-eval-shape pattern-only` の出力は引き続き scalar-free で、`opening.mobility` などの scalar keys は書かない。`current_default.eval`、`default_evaluation_config()`、exact endgame semantics は変更していない。

## Candidates

All candidates used `families=broad_v2`, `candidate-eval-shape=pattern-only`, `--no-base-margin`, `candidate-pattern-table-weight=1`, and train split rows from `runs/pattern-training/ntest-balanced300k-v0/local-inputs/labels-50000.jsonl`.

| candidate | pair mode | weighting | l2 | lr | output scale | calibration |
| --- | --- | --- | --- | --- | --- | --- |
| `pattern_only_calibrated_v0_50k` | `best-vs-all` | `score-margin` | `1e-3` | `0.05` | `4` | `anti_tie_margin=1.0`, `teacher_top1_weight=1.5` |
| `pattern_only_exact_aware_v0_50k` | `exact-aware` | `exact-boost` | `1e-3` | `0.05` | `4` | `anti_tie_margin=1.0`, `exact_best_weight=3.0`, `exact_rank_weight=1.25` |
| `pattern_only_high_scale_v0_50k` | `best-vs-all` | `score-margin` | `3e-4` | `0.03` | `8` | `anti_tie_margin=1.0`, `teacher_top1_weight=1.5` |

The `pattern_only_high_scale_v0_50k` candidate was the strongest 50K result and was attempted as `pattern_only_high_scale_v0_full`. The full run was stopped after repeated 5-minute polls without producing an artifact; the bottleneck was full-size pair training/SGD rather than root analysis cache misses.

## Commands

Representative 50K high-scale training:

```sh
python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --teacher-labels runs/pattern-training/ntest-balanced300k-v0/local-inputs/labels-50000.jsonl \
  --dataset-root <dataset-root> \
  --exact-labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config data/eval/current_default.eval \
  --analyze-position build/release/othello_analyze_position \
  --out-dir runs/pattern-training/ntest-balanced300k-v0/pattern_only_high_scale_v0_50k \
  --analysis-cache-dir runs/pattern-training/ntest-balanced300k-v0/analysis-cache \
  --analysis-cache-mode read-write \
  --analysis-runner batch \
  --analysis-jobs 8 \
  --families broad_v2 \
  --split train \
  --pair-mode best-vs-all \
  --pair-weighting score-margin \
  --max-pairs-per-position 8 \
  --loss logistic \
  --l2 3e-4 \
  --epochs 5 \
  --learning-rate 0.03 \
  --output-scale 8 \
  --candidate-pattern-table-weight 1 \
  --candidate-eval-shape pattern-only \
  --no-base-margin \
  --anti-tie-margin 1.0 \
  --teacher-top1-weight 1.5 \
  --seed 20260609
```

Move-choice diagnostics:

```sh
python3 tools/scripts/eval_move_choice_diagnostics.py \
  --teacher-labels runs/pattern-training/ntest-balanced300k-v0/local-inputs/labels-50000.jsonl \
  --dataset-root <dataset-root> \
  --exact-labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --config current_default=data/eval/current_default.eval \
  --config pattern_only_full_v0=runs/pattern-training/ntest-balanced300k-v0/pattern_only_full_v0/candidate.eval \
  --config pattern_broad_vocab_v0_50k=runs/pattern-training/ntest-balanced300k-v0/pattern_broad_vocab_v0_50k/candidate.eval \
  --config pattern_only_calibrated_v0_50k=runs/pattern-training/ntest-balanced300k-v0/pattern_only_calibrated_v0_50k/candidate.eval \
  --config pattern_only_exact_aware_v0_50k=runs/pattern-training/ntest-balanced300k-v0/pattern_only_exact_aware_v0_50k/candidate.eval \
  --config pattern_only_high_scale_v0_50k=runs/pattern-training/ntest-balanced300k-v0/pattern_only_high_scale_v0_50k/candidate.eval \
  --out runs/eval/pattern_only_calibrated_v0_50k_move_choice.json \
  --analyze-position build/release/othello_analyze_position \
  --analysis-jobs 8 \
  --depth 1 \
  --limit 10000
```

Exact overlap and match smoke used the same `candidate.eval` paths under `runs/`.

## Training

| candidate | wall seconds | cache hits | cache misses | cache writes | pairs | final accuracy | avg objective margin | quantized nonzero entries opening/midgame/late |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `pattern_only_calibrated_v0_50k` | 161.47 | 34,867 | 0 | 0 | 251,281 | 0.7964 | 2.4244 | 2,639 / 8,747 / 14,813 |
| `pattern_only_exact_aware_v0_50k` | 144.11 | 34,867 | 0 | 0 | 251,642 | 0.7546 | 1.5482 | 1,831 / 3,803 / 7,785 |
| `pattern_only_high_scale_v0_50k` | 148.25 | 34,867 | 0 | 0 | 251,281 | 0.8681 | 3.1441 | 4,607 / 26,720 / 40,020 |

## Move-Choice Diagnostics

10K rows from the 50K label subset.

| evaluator | selected teacher agreement | avg teacher rank | top tie rate | avg top group size | teacher in top group | exact-best top group | avg exact-best rank |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `current_default` | 0.4534 | 2.3889 | 0.0170 | 1.0174 | 0.4581 | 0.5538 | 1.8585 |
| `pattern_only_full_v0` | 0.3050 | 1.9508 | 0.5660 | 3.0248 | 0.6192 | 0.6523 | 1.5815 |
| `pattern_broad_vocab_v0_50k` | 0.2700 | 2.5252 | 0.5052 | 2.2738 | 0.4707 | 0.5662 | 1.8923 |
| `pattern_only_calibrated_v0_50k` | 0.4326 | 2.4188 | 0.0850 | 1.0933 | 0.4573 | 0.6338 | 1.6462 |
| `pattern_only_exact_aware_v0_50k` | 0.4251 | 2.4447 | 0.1044 | 1.1173 | 0.4541 | 0.7138 | 1.5323 |
| `pattern_only_high_scale_v0_50k` | 0.5282 | 2.0307 | 0.0360 | 1.0374 | 0.5392 | 0.7323 | 1.4431 |

`pattern_only_high_scale_v0_50k` is the first PatternOnly result in this line that improves both top tie rate and selected teacher agreement relative to `current_default` on this diagnostic slice.

## Exact-Overlap

Top 10K exact-overlap rows with move-rank analysis.

| evaluator | sign agreement | wrong direction | high-confidence wrong | exact-best top | exact-best rank sum | avg exact-best rank |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `current_default` | 8,005 | 1,650 | 220 | 5,582 | 18,434 | 1.8434 |
| `pattern_only_full_v0` | 5,138 | 2,400 | 0 | 6,017 | 20,119 | 2.0119 |
| `pattern_broad_vocab_v0_50k` | 5,140 | 2,269 | 0 | 5,234 | 20,992 | 2.0992 |
| `pattern_only_calibrated_v0_50k` | 6,339 | 3,122 | 0 | 3,163 | 28,288 | 2.8288 |
| `pattern_only_exact_aware_v0_50k` | 6,635 | 2,745 | 0 | 3,472 | 26,917 | 2.6917 |
| `pattern_only_high_scale_v0_50k` | 6,449 | 3,131 | 0 | 2,996 | 29,087 | 2.9087 |

Sign agreement improves substantially over previous PatternOnly/Broad 50K candidates, but exact-best rank quality regresses badly. This is the main negative evidence against retaining the candidates.

## Search Smoke

All candidates loaded and completed `othello_search_bench --mode iterative --depths 5,6,7 --positions evaluation`.

| evaluator | depth 5 nodes/ms | depth 6 nodes/ms | depth 7 nodes/ms | best moves d5/d6/d7 |
| --- | --- | --- | --- | --- |
| `current_default` | 16,933 / 20.950 | 46,720 / 31.971 | 150,234 / 100.104 | b2 / d6 / b2 |
| `pattern_only_calibrated_v0_50k` | 17,967 / 22.169 | 51,475 / 38.837 | 161,435 / 109.818 | f6 / b2 / d6 |
| `pattern_only_exact_aware_v0_50k` | 18,888 / 24.267 | 51,262 / 43.629 | 162,958 / 106.588 | d6 / b2 / d6 |
| `pattern_only_high_scale_v0_50k` | 19,358 / 37.048 | 55,899 / 46.354 | 182,352 / 78.702 | d6 / b2 / f6 |

Search smoke passes, but nodes generally increase vs `current_default`.

## Deterministic Match

Depth 6, 200 games, swapped sides, seed `20260609`, openings `data/openings/eval_regression_openings.txt`, candidate as player A vs `current_default` as player B.

| candidate | wins | losses | draws | errors | avg disc diff | avg candidate nodes | avg candidate ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `pattern_only_calibrated_v0_50k` | 9 | 191 | 0 | 0 | -35.25 | 385,650 | 106.135 |
| `pattern_only_exact_aware_v0_50k` | 7 | 193 | 0 | 0 | -36.42 | 394,797 | 107.061 |
| `pattern_only_high_scale_v0_50k` | 21 | 179 | 0 | 0 | -30.15 | 432,858 | 115.449 |

This is a clear improvement over the earlier `pattern_only_full_v0` 2 wins / 198 losses and broad 50K 0 wins / 200 losses, but it is still a decisive loss to `current_default`.

## Positive Evidence

- Scalar-free PatternOnly calibration reduced top tie collapse dramatically.
- `pattern_only_high_scale_v0_50k` improved selected teacher agreement to 0.5282 and top tie rate to 0.0360 on the 10K diagnostic slice.
- Match smoke improved from catastrophic 0-2 wins to 21 wins / 179 losses for the best 50K candidate.
- All generated `.eval` candidates are scalar-free `mode=pattern_only`; no current-default scalar weights are copied into them.

## Negative Evidence

- All candidates still lose badly to `current_default` in deterministic match.
- Exact-best rank sum regressed sharply for calibrated candidates, especially high-scale.
- Search nodes increased vs `current_default`.
- Full 300K high-scale escalation did not complete in a practical turn window; training/SGD scaling now needs optimization before full matrix runs become routine.

## Decision

Reject as retained preset. Do not add anything under `data/eval`, and do not promote or modify `current_default.eval`.

The useful result is directional: PatternOnly is not doomed by broad vocabulary alone, and tie/quantization calibration can move match results from 0-2 wins to 21 wins on 50K. The current objective still overfits top-choice/tie behavior while damaging exact-best rank calibration.

## Next Action

- Add a sparse/streaming or minibatch trainer path so full broad_v2 high-scale training can complete predictably.
- Try a multi-objective scalar-free loss that combines teacher top-1, exact-best rank preservation, and sign-safe constraints instead of only anti-tie margin.
- Evaluate smaller output-scale grid around 6-8 and add per-phase scale, because high-scale improved choice but hurt exact rank.
- Consider broader phase granularity only after the objective can preserve exact-best rank.
