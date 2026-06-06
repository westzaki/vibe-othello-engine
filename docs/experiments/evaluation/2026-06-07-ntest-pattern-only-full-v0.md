# NTest Pattern-Only Full V0 Foundation

This report records the first implementation pass for generating a scalar-free
PatternOnly evaluator candidate from the NTest balanced 300K teacher dataset.
It is not a default-promotion report and does not change `current_default.eval`.

## Hypothesis

A phase-specific learned pattern table can become a stronger PatternOnly /
PatternFirst foundation than the current project default shape, which combines
scalar fallback weights with learned pattern-table deltas.

The first candidate shape should be explicit, scalar-free, reversible, and
loadable through `--eval-config` before any default promotion is considered.

## Dataset

| field | value |
| --- | --- |
| teacher dataset | `dataset:teacher/ntest-balanced300k-v0` |
| exact overlap | `dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl` |
| raw local output | `runs/pattern-training/ntest-balanced300k-v0/pattern_only_full_v0` |
| source-controlled dataset | none |

The dataset itself and raw run logs are local artifacts and are not committed.

## Teacher

| field | value |
| --- | --- |
| teacher | `ntest12-local` |
| source label rows | 300000 |
| usable teacher rows | 299913 |
| split seed | `20260601` in the dataset, `20260607` in this trainer invocation |

## Trainer Command

Intended full candidate command. The trainer accepts a comma-separated path
list, and the path resolver does not expand globs, so the shard list is built
explicitly before invocation:

```sh
teacher_labels="$(
  python3 - <<'PY'
print(",".join(
    "dataset:teacher/ntest-balanced300k-v0/labels/ntest12-local/shards/"
    f"labels-{index:04d}.jsonl"
    for index in range(300)
))
PY
)"

python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --teacher-labels "$teacher_labels" \
  --exact-labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config data/eval/current_default.eval \
  --analyze-position build/release/othello_analyze_position \
  --out-dir runs/pattern-training/ntest-balanced300k-v0/pattern_only_full_v0 \
  --analysis-cache-dir runs/pattern-training/ntest-balanced300k-v0/analysis-cache \
  --analysis-cache-mode read-write \
  --analysis-jobs 8 \
  --families all \
  --split train \
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

The implementation added `--candidate-eval-shape base-plus-delta|pattern-only`.
The default is `base-plus-delta` for compatibility. With `pattern-only`, the
rendered candidate has `mode=pattern_only`, phase-specific table paths,
`opening.pattern_table` / `midgame.pattern_table` / `late.pattern_table`
weights, phase thresholds inherited from the base eval, and no scalar feature
keys.

## Candidate Shape

| field | value |
| --- | --- |
| shape | `pattern_only` |
| base margin | disabled with `--no-base-margin` |
| scalar feature keys | omitted |
| phase table paths | `tables/opening.tsv`, `tables/midgame.tsv`, `tables/late.tsv` |
| phase thresholds | inherited from `data/eval/current_default.eval` |
| default promotion | no |

## Families

`--families all` was selected for this foundation pass.

This means every currently supported C++ runtime pattern-table family is filled:
`corner_2x3`, `corner_3x3`, `edge_8`, `edge_x_10`, `diagonal_8`, and
`inner_row_8`.

`all` is intentionally broader than `broad_all` because the goal is to exercise
the full existing runtime table capacity before adding new pattern families.
No new C++ pattern family was added in this PR.

## Training Result

The full `best-vs-all` training run was not completed in this PR.

Local timing showed that label parsing and rule-side preprocessing are not the
main bottleneck:

| step | rows | elapsed |
| --- | ---: | ---: |
| JSONL read | 300000 | 4.01 s |
| split / phase / legal move preprocessing | 209390 train rows | 14.95 s |
| `othello_analyze_position` subprocess smoke | 100 positions | 5.78 s |

Because `best-vs-all` needs root scores for each selected training position,
the current trainer launches `othello_analyze_position` once per unique board.
At the observed subprocess rate, the 209390 train-row analysis step is hours
rather than minutes on this setup. The trainer currently writes the analysis
cache after the full batch completes, so interrupted runs do not leave a useful
partial cache.

## Teacher Agreement

Not collected. The full candidate was not generated.

## Exact Overlap

Not collected. The full candidate was not generated.

## Search Smoke

Not collected for a trained `ntest_pattern_only_full_v0` candidate. Loader and
script rendering tests were run instead.

## Deterministic Match

Not collected. The full candidate was not generated.

## Positive Evidence

- The trainer now has an explicit `--candidate-eval-shape pattern-only` render
  path.
- PatternOnly candidate rendering omits scalar feature keys.
- The rendered candidate preserves base phase thresholds.
- The rendered candidate uses phase-specific table paths and phase
  `pattern_table` weights.
- The default render path remains `base-plus-delta`.
- The C++ loader already supports `mode=pattern_only`; existing tests cover the
  loader behavior.
- The trainer now recognizes `engine_move` from teacher label rows as a
  precomputed engine move for analysis-free `best-vs-engine` runs.

## Negative Evidence

- The requested full 300K `best-vs-all` candidate was not generated.
- No trained `data/eval/ntest_pattern_only_full_v0.eval` or source-controlled
  pattern tables were promoted in this PR.
- The current `best-vs-all` analysis path is dominated by many short
  `othello_analyze_position` subprocess launches.
- Analysis cache writes happen after all unique missing analyses finish, so an
  interrupted full run does not preserve partial progress.
- No teacher agreement, exact-overlap, search smoke, or deterministic match
  evidence exists yet for the full PatternOnly candidate.

## Runtime Impact

No engine runtime behavior changes were made. The C++ evaluator, exact endgame
semantics, `default_evaluation_config()`, and `data/eval/current_default.eval`
are unchanged.

The tooling runtime concern is in candidate generation: `best-vs-all` currently
requires a large root-analysis batch and pays process startup overhead for each
position.

## Decision

`KEEP_EXPERIMENTAL_FOUNDATION_NEEDS_FULL_TRAINING`

This PR should be treated as the PatternOnly rendering and measurement
foundation, not as the full candidate artifact PR. Promotion is not considered.

## Next Action

1. Add a faster batch or persistent root-analysis path for
   `othello_analyze_position`.
2. Stream analysis-cache writes as each future completes, so interrupted full
   runs are resumable.
3. Rerun the intended full `pattern-only` training command.
4. Promote the generated `.eval` and phase tables only after the full candidate
   exists and loader, exact-overlap, search smoke, and deterministic match
   evidence have been collected.
