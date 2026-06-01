# NTest Teacher Dataset Runbook

This runbook describes the first reusable local NTest teacher dataset for
pattern-eval training. It is an operational workflow, not a strength claim and
not evaluator-default evidence by itself.

## Goal

Build a reusable local NTest teacher dataset with:

- about 300K balanced board9 positions
- deterministic 70/15/15 train, validation, and holdout split
- sharded teacher labels
- resume-safe overnight execution
- legal-move validation for teacher moves
- QC summaries and failure accounting
- no generated dataset committed to git

Keep the generated dataset under an external dataset root, not under
source-controlled `data/`.

## Local Variables

Set these variables in the shell that will run the commands:

```sh
export VIBE_OTHELLO_DATASET_ROOT="/path/to/vibe-othello-datasets"
export NTEST_BIN="/path/to/ntest"
export NTEST_WORKDIR="/path/to/ntest-resource-directory"
export BUILD_DIR="build"
export DATASET_ID="ntest-balanced300k-v0"
export DEPTH="12"
export TIMEOUT_MS="5000"
export TEACHER_NAME="ntest${DEPTH}-local"
export JOBS="<machine-dependent>"
```

Use `DEPTH=12` for the first full dataset unless the depth 10/12/14 smoke
results say otherwise. Choose `JOBS` from smoke throughput and failure behavior,
not from CPU count alone.

## Build Commands

Configure and build the C++ tools in Release mode:

```sh
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure
```

Optional Python test pass, when `pytest` is installed locally:

```sh
python3 -m pytest tools/scripts/tests
```

Do not add NTest to CI. NTest remains a local external engine.

## Position Pool

Create the reusable balanced board9 source pool with the C++
`othello_position_sampler` orchestrated by `balanced_position_pool.py`.

```sh
BUCKET_SPEC="52:4000,51:4000,48:5000,47:5000,44:7000,43:7000,40:10000,39:10000,36:14000,35:14000,32:17000,31:17000,28:18000,27:18000,24:18000,23:18000,20:16000,19:16000,16:14000,15:14000,14:11000,13:11000,12:9000,11:9000,10:4500,9:4500,8:2500,7:2500"

python3 tools/scripts/balanced_position_pool.py \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --output "teacher/$DATASET_ID/source/positions.txt" \
  --bucket-spec "$BUCKET_SPEC" \
  --seed 20260601 \
  --sampler "$BUILD_DIR/othello_position_sampler" \
  --max-attempts-per-bucket 20000000
```

Expected local artifacts:

- `$VIBE_OTHELLO_DATASET_ROOT/teacher/$DATASET_ID/source/positions.txt`
- `$VIBE_OTHELLO_DATASET_ROOT/teacher/$DATASET_ID/source/manifest.json`
- `$VIBE_OTHELLO_DATASET_ROOT/teacher/$DATASET_ID/source/qc/phase_distribution.tsv`
- `$VIBE_OTHELLO_DATASET_ROOT/teacher/$DATASET_ID/source/qc/duplicate_report.tsv`
- `$VIBE_OTHELLO_DATASET_ROOT/teacher/$DATASET_ID/source/commands.sh`

Do not commit these generated files.

## Smoke Runs

Run all three smoke depths before the full overnight run. The smoke helper
generates or reuses a small balanced position pool, calls
`teacher_dataset_build.py`, measures throughput, and writes a report under
`runs/ntest-teacher-smoke/<timestamp>/report.md`.

Use `--teacher-engine-lifecycle persistent` for the overnight candidate smoke
so each worker keeps one NTest NBoard process alive across positions. The
default `per-request` lifecycle remains useful as a baseline, but it starts a
new NTest process for every position and may be too slow for 300K labels.

Depth 10:

```sh
python3 tools/scripts/ntest_teacher_smoke.py \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --dataset-id "$DATASET_ID-smoke-d10" \
  --ntest-workdir "$NTEST_WORKDIR" \
  --depth 10 \
  --timeout-ms "$TIMEOUT_MS" \
  --jobs "$JOBS" \
  --position-log-mode failures \
  --teacher-engine-lifecycle persistent \
  --sampler "$BUILD_DIR/othello_position_sampler" \
  --legal-validator "$BUILD_DIR/othello_validate_move" \
  --seed 20260601 \
  --bucket-spec smoke \
  --ntest-cmd -- "$NTEST_BIN" x
```

