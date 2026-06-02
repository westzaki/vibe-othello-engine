# NTest Balanced 300K v0 Completion Report

This report records the first reusable local NTest teacher dataset completed for
pattern-eval training. The dataset artifacts live under an external dataset
root and are not committed to this repository.

This is teacher evidence for training. It is not exact truth, not a playing
strength claim, and not evidence for promoting evaluator defaults.

## Purpose

`ntest-balanced300k-v0` provides a reusable set of balanced board9 positions
with NTest teacher moves for training and evaluating pattern-eval experiments.
The run was designed to verify the durable pipeline:

- balanced board9 source positions
- deterministic 70/15/15 split
- sharded labels
- resume-safe teacher labeling
- legal-move validation
- QC summaries and explicit failure accounting

## Generation Settings

| field | value |
| --- | --- |
| dataset id | `ntest-balanced300k-v0` |
| generation repo SHA | `a8a4df6cc82119b9d164ec7ca257175e97c02612` |
| generation commit | `feat: add persistent NTest teacher workers (#222)` |
| teacher | `ntest12-local` |
| adapter/protocol | `ntest` / `nboard` |
| engine lifecycle | `persistent` |
| NTest book | disabled through a local no-book runtime |
| depth | `12` |
| timeout | `5000 ms` |
| jobs | `4` |
| split seed | `20260601` |
| split ratios | `70/15/15` |
| shard size | `1000` |
| position pool | reused existing matching pool |
| full run exit code | `0` |
| repository status after generation | clean |

## Smoke Results

All smoke runs used persistent NBoard workers and completed cleanly before the
full run was started.

| depth | action | requested | ok | failed | timed out | illegal moves | labels/sec | estimated 300K time |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 10 | OK for overnight | 1200 | 1200 | 0 | 0 | 0 | 108.6155 | 46m 2s |
| 12 | OK for overnight | 1200 | 1200 | 0 | 0 | 0 | 76.3765 | 1h 5m 28s |
| 14 | OK for overnight | 1200 | 1200 | 0 | 0 | 0 | 53.7627 | 1h 33m 0s |

Depth 12 was selected for the first full dataset because it matched the
runbook default and passed the smoke gate. Depth 14 was operationally viable in
the smoke run, but it was not selected automatically.

## Full Run Summary

| field | value |
| --- | ---: |
| generated unique positions | 300000 |
| duplicates | 0 |
| position shards | 300 |
| label shards | 300 |
| labels requested | 300000 |
| labels ok | 299913 |
| labels usable | 299913 |
| labels failed | 87 |
| illegal teacher moves | 0 |
| invalid move tokens | 83 |
| timed-out failures | 4 |
| exact labels generated | 0 |

## Split Counts

| split | positions |
| --- | ---: |
| train | 209445 |
| validation | 45348 |
| holdout | 45207 |

## Failure Summary

There were 87 failed rows total:

- 83 rows: engine produced no valid move
- 4 rows: engine timed out

All failed rows are recorded in `labels/ntest12-local/failed.jsonl`. Failed
rows are not usable labels.

Training jobs must use only rows where:

- `status == "ok"`
- `label_usable == true`

## Artifact Locations

Paths below are relative to the external dataset root.

| artifact | dataset-relative path |
| --- | --- |
| dataset root | `teacher/ntest-balanced300k-v0` |
| position source | `teacher/ntest-balanced300k-v0/source/positions.txt` |
| dataset manifest | `teacher/ntest-balanced300k-v0/manifest.json` |
| dataset card | `teacher/ntest-balanced300k-v0/dataset_card.md` |
| QC summary | `teacher/ntest-balanced300k-v0/qc/summary.json` |
| phase distribution | `teacher/ntest-balanced300k-v0/qc/phase_distribution.tsv` |
| duplicate report | `teacher/ntest-balanced300k-v0/qc/duplicate_report.tsv` |
| legality summary | `teacher/ntest-balanced300k-v0/qc/legality_summary.json` |
| failure table | `teacher/ntest-balanced300k-v0/qc/failures.tsv` |
| label shards | `teacher/ntest-balanced300k-v0/labels/ntest12-local/shards/labels-*.jsonl` |
| failed rows | `teacher/ntest-balanced300k-v0/labels/ntest12-local/failed.jsonl` |
| workflow reports | `teacher/ntest-balanced300k-v0/labels/ntest12-local/workflow-shards/shard-*/workflow.md` |

## Caveats

- NTest labels are teacher evidence, not exact game-theoretic truth.
- This report makes no playing-strength claim.
- This dataset is not evaluator-default promotion evidence.
- Exact or higher-depth overlap is still needed before trusting learned tables.
- The 87 failed rows must remain excluded from training inputs.

## Recommended Next Steps

1. Inspect `labels/ntest12-local/failed.jsonl` and the corresponding workflow
   logs to confirm the failed rows are understood.
2. Build a separate exact or high-depth overlap dataset.
3. Train a pattern table v0 using only usable NTest rows.
4. Evaluate the learned table on validation, holdout, exact overlap, and search
   benchmark checks before drawing strength conclusions.
