# NTest Pattern-Only Full V0 Batch Analysis

This report records the follow-up implementation and measurement pass after
the PatternOnly rendering foundation. It generated a full local
`pattern-only` candidate from the NTest balanced 300K teacher dataset, measured
it, and rejected source-controlled promotion for this pass.

It does not change `data/eval/current_default.eval`,
`default_evaluation_config()`, or exact-endgame semantics.

## Hypothesis

The largest blocker for full PatternOnly training is not label parsing or
pattern rendering, but root analysis. Replacing one short
`othello_analyze_position` subprocess per training position with batch JSONL
analysis should make the full 300K training run feasible and make interrupted
runs resumable through streaming cache writes.

The resulting candidate should be evaluated as an experimental PatternOnly
foundation only. Strength claims require exact-overlap, search, and match
evidence.

## Implementation Summary

- Added `--batch-jsonl --stdin` mode to `othello_analyze_position`.
- Batch input is JSONL with `position_id` and `board` or `board_text`.
- Batch output is JSONL with `position_id`, `status`, `best_move`, `score`,
  `depth`, `nodes`, `elapsed_ms`, `root_scores`, and optional `error`.
- Existing single-position behavior remains unchanged.
- Added Python batch and parallel-batch runners for pattern training.
- Added `--analysis-runner subprocess|batch`; default remains `subprocess`.
- `--analysis-jobs` now controls the number of parallel persistent batch
  analyzer processes for the batch runner.
- Analysis cache rows are appended immediately as each result is returned.
- Duplicate cache keys within one run are analyzed once and fanned out to all
  source rows.

## Commands

The intended full command uses an explicit comma-separated shard list. Glob
expansion is intentionally not required by this PR.

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
  --analysis-runner batch \
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

For this local run, a pre-concatenated ignored JSONL file under `runs/` was
used instead of passing the long comma-separated shard list. The source dataset
and run artifacts remain local artifacts and are not committed.

## Batch Root Analysis Design

`othello_analyze_position --batch-jsonl --stdin` keeps the analyzer process
alive and streams one JSON result per input row. Python writes JSONL requests
to stdin and reads stdout line-by-line, so each completed root analysis can be
stored in the cache immediately.

Invalid input rows emit structured `status:"error"` JSON rows and cause the
tool to exit nonzero after processing input. The trainer keeps fail-fast
behavior for failed analysis rows.

## Cache Behavior

| run | rows | hits | misses | writes | analysis elapsed |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1K smoke | 702 train | 0 | 702 | 702 | 2.759 s |
| 1K resume | 702 train | 702 | 0 | 0 | 0.008 s |
| 10K smoke | 7036 train | 0 | 7036 | 7036 | 26.619 s |
| full 300K train | 209390 train | 0 | 209390 | 209390 | 799.661 s |

The full run wrote 209390 cache rows before the later pair/training phase
completed. The final full training artifact was written about 10 minutes after
analysis completion, so the next bottleneck is now pair feature construction /
training rather than process startup.

## Speed Comparison

100-position root-analysis smoke on the same dataset slice:

| runner | rows | elapsed | rows/s |
| --- | ---: | ---: | ---: |
| subprocess | 100 | 5.129 s | 19.5 |
| batch, 1 process | 100 | 1.259 s | 79.4 |
| batch, 8 processes | 100 | 0.439 s | 227.5 |

The full train split analyzed 209390 rows in 799.661 s, or about 261.9
analyses/s. At the measured subprocess rate, the same root-analysis work would
be about 2.98 hours. The batch run completed that phase in about 13.3 minutes.

## Full Training Result

| metric | value |
| --- | ---: |
| teacher rows | 300000 |
| accepted teacher rows | 299913 |
| train rows | 209390 |
| validation rows | 45331 |
| holdout rows | 45192 |
| paired positions | 209158 |
| preference pairs | 1508599 |
| final weighted loss | 0.635908 |
| final unweighted loss | 0.690256 |
| final weighted accuracy | 0.871184 |
| final accuracy | 0.837388 |

Generated local candidate:

```text
mode=pattern_only
pattern_table.opening=tables/opening.tsv
pattern_table.midgame=tables/midgame.tsv
pattern_table.late=tables/late.tsv
opening.pattern_table=1
midgame.pattern_table=1
late.pattern_table=1
opening_max_occupied=20
midgame_max_occupied=44
```

The candidate is scalar-free and has all runtime table families represented in
the generated phase tables. The generated tables remain under `runs/`.

## Teacher Agreement

Teacher agreement was measured at root-analysis depth 1 against the NTest
teacher labels. This is teacher-following evidence, not exact truth.

| split | config | agreement | avg teacher rank | rank sum | rank1 | rank2 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| train | current_default | 94663 / 209390 (45.21%) | 2.371 | 496440 | 95736 | 45453 |
| train | pattern_only_full_v0 | 63837 / 209390 (30.49%) | 1.958 | 409909 | 129675 | 39971 |
| validation | current_default | 20416 / 45331 (45.04%) | 2.408 | 109174 | 20638 | 9604 |
| validation | pattern_only_full_v0 | 13700 / 45331 (30.22%) | 1.976 | 89592 | 27941 | 8601 |
| holdout | current_default | 20362 / 45192 (45.06%) | 2.392 | 108083 | 20565 | 9710 |
| holdout | pattern_only_full_v0 | 13600 / 45192 (30.09%) | 1.968 | 88917 | 27783 | 8695 |