Depth 12:

```sh
python3 tools/scripts/ntest_teacher_smoke.py \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --dataset-id "$DATASET_ID-smoke-d12" \
  --ntest-workdir "$NTEST_WORKDIR" \
  --depth 12 \
  --timeout-ms "$TIMEOUT_MS" \
  --jobs "$JOBS" \
  --position-log-mode failures \
  --teacher-engine-lifecycle persistent \
  --sampler "$BUILD_DIR/othello_position_sampler" \
  --legal-validator "$BUILD_DIR/othello_validate_move" \
  --seed 20260601 \
  --bucket-spec smoke \
  --ntest-cmd -- "$NTEST_BIN" x
```

Depth 14:

```sh
python3 tools/scripts/ntest_teacher_smoke.py \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --dataset-id "$DATASET_ID-smoke-d14" \
  --ntest-workdir "$NTEST_WORKDIR" \
  --depth 14 \
  --timeout-ms "$TIMEOUT_MS" \
  --jobs "$JOBS" \
  --position-log-mode failures \
  --teacher-engine-lifecycle persistent \
  --sampler "$BUILD_DIR/othello_position_sampler" \
  --legal-validator "$BUILD_DIR/othello_validate_move" \
  --seed 20260601 \
  --bucket-spec smoke \
  --ntest-cmd -- "$NTEST_BIN" x
```

Interpret the smoke report before continuing:

- `labels_per_second`: requested labels divided by measured wall-clock time.
  Use this to estimate whether 300K labels fit the overnight window.
- `estimated_time_for_300k`: full-run ETA at the observed rate. Treat an ETA
  over the overnight window as a reason to lower depth or reduce operational
  risk before the full run.
- failure rate: `failed_labels / requested_labels`. Any unexpected non-zero
  failure rate needs inspection, even when `--allow-failures` lets the command
  finish.
- illegal move rate: `illegal_teacher_moves / requested_labels`. Any non-zero
  illegal move count is an abort condition.
- timeout rate: `timed_out_labels / requested_labels`. High timeout rates mean
  the configured depth, timeout, jobs, or NTest working directory is not ready
  for the full run.

Use the report's `recommended_action` as the first operational gate. Continue
only when the smoke output is clean enough for the target overnight window.

To compare lifecycle overhead at depth 10, run the same smoke twice with
different dataset IDs:

```sh
python3 tools/scripts/ntest_teacher_smoke.py \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --dataset-id "$DATASET_ID-smoke-d10-per-request" \
  --ntest-workdir "$NTEST_WORKDIR" \
  --depth 10 \
  --timeout-ms "$TIMEOUT_MS" \
  --jobs "$JOBS" \
  --position-log-mode failures \
  --teacher-engine-lifecycle per-request \
  --sampler "$BUILD_DIR/othello_position_sampler" \
  --legal-validator "$BUILD_DIR/othello_validate_move" \
  --seed 20260601 \
  --bucket-spec smoke \
  --ntest-cmd -- "$NTEST_BIN" x

python3 tools/scripts/ntest_teacher_smoke.py \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --dataset-id "$DATASET_ID-smoke-d10-persistent" \
  --ntest-workdir "$NTEST_WORKDIR" \
  --depth 10 \
  --timeout-ms "$TIMEOUT_MS" \
  --jobs "$JOBS" \
  --position-log-mode failures \
  --teacher-engine-lifecycle persistent \
  --sampler "$BUILD_DIR/othello_position_sampler" \
  --legal-validator "$BUILD_DIR/othello_validate_move" \
  --seed 20260601 \
  --bucket-spec smoke \
  --ntest-cmd -- "$NTEST_BIN" x
```

Record `labels_per_second`, `estimated_time_for_300k`, and
`recommended_action` from both reports. If persistent mode does not reach the
overnight target cleanly, do not start the 300K run.

## Full Overnight Run

Run the full label build after the position pool and smoke runs are acceptable.
This command records failures but keeps completed shards usable for inspection
and resume.

