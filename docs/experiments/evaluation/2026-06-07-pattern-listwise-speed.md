# Pattern Listwise Trainer Speedup

Date: 2026-06-07

## Purpose

Make the PatternOnly `exact-aware-listwise` objective practical at 50K+ rows
without changing objective semantics, promoting a default evaluator, or
committing generated eval tables.

This is tooling evidence only. It does not change `current_default.eval`, does
not add a retained preset, and does not claim playing-strength improvement.

## Implementation

- Added a listwise child-board feature cache keyed by dataset hashes, eval config
  hash, selected families, phase cutoffs, objective, pair/listwise policy, and
  seed.
- Added a compact listwise training representation:
  - example candidate offsets
  - candidate feature offsets
  - flattened feature ids and values
  - candidate phase ids
  - target and exact-best masks
  - exact child scores and exact root scores
- Kept `pairwise-logistic` behavior unchanged.
- For listwise objectives, skipped unused pairwise preference-pair feature
  construction while retaining validation records for rows used by listwise
  training.
- Added timing fields to `summary.json` and `report.md`:
  - label load seconds
  - exact load seconds
  - analysis seconds
  - feature construction seconds
  - listwise training seconds
  - examples/sec
  - updates/sec

## Commands

Commands below use a local dataset root placeholder to avoid embedding
machine-specific paths in repository history.

```sh
DATASET_ROOT=<dataset-root>

python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --teacher-labels runs/pattern-training/objective-smoke-inputs/teacher-10000.jsonl \
  --exact-labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --dataset-root "$DATASET_ROOT" \
  --eval-config data/eval/current_default.eval \
  --analyze-position build/othello_analyze_position \
  --out-dir runs/pattern-training/listwise-speed/listwise-10k-cache-hit \
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
  --sign-penalty 1 \
  --no-base-margin \
  --candidate-eval-shape pattern-only \
  --calibrate-output-scale \
  --scale-grid 1,2,4,8,16 \
  --seed 20260607
```

The 50K command is identical except:

```sh
--teacher-labels runs/pattern-training/objective-smoke-inputs/teacher-50000.jsonl
--out-dir runs/pattern-training/listwise-speed/listwise-50k-cache-hit
```

## 10K Equivalence

The optimized 10K run matched the PR313 exact-aware-listwise 10K metrics.

| Metric | PR313 10K | Optimized 10K |
| --- | ---: | ---: |
| selected teacher agreement | 0.726071 | 0.726071 |
| top tie rate | 0.638767 | 0.638767 |
| exact sign agreement | 0.668790 | 0.668790 |
| avg exact-best rank | 1.052469 | 1.052469 |

This supports that the speed changes did not alter the objective/output shape
for the 10K comparison point.

## Timing

Cold means the listwise feature cache was populated during the run. Cache-hit
means root analysis and listwise child-board features were already present.

| Run | Label load | Exact load | Analysis | Feature construction | Listwise training | Examples/sec | Updates/sec |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10K cold | 0.088s | 0.090s | 92.479s | 12.501s | 23.597s | 798.990 | 32027.038 |
| 10K cache-hit | 0.090s | 0.079s | 0.377s | 1.571s | 22.952s | 6357.660 | 32844.169 |
| 50K cold | 0.475s | 0.203s | 420.504s | 73.115s | 121.132s | 682.962 | 32379.292 |
| 50K cache-hit | 0.396s | 0.173s | 1.017s | 12.270s | 116.695s | 4069.705 | 33120.842 |

50K cache-hit compact dataset size:

- examples: 49,935
- candidates: 507,493
- flattened feature entries: 28,498,965
- unique features: 77,475

## 50K Completion

The 50K `exact-aware-listwise` run completed and wrote:

- `runs/pattern-training/listwise-speed/listwise-50k/candidate.eval`
- `runs/pattern-training/listwise-speed/listwise-50k-cache-hit/candidate.eval`

50K cache-hit diagnostics:

| Metric | Value |
| --- | ---: |
| selected teacher agreement | 0.486713 |
| avg teacher rank | 2.301172 |
| top tie rate | 0.727466 |
| exact-best top group rate | 0.865918 |
| avg exact-best rank | 1.622563 |
| exact sign agreement | 0.696615 |
| wrong direction | 493 |
| high-confidence wrong direction | 0 |

## Interpretation

The optimized trainer preserves the 10K PR313 metrics while making 50K
`exact-aware-listwise` candidate generation complete in a practical local run.
After both caches are warm, the remaining bottleneck is listwise SGD rather than
child-board feature extraction or root analysis.

The 50K run is evidence that the objective can now be compared at larger scale.
It is not evidence for evaluator promotion.

