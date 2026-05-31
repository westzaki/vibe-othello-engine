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
