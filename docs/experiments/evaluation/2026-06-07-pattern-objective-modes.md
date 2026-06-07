# Pattern Objective Modes v0

This report records the first correctness-first implementation pass for
exact-aware, tie-aware, and sign-aware PatternOnly objectives. It is not an Elo
estimate and does not promote a default evaluator.

`current_default.eval`, `default_evaluation_config()`, retained presets, and
committed evaluation tables were not changed.

## Hypothesis

PatternOnly full v0 had high pairwise training accuracy, but poor selected
teacher agreement and deterministic match behavior. Broadening the pattern
vocabulary did not recover this. The next likely failure boundary is therefore
the objective/output shape: pairwise teacher-vs-child deltas may not preserve
the root list ordering, exact-best set, sign direction, or quantized runtime
score shape that search actually consumes.

This pass keeps the pattern vocabulary and runtime shape fixed and adds trainer
objective modes before considering a larger evaluator change.

## Implementation

`tools/scripts/regularized_pairwise_pattern_train.py` now supports:

- `--objective pairwise-logistic|listwise-softmax|exact-aware-listwise`.
- listwise softmax over root legal candidates.
- exact-aware targets using `exact_best_moves` when exact labels are available,
  with teacher-move fallback when exact labels are unavailable.
- `--tie-penalty`, `--target-top-group-size`, and `--top-group-margin` to
  penalize overly broad top groups.
- `--sign-penalty` for exact-overlap rows where model ordering contradicts
  exact score ordering.
- `--calibrate-output-scale` and `--scale-grid` to choose a quantized output
  scale from validation/listwise diagnostics.
- trainer reports for selected teacher agreement, average teacher rank, top
  tie rate, exact-best top group, average exact-best rank, exact sign
  agreement, wrong direction, and high-confidence wrong direction.

The existing pairwise mode remains the default. For compatibility, pairwise
training does not construct listwise examples unless scale calibration needs
them.

## Test Coverage

Added synthetic trainer tests for:

- listwise softmax overfitting a target move.
- exact-aware listwise choosing an exact-best move when the teacher disagrees.
- output-scale calibration selecting the best grid value after quantization.

The previous pairwise trainer tests remain enabled.

## Commands

1K exact-aware listwise smoke:

```bash
python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --teacher-labels runs/pattern-training/objective-smoke-inputs/teacher-1000.jsonl \
  --exact-labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config data/eval/current_default.eval \
  --analyze-position build/othello_analyze_position \
  --out-dir runs/pattern-training/objective-smoke/listwise-1k \
  --analysis-cache-dir runs/pattern-training/objective-smoke/analysis-cache \
  --analysis-cache-mode read-write \
  --analysis-runner batch \
  --analysis-jobs 4 \
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
  --sign-penalty 1 \
  --no-base-margin \
  --candidate-eval-shape pattern-only \
  --calibrate-output-scale \
  --scale-grid 1,2,4,8,16 \
  --seed 20260607
```

10K pairwise baseline and 10K exact-aware listwise used the same command shape,
with `--objective pairwise-logistic` for the baseline and
`--objective exact-aware-listwise` for the new candidate. The 10K move-choice
diagnostic was:

```bash
python3 tools/scripts/eval_move_choice_diagnostics.py \
  --teacher-labels runs/pattern-training/objective-smoke-inputs/teacher-10000.jsonl \
  --exact-labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --analyze-position build/othello_analyze_position \
  --config pairwise_10k=runs/pattern-training/objective-smoke/pairwise-10k/candidate.eval \
  --config listwise_10k=runs/pattern-training/objective-smoke/listwise-10k/candidate.eval \
  --limit 10000 \
  --out runs/pattern-training/objective-smoke/move-choice-10k.md \
  --analysis-jobs 4
```

Exact-overlap diagnostics used `build/othello_eval_vs_exact --top 10000
--move-rank-analysis` for each candidate.

## 1K Smoke

The 1K exact-aware listwise run selected `output_scale=4.0`.

| metric | quantized final |
| --- | ---: |
| rows | 999 |
| selected teacher agreement | 98.70% |
| avg teacher rank | 1.009 |
| top tie rate | 21.62% |
| exact-best top group | 100.00% |
| avg exact-best rank | 1.000 |
| exact sign agreement | 62.50% |
| wrong direction | 15 |
| high-confidence wrong direction | 0 |

This confirms the new objective can overfit a small real subset and survive
output quantization.

## 10K Teacher Move Choice

The pairwise baseline is a same-subset, same-shape compatibility baseline
because the historical `pattern_only_full_v0` artifact was not present in this
worktree. Both candidates are PatternOnly and use `families=all`.

| config | rows | selected agreement | avg teacher rank | avg top group | top tie rate | exact-best top group | avg exact-best rank |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| pairwise_10k | 9,995 | 28.35% | 2.296 | 2.446 | 53.52% | 64.92% | 1.674 |
| listwise_10k | 9,995 | 72.73% | 1.472 | 1.057 | 5.44% | 96.62% | 1.052 |

The new objective improved selected teacher agreement and top tie rate by a
large margin. It also improved exact-best top-group rate on the teacher-overlap
subset used by the move-choice diagnostic.

## 10K Exact Overlap

| config | sign agreement | wrong direction | high-confidence wrong | exact-best top group | avg exact-best rank |
| --- | ---: | ---: | ---: | ---: | ---: |
| pairwise_10k | 50.64% | 2,159 | 0 | 58.24% | 2.063 |
| listwise_10k | 62.86% | 3,188 | 0 | 30.03% | 2.926 |

Exact sign agreement improved, satisfying one of the target metrics. However,
wrong-direction count increased and exact-best root ranking regressed on the
standalone exact-overlap benchmark. This is negative evidence: selected
agreement and top tie rate improved, but exact-best rank remains unresolved.
The sign-aware penalty as implemented improves coarse sign agreement but is not
yet enough to preserve exact-best ranking under the quantized runtime evaluator.

## 50K Attempt

50K exact-aware listwise was attempted twice using the same command shape and
the local 50K subset. The first run populated most of
`runs/pattern-training/objective-smoke/analysis-cache/root_analysis.jsonl`;
the second run reused that cache.

Both runs were stopped after many minutes without producing a candidate. The
remaining bottleneck is Python feature extraction/listwise training, not
subprocess root analysis. No 50K metric is reported from this pass.

This is negative evidence for the current implementation: 10K is useful for
objective comparison, but 50K needs trainer-side optimization before it is a
practical comparison loop.

## Verification

```bash
python3 -m unittest tools.scripts.tests.test_regularized_pairwise_pattern_train
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Result:

- Python trainer tests: 63 passed.
- CTest: 353/353 passed.

## Decision

`KEEP_EXPERIMENTAL_OBJECTIVE_NEEDS_50K_AND_EXACT_RANK_FIX`

The 10K evidence clears the immediate output-shape hypothesis for selected
agreement and top-tie collapse: listwise/exact-aware training can produce a
root ordering that the runtime evaluator reflects after quantization. It does
not clear the exact ranking hypothesis: selected agreement and top tie rate
improved, but exact-best rank remains unresolved. Before any strength or
default promotion claim, the next pass should make 50K training practical and
reduce wrong-direction/exact-best-rank regressions on exact-overlap.
