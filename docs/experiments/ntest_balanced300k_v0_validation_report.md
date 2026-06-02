# NTest Balanced 300K v0 Validation Report

This report records validation evidence for the completed
`ntest-balanced300k-v0` teacher dataset before pattern-table training.

It is not a playing-strength claim, and it is not evidence for promoting
evaluator defaults. The results below validate teacher-data quality and identify
the remaining high-depth validation gap.

## Main Dataset Reference

| field | value |
| --- | --- |
| dataset id | `ntest-balanced300k-v0` |
| teacher | `ntest12-local` |
| teacher depth | `12` |
| lifecycle | `persistent` |
| usable labels | `299913 / 300000` |
| failed labels | `87` |
| illegal teacher moves | `0` |
| invalid move tokens | `83` |
| timed-out failures | `4` |

Training jobs must continue to use only rows where `status == "ok"` and
`label_usable == true`.

## Exact Overlap Results

| field | value |
| --- | ---: |
| dataset id | `ntest-balanced300k-v0-exact-overlap-v0` |
| positions | 10000 |
| exact labels | 10000 |
| skipped too many empties | 0 |
| compared rows | 9983 |
| missing or non-usable main labels | 17 |
| missing exact labels | 0 |
| missing exact move scores | 0 |
| exact-best single chosen move match | 8805 / 9983 (88.20%) |
| tied exact-best match | 9983 / 9983 (100.00%) |
| average score gap | 0.0 |
| p50 / p90 / max score gap | 0 / 0 / 0 |

Exact overlap empties distribution:

| empties | count |
| ---: | ---: |
| 12 | 2000 |
| 11 | 2000 |
| 10 | 1500 |
| 9 | 1500 |
| 8 | 1500 |
| 7 | 1500 |

Conclusion: the late/endgame exact overlap looks clean for usable rows. Every
compared usable depth-12 teacher move was tied for exact-best, and the exact
score gap was zero. The 17 non-compared rows were missing because the main
teacher row was absent or not usable.

## High-Depth Depth 24 Attempt

| field | value |
| --- | ---: |
| dataset id | `ntest-balanced300k-v0-highdepth-overlap-v0` |
| positions | 5000 |
| attempted depth | 24 |
| timeout | 5000 ms |
| jobs | 4 |
| completed label shards | 0 |
| observed failure logs before stop | 170 |

High-depth phase distribution:

| phase | count |
| --- | ---: |
| opening-ish | 800 |
| midgame | 2200 |
| late/endgame | 2000 |

Depth 24 with a 5000 ms per-position timeout is not ready as configured. The
run was stopped before the first label shard completed because timeout failures
accumulated quickly. No depth-12-vs-depth-24 comparison was generated.

## Artifact Paths

Paths below are relative to the external dataset root.

| artifact | dataset-relative path |
| --- | --- |
| exact overlap positions | `teacher/ntest-balanced300k-v0-exact-overlap-v0/source/positions.txt` |
| exact labels | `teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl` |
| exact comparison report | `teacher/ntest-balanced300k-v0-exact-overlap-v0/reports/exact_comparison_report.md` |
| exact comparison summary | `teacher/ntest-balanced300k-v0-exact-overlap-v0/reports/exact_comparison_summary.json` |
| exact worst examples | `teacher/ntest-balanced300k-v0-exact-overlap-v0/reports/exact_worst_examples.tsv` |
| depth-24 overlap positions | `teacher/ntest-balanced300k-v0-highdepth-overlap-v0/source/positions.txt` |
| depth-24 status report | `teacher/ntest-balanced300k-v0-highdepth-overlap-v0/reports/highdepth_status_report.md` |

## Current Verdict

`ntest-balanced300k-v0` remains `READY_FOR_PATTERN_TRAINING_DATASET_INPUT`.

Teacher quality validation is `PARTIAL`:

- exact/endgame overlap is strong
- high-depth midgame/opening validation still needs a follow-up

Pattern-table v0 training may proceed only with the explicit caveat that
midgame high-depth validation is incomplete. A high-depth follow-up should be
run before using learned tables as promotion evidence.

## Recommended Follow-Up

Use depth 20 as a separate high-depth validation candidate. Do not reuse or
overwrite the incomplete depth-24 dataset. Start with a small smoke subset
before running the 5000-position overlap.

Recommended order:

1. Create a new deterministic depth-20 overlap dataset or smoke subset under a
   separate dataset id.
2. Try `timeout_ms=10000` and `jobs=4` first.
3. If timeouts persist, try `jobs=2`.
4. Proceed to the full 5000-position depth-20 overlap only when the smoke is
   clean.
5. If depth 20 is still unstable, proceed to pattern-table v0 only with the
   caveat that midgame high-depth validation remains incomplete.

There is no committed reusable subset-selection script dedicated to extracting
this overlap from an existing teacher dataset. Until one is added, create the
depth-20 `source/positions.txt` as a local deterministic subset under the
external dataset root, preserving board9 text exactly and recording the
selection manifest locally.

## Prepared Depth 20 Commands

These commands are prepared for a future run. Do not run them until a
depth-20 smoke subset is clean.

```sh
export VIBE_OTHELLO_DATASET_ROOT="/path/to/vibe-othello-datasets"
export NTEST_BIN="/path/to/ntest"
export NTEST_WORKDIR="/path/to/ntest-runtime-nobook"
export BUILD_DIR="build"
export DEPTH20_ID="ntest-balanced300k-v0-highdepth20-overlap-v0"
export DEPTH20_TEACHER_NAME="ntest20-local"
export DEPTH20_TIMEOUT_MS="10000"
export DEPTH20_JOBS="4"
```

Smoke-first expectation:

- create a small deterministic board9 subset under
  `teacher/$DEPTH20_ID/source/smoke-positions.txt`
- preserve source board9 text exactly
- include opening-ish, midgame, and late/endgame positions
- write a local manifest with source dataset id, selection seed, target count,
  actual count, empties distribution, and output hash
- run a small `teacher_dataset_build.py` job against that smoke file before the
  full 5000-position overlap

Run the full depth-20 overlap only after the smoke is clean:

```sh
python3 tools/scripts/teacher_dataset_build.py \
  --dataset-id "$DEPTH20_ID" \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --positions "$VIBE_OTHELLO_DATASET_ROOT/teacher/$DEPTH20_ID/source/positions.txt" \
  --split-seed 20260602 \
  --split-ratios 70,15,15 \
  --shard-size 1000 \
  --teacher-adapter ntest \
  --teacher-protocol nboard \
  --teacher-depth 20 \
  --teacher-timeout-ms "$DEPTH20_TIMEOUT_MS" \
  --teacher-engine-name "$DEPTH20_TEACHER_NAME" \
  --teacher-workdir "$NTEST_WORKDIR" \
  --label-jobs "$DEPTH20_JOBS" \
  --position-log-mode failures \
  --teacher-engine-lifecycle persistent \
  --legal-validator "$BUILD_DIR/othello_validate_move" \
  --resume \
  --allow-failures \
  --teacher-engine-cmd -- "$NTEST_BIN" x
```