```sh
python3 tools/scripts/teacher_dataset_build.py \
  --dataset-id "$DATASET_ID" \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --positions "$VIBE_OTHELLO_DATASET_ROOT/teacher/$DATASET_ID/source/positions.txt" \
  --split-seed 20260601 \
  --split-ratios 70,15,15 \
  --shard-size 1000 \
  --teacher-adapter ntest \
  --teacher-protocol nboard \
  --teacher-depth "$DEPTH" \
  --teacher-timeout-ms "$TIMEOUT_MS" \
  --teacher-engine-name "$TEACHER_NAME" \
  --teacher-workdir "$NTEST_WORKDIR" \
  --label-jobs "$JOBS" \
  --position-log-mode failures \
  --teacher-engine-lifecycle persistent \
  --legal-validator "$BUILD_DIR/othello_validate_move" \
  --resume \
  --allow-failures \
  --teacher-engine-cmd -- "$NTEST_BIN" x
```

`--allow-failures` is for preserving partial evidence from a long run. It does
not make failed rows acceptable for training.

## Resume Instructions

To resume an interrupted run, re-run the exact same full overnight command with
`--resume`.

Completed shards are files under:

```text
$VIBE_OTHELLO_DATASET_ROOT/teacher/$DATASET_ID/labels/$TEACHER_NAME/shards/
```

`teacher_dataset_build.py` treats a completed shard as resume-eligible only
when the shard file exists, has the expected row count, and contains
`teacher_label.v1` rows. Incomplete or invalid shards are regenerated.

Failure and QC files are written here:

```text
$VIBE_OTHELLO_DATASET_ROOT/teacher/$DATASET_ID/labels/$TEACHER_NAME/failed.jsonl
$VIBE_OTHELLO_DATASET_ROOT/teacher/$DATASET_ID/qc/summary.json
$VIBE_OTHELLO_DATASET_ROOT/teacher/$DATASET_ID/qc/legality_summary.json
$VIBE_OTHELLO_DATASET_ROOT/teacher/$DATASET_ID/qc/failures.tsv
```

Per-shard workflow reports and failure-only logs are under:

```text
$VIBE_OTHELLO_DATASET_ROOT/teacher/$DATASET_ID/labels/$TEACHER_NAME/workflow-shards/
```

## Abort Conditions

Abort the full run, or lower depth and repeat smoke, when any of these are true:

- smoke ETA exceeds the intended overnight window
- timeout rate is high enough to threaten label completeness
- `illegal_teacher_moves` is non-zero
- `invalid_move_tokens` is non-zero
- failure rate is unexpectedly high for the selected depth and timeout
- NTest fails to start from `NTEST_WORKDIR`
- `recommended_action` is not `OK for overnight`

Do not continue by increasing `--allow-failures` tolerance. Fix the operational
issue first.

## Output Locations

Main dataset output:

```text
$VIBE_OTHELLO_DATASET_ROOT/teacher/$DATASET_ID/
```

Important paths:

- positions source:
  `source/positions.txt`
- position pool manifest and commands:
  `source/manifest.json`, `source/commands.sh`
- reusable position shards:
  `positions/shards/positions-0000.jsonl`, ...
- deterministic split IDs:
  `splits/train.ids`, `splits/validation.ids`, `splits/holdout.ids`
- teacher labels:
  `labels/$TEACHER_NAME/shards/labels-0000.jsonl`, ...
- failed teacher rows:
  `labels/$TEACHER_NAME/failed.jsonl`
- label manifest:
  `labels/$TEACHER_NAME/manifest.json`
- workflow shard reports and logs:
  `labels/$TEACHER_NAME/workflow-shards/shard-0000/workflow.md`, ...
- dataset manifest and card:
  `manifest.json`, `dataset_card.md`
- QC summaries:
  `qc/summary.json`, `qc/phase_distribution.tsv`,
  `qc/duplicate_report.tsv`, `qc/legality_summary.json`,
  `qc/failures.tsv`
- replay command:
  `commands.sh`

## Follow-Up After the Full Run

After the full command finishes:

1. Inspect `qc/summary.json`, `qc/legality_summary.json`,
   `qc/phase_distribution.tsv`, `qc/duplicate_report.tsv`, and
   `labels/$TEACHER_NAME/failed.jsonl`.
2. Confirm the train, validation, and holdout counts match the intended
   deterministic 70/15/15 split.
3. Build a small exact or higher-depth overlap dataset separately. Keep that
   run isolated so it can validate a subset without changing this dataset.
4. Only then use the teacher labels for pattern-table training.
5. Do not promote evaluator defaults from this dataset alone. Treat any
   evaluator change as a separate measured experiment against stable baselines.
