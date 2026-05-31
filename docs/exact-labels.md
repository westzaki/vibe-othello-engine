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

Keep generated datasets under `runs/`; they should not be committed. Durable
summaries may go under `docs/perf/baselines/` when they are small, clearly
caveated, and useful as historical evidence.

Use tiny fixtures and smoke files for committed tests and examples. For larger
local datasets, choose `--max-empties` and `--limit` based on the current
machine, solver performance, and validation budget. Record the command, source
SHA, and caveats with any durable summary.

The default `--max-empties 14` is a conservative safety cap; positions above the
cap are skipped with a summary instead of solved.

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

The high-confidence disagreement threshold defaults to `250` heuristic units.
Tune `--high-confidence-threshold` for the evaluator scale and validation goal
of the current run. The v1 analyzer is fail-fast for malformed or semantically
invalid records, so invalid rows abort the run instead of being counted as
skipped records.

Keep raw analyzer outputs under `runs/`. Durable summaries may go under
`docs/perf/baselines/` when they include the command, source SHA, input labels,
selected evaluator, and caveats. The next step after this report is an
analysis or tuning experiment, not an automatic default promotion.
