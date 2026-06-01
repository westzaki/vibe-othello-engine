# Exact Label Datasets

`othello_exact_label_dump` writes exact-solver teacher labels for existing board
positions as JSONL. The tool is intended for evaluation-function research and
validation, not for changing evaluator, search, or exact-solver semantics.

The exact score field is named `exact_score_side_to_move`. Positive values mean
the side to move wins by that many final discs. Negative values mean the side to
move loses by that many final discs. This matches the public exact solver result
for the root board and keeps pass positions normalized to the original root
side-to-move perspective.

Example:

```sh
./build/othello_exact_label_dump \
  --input data/positions/exact_label_smoke.txt \
  --output runs/exact-labels/smoke.jsonl \
  --max-empties 14 \
  --limit 10 \
  --include-move-scores
```

The input file uses the existing 9-line board format accepted by
`board_from_string`: eight board rows followed by `side=B` or `side=W`. Multiple
positions may be listed in one file. Blank lines and `#` comments are ignored.

Output records use schema `exact_label.v1` and include:

- board text and side to move
- occupied and empty counts
- legal root moves, with root pass represented as `PASS`
- exact final disc margin from the side-to-move perspective
- deterministic best move and best move list
- optional per-root-move exact scores with `--include-move-scores`
- elapsed milliseconds and exact-solver node count for label generation

Keep one-off generated datasets under `runs/`; they should not be committed.
Reusable teacher/exact artifacts should be copied to an external dataset root
with a manifest, then referenced through `docs/datasets/README.md` conventions.
Durable summaries may go under `docs/perf/baselines/` when they are small,
clearly caveated, and useful as historical evidence.

The committed `data/positions/evaluation/diagnostic_suite.txt` file is the
source of truth for the `othello_search_bench --positions evaluation` diagnostic
set. It can also be used as a small reusable input when evaluator candidates
need exact-label or eval-vs-exact smoke evidence:

```sh
python3 tools/scripts/exact_label_workflow.py \
  --build-dir build \
  --out runs/exact-label-workflow/evaluation-diagnostic \
  --skip-sampling \
  --positions data/positions/evaluation/diagnostic_suite.txt \
  --max-empties 14 \
  --eval-preset default \
  --analyze
```

The workflow still writes labels and reports under `runs/`; do not commit the
generated JSONL. With the conservative `--max-empties 14` cap, positions in the
suite above that cap may be skipped. That is expected for smoke evidence; raise
the cap only when the local validation budget supports solving more positions.

Use tiny fixtures and smoke files for committed tests and examples. For larger
local datasets, choose `--max-empties` and `--limit` based on the current
machine, solver performance, and validation budget. Record the command, source
SHA, and caveats with any durable summary.

The default `--max-empties 14` is a conservative safety cap; positions above the
cap are skipped with a summary instead of solved.

## Sampling Workflow

`othello_position_sampler` creates reproducible random-playout board positions
in the same 9-line format accepted by `othello_exact_label_dump`. It starts from
the initial Othello position, plays random legal moves using the existing C++
rule engine, handles legal passes, and writes board positions only. It does not
write exact labels and does not use an evaluator.

Example:

```sh
./build/othello_position_sampler \
  --output runs/positions/sample_empties_8_10_12.txt \
  --count 20 \
  --target-empties 8,10,12 \
  --seed 20260531
```

Use `--unique true|false` to control board+side deduplication. The default is
`true`. If the sampler cannot produce enough positions within its attempt
budget, it fails instead of silently writing fewer positions.

For the full sampling-to-labeling workflow, use the Python orchestrator:

```sh
python3 tools/scripts/exact_label_workflow.py \
  --build-dir build \
  --out runs/exact-label-workflow/smoke \
  --count 20 \
  --target-empties 8,10,12 \
  --seed 20260531 \
  --max-empties 14 \
  --eval-preset default \
  --analyze
```

The workflow writes:

- sampled positions as `positions.txt`
- exact-label JSONL as `labels.jsonl`
- optional eval-vs-exact Markdown as `eval-vs-exact.md`
- command logs under `logs/`
- a `workflow.md` summary with metadata, exact commands, counts, and caveats

Random playout samples are reproducible smoke and teacher-data inputs, not a
representative Othello training distribution. Exact labels are final disc
margins from the side-to-move perspective. Eval-vs-exact scores are heuristic
units and must not be interpreted as disc margins.

Keep raw workflow output under `runs/`; do not commit generated datasets. Move
datasets that need to survive across worktrees to an external dataset root and
record their manifest outside git. Durable summaries should include the command,
source SHA, input labels, selected evaluator or config, and caveats. This
workflow is an analysis and data generation aid only: it makes no strength
claim and does not imply automatic default evaluator promotion.

## Eval-vs-Exact Analysis

`othello_eval_vs_exact` compares exact-label JSONL against one selected
evaluator. It is an analysis tool only: it does not tune weights, promote a
default, change exact-solver behavior, or make a strength claim.

Example:

```sh
./build/othello_eval_vs_exact \
  --labels runs/exact-labels/smoke.jsonl \
  --output runs/eval-vs-exact/smoke-default.md \
  --eval-preset default \
  --high-confidence-threshold 250 \
  --phase-breakdown \
  --include-positions
```

For file-based configs, use exactly one `--eval-config` instead of
`--eval-preset`:

```sh
./build/othello_eval_vs_exact \
  --labels runs/exact-labels/smoke.jsonl \
  --output runs/eval-vs-exact/smoke-current-default.md \
  --eval-config data/eval/current_default.eval
```

The report focuses on sign agreement, wrong-direction cases, empties buckets,
optional evaluator phase buckets, and high-confidence disagreements. Exact
labels are final disc margins, while evaluator scores are heuristic units. Raw
score differences are therefore uncalibrated heuristic-vs-disc comparisons and
must not be reported as disc-margin MAE.

When labels were generated with `--include-move-scores`, pass
`--move-rank-analysis` to add a root move-quality diagnostic. This evaluates
each legal root child from the original side-to-move perspective, ranks moves by
the evaluator score, and reports how highly exact-best moves rank plus cases
where the evaluator top score group does not contain an exact-best move.
Evaluator scores can tie, so top-move hit reporting uses the full top score
group; the report may still show one deterministic selected top move for
inspection. This is root-child move-quality diagnostic evidence for evaluation
work; it is not Elo, a tuner, a promotion gate, a strength claim, or an
automatic default recommendation.

Evaluator workflow scripts that call `othello_eval_vs_exact` can pass their own
`--move-rank-analysis` option through to the analyzer. In those reports,
records without `move_scores` are preserved as caveats rather than workflow
failures.

The high-confidence disagreement threshold defaults to `250` heuristic units.
Tune `--high-confidence-threshold` for the evaluator scale and validation goal
of the current run. The v1 analyzer is fail-fast for malformed or semantically
invalid records, so invalid rows abort the run instead of being counted as
skipped records.

Keep raw analyzer outputs under `runs/`. Durable summaries may go under
`docs/perf/baselines/` when they include the command, source SHA, input labels,
selected evaluator, and caveats. The next step after this report is an
analysis or tuning experiment, not an automatic default promotion.
