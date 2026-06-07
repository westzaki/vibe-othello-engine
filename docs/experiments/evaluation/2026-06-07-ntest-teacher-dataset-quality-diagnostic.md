# NTest Teacher 300K Dataset Quality Diagnostic

This report records a data-quality diagnostic pass for the local NTest teacher
300K dataset before any further PatternOnly training.

It does not add source-controlled dataset rows, does not run large training,
and does not change `data/eval/current_default.eval`.

## Purpose

PatternOnly full v0 improved teacher rank but was very weak in deterministic
matches. The next pass should first expose whether the training signal is being
damaged by teacher/exact disagreement, phase or bucket imbalance, noisy exact
top groups, or too many weak-margin pairs.

The trainer now supports `--diagnose-dataset`, which runs teacher loading,
exact overlap matching, engine root analysis, and pair generation diagnostics
without training or writing candidate evaluator artifacts.

## Implemented Controls

- `--diagnose-dataset`: write `summary.json`, `dataset_diagnostic.md`, and
  `diagnostic_validation.tsv`; do not train or write `candidate.eval`.
- `--drop-teacher-exact-disagreement`: drop rows whose available exact label
  does not include the teacher move.
- `--exact-aware-only-when-available`: with `--pair-mode exact-aware`, skip
  non-overlap rows instead of falling back to teacher pairs.
- `--min-score-margin`: already existed; now reported in dataset diagnostics
  as the weak-margin pair filter.
- `--max-top-group-size-for-training`: drop rows where the exact-best group is
  too broad for clean exact-aware training.

Diagnostics are grouped by split, phase, and source bucket. The machine-readable
`summary.json` keeps full distributions for teacher rows, accepted rows,
illegal/skipped rows, teacher/exact disagreement, exact unavailable rows, legal
move counts, pair counts, score margins, rank margins, pair weight mass,
teacher-in-exact-best, engine-in-exact-best, teacher rank, and exact-best top
group size.

## Commands

The diagnostic runs used a local dataset root outside the repository:

```sh
DATASET_ROOT=/path/to/vibe-othello-datasets
```

1K smoke:

```sh
python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --diagnose-dataset \
  --teacher-labels dataset:teacher/ntest-balanced300k-v0/labels/ntest12-local/shards/labels-0000.jsonl \
  --dataset-root "$DATASET_ROOT" \
  --exact-labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config data/eval/current_default.eval \
  --analyze-position build/release/othello_analyze_position \
  --out-dir runs/pattern-training/ntest-balanced300k-v0/diagnostics-1k \
  --analysis-cache-dir runs/pattern-training/ntest-balanced300k-v0/diagnostic-analysis-cache \
  --analysis-cache-mode read-write \
  --analysis-runner batch \
  --analysis-jobs 8 \
  --families all \
  --split all \
  --pair-mode exact-aware \
  --pair-weighting exact-boost \
  --drop-teacher-exact-disagreement \
  --exact-aware-only-when-available \
  --min-score-margin 4 \
  --max-top-group-size-for-training 4 \
  --max-pairs-per-position 8 \
  --loss logistic \
  --no-base-margin \
  --candidate-eval-shape pattern-only \
  --seed 20260607
```

The 10K and 50K runs used the same command shape with shard ranges
`labels-0000.jsonl` through `labels-0009.jsonl`, and `labels-0000.jsonl`
through `labels-0049.jsonl`.

## Generated Reports

| slice | report | summary |
| --- | --- | --- |
| 1K | `runs/pattern-training/ntest-balanced300k-v0/diagnostics-1k/dataset_diagnostic.md` | `runs/pattern-training/ntest-balanced300k-v0/diagnostics-1k/summary.json` |
| 10K | `runs/pattern-training/ntest-balanced300k-v0/diagnostics-10k/dataset_diagnostic.md` | `runs/pattern-training/ntest-balanced300k-v0/diagnostics-10k/summary.json` |
| 50K | `runs/pattern-training/ntest-balanced300k-v0/diagnostics-50k/dataset_diagnostic.md` | `runs/pattern-training/ntest-balanced300k-v0/diagnostics-50k/summary.json` |

These are local run artifacts under `runs/`; they are evidence, not
source-controlled datasets.

## Results