PatternOnly improves teacher rank and top-rank inclusion, but it often does
not select the teacher move as the single chosen move. The sparse quantized
tables likely create many ties or near-ties, which is useful rank evidence but
not sufficient promotion evidence.

## Exact Overlap

Command:

```sh
build/release/othello_eval_vs_exact \
  --labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config runs/pattern-training/ntest-balanced300k-v0/pattern_only_full_v0/candidate.eval \
  --top 10000 \
  --move-rank-analysis \
  --output runs/eval/ntest_pattern_only_full_v0_exact.md
```

| metric | current_default | pattern_only_full_v0 |
| --- | ---: | ---: |
| analyzed | 10000 | 10000 |
| sign agreements | 8005 | 5138 |
| wrong direction | 1650 | 2400 |
| high-confidence wrong direction | 220 | 0 |
| exact-best top group | 5582 | 6017 |
| exact-best rank sum | 18434 | 20119 |
| avg exact-best rank | 1.843 | 2.012 |

The full PatternOnly candidate improves exact-best top-group count and removes
high-confidence wrong directions in this sample, but sign agreement and
exact-best rank sum are worse than `current_default`.

## Search Smoke

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
  --eval-config runs/pattern-training/ntest-balanced300k-v0/pattern_only_full_v0/candidate.eval \
  --format jsonl
```

| depth | config | nodes | elapsed ms | eval calls | best move | result checksum |
| ---: | --- | ---: | ---: | ---: | --- | --- |
| 5 | current_default | 16933 | 14.907 | 15484 | b2 | 3407339372377443460 |
| 5 | pattern_only_full_v0 | 14045 | 14.161 | 12992 | d6 | 18053768697416321897 |
| 6 | current_default | 46720 | 20.403 | 42810 | d6 | 6917595495085306980 |
| 6 | pattern_only_full_v0 | 40445 | 22.228 | 37881 | b2 | 7810819286316182763 |
| 7 | current_default | 150234 | 60.637 | 138775 | b2 | 12106001709669006217 |
| 7 | pattern_only_full_v0 | 118082 | 44.027 | 109985 | d6 | 8474057457328878056 |

The candidate loads and searches successfully. Best moves and checksums change
at every tested depth.

## Deterministic Match

Command:

```sh
build/release/othello_match_runner \
  --black search:depth=6,eval_config=runs/pattern-training/ntest-balanced300k-v0/pattern_only_full_v0/candidate.eval \
  --white search:depth=6,eval_config=data/eval/current_default.eval \
  --games 200 \
  --swap-sides true \
  --seed 20260607 \
  --openings data/openings/eval_regression_openings.txt \
  --output runs/matches/ntest-pattern-only-full-v0-vs-current-default-depth6.jsonl \
  --format jsonl \
  --quiet
```

| metric | value |
| --- | ---: |
| games | 200 |
| PatternOnly wins | 2 |
| PatternOnly losses | 198 |
| draws | 0 |
| errors | 0 |
| avg disc diff from PatternOnly | -44.08 |
| avg nodes PatternOnly | 239777.6 |
| avg nodes current_default | 389647.5 |
| avg ms PatternOnly | 48.798 |
| avg ms current_default | 85.198 |

The match smoke is strongly negative. This candidate should not be promoted.

## Positive Evidence

- Full 300K PatternOnly training is now feasible with the batch runner.
- Streaming cache writes made the full analysis phase resumable.
- The local full candidate is scalar-free, phase-specific, and loadable.
- Teacher rank improved across train, validation, and holdout.
- Exact-overlap top-group count improved and high-confidence wrong directions
  dropped to zero on the 10K exact-overlap sample.
- Search smoke and deterministic match completed without loader or runtime
  errors.

## Negative Evidence

- Teacher top-1 selected-move agreement regressed across all splits.
- Exact sign agreement and exact-best rank sum regressed versus
  `current_default`.
- Depth-6 deterministic match was 2 wins / 198 losses / 0 draws against
  `current_default`.
- The full candidate is still too sparse/quantized to be a practical playing
  evaluator on its own.
- After root-analysis acceleration, pair feature construction and training are
  now visible as the next major training bottleneck.

## Decision

`DO_NOT_PROMOTE_PATTERN_ONLY_FULL_V0`

Do not add `data/eval/ntest_pattern_only_full_v0.eval` or source-controlled
phase tables in this PR. Keep `current_default.eval` unchanged.

This PR is valuable as batch-analysis and streaming-cache infrastructure plus
full-candidate negative evidence, not as a retained evaluator artifact.

## Next Action

1. Make teacher/exact agreement evaluation use the batch runner directly, so
   full split comparisons do not require one-off scripts.
2. Investigate tie-aware or rank-aware PatternOnly selection: teacher rank
   improved, but selected top-1 agreement and match strength did not.
3. Add streaming or chunked pair feature construction so full training does not
   materialize all pair features before writing outputs.
4. Try `pattern_only_with_base_anchor` or a small explicit anchor term before
   any PatternFirst/default-promotion discussion.
5. Consider a cache format that can store analysis rows for multiple eval
   configs and splits in one reusable evaluation cache.