| slice | teacher rows | accepted | unusable | exact unavailable | teacher/exact disagreement | broad exact top group skipped | paired positions | pairs | pair weight mass |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1K | 1000 | 999 | 1 | 957 | 0 | 0 | 41 | 221 | 442 |
| 10K | 10000 | 9995 | 5 | 9670 | 0 | 1 | 322 | 1733 | 3466 |
| 50K | 50000 | 49988 | 12 | 48283 | 0 | 2 | 1680 | 9107 | 18214 |

Phase concentration in the 50K diagnostic:

| phase | teacher rows | accepted | exact unavailable | pair weight mass |
| --- | ---: | ---: | ---: | ---: |
| opening | 4213 | 4213 | 4213 | 0 |
| midgame | 29460 | 29457 | 29457 | 0 |
| late | 16327 | 16318 | 14613 | 18214 |

The available exact overlap is late-only in this slice, so the safe exact-aware
preset intentionally trains only late rows unless more exact labels are
generated. `source_bucket` is missing in this dataset, so bucket diagnostics
collapse to `__missing__`; future mixed-source datasets should populate
`source_bucket` before relying on bucket balance.

50K exact-overlap quality:

| metric | value |
| --- | ---: |
| teacher in exact-best | 1705 |
| teacher not in exact-best | 0 |
| engine in exact-best | 975 |
| engine not in exact-best | 728 |
| exact-best in engine top group | 981 |
| exact-best not in engine top group | 722 |

50K exact-best top group sizes were mostly narrow: 1366 singleton rows, 263
size-2 rows, 62 size-3 rows, 12 size-4 rows, and 2 size-5 rows. With
`--max-top-group-size-for-training 4`, only the two size-5 rows were filtered.

50K accepted exact-aware pairs after `--min-score-margin 4` still include a
small weak-margin tail: score-margin buckets were `2-4:59`, `5-8:184`,
`9-16:445`, `17-32:928`, `33-64:1726`, `65-128:2421`, and `129+:3344`.
Rank-margin buckets were `1:2167`, `2-4:4935`, `5-8:1926`, and `9-16:79`.

## Gate For Full 300K

Proceed to full 300K only if the intended experiment accepts these constraints:

- exact-aware-only training on the current exact-overlap artifact is late-only
  and uses about 3.4% of the first 50K teacher rows.
- teacher/exact disagreement is not the observed failure mode in the 50K slice;
  it was 0 / 1705 exact-overlap rows.
- exact unavailable rows dominate because the exact-overlap artifact only covers
  endgame positions; do not mix fallback teacher pairs into exact-aware training
  unless that is the explicit hypothesis.
- broad exact top groups are rare with cap 4, so keeping
  `--max-top-group-size-for-training 4` is safe for the next pass.
- pair weight mass is entirely late phase and `__missing__` bucket in this
  dataset; do not interpret resulting PatternOnly weights as balanced
  opening/midgame learning.
- weak rank-margin-1 pairs remain common. If the next run overfits noisy
  near-ties, raise `--min-score-margin` or add a stricter rank-margin filter in
  a separate measured change.

Safe preset for the next exact-aware training experiment:

```sh
python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --teacher-labels "$teacher_labels" \
  --dataset-root "$DATASET_ROOT" \
  --exact-labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config data/eval/current_default.eval \
  --analyze-position build/release/othello_analyze_position \
  --out-dir runs/pattern-training/ntest-balanced300k-v0/exact_aware_safe_v0 \
  --analysis-cache-dir runs/pattern-training/ntest-balanced300k-v0/diagnostic-analysis-cache \
  --analysis-cache-mode read-write \
  --analysis-runner batch \
  --analysis-jobs 8 \
  --families all \
  --split all \
  --pair-mode exact-aware \
  --pair-weighting exact-boost \
  --drop-teacher-exact-disagreement \
  --exact-aware-only-when-available \
  --min-score-margin 4 \
  --max-top-group-size-for-training 4 \
  --max-pairs-per-position 8 \
  --loss logistic \
  --l2 1e-3 \
  --epochs 5 \
  --learning-rate 0.05 \
  --no-base-margin \
  --candidate-eval-shape pattern-only \
  --seed 20260607
```

This preset is safe for investigating exact-aware PatternOnly learning, but it
is not balanced enough to justify promotion by itself.
