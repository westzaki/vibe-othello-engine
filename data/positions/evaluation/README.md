# Evaluation Diagnostic Positions

This directory contains small, curated board-position inputs for evaluator
diagnostics. They are intended for repeated analysis of evaluator candidates,
not for proving playing strength.

`diagnostic_suite.txt` is the source of truth for the evaluation diagnostic
position set. `othello_search_bench --positions evaluation` loads this committed
suite, so update this file when adding or editing evaluation diagnostic
positions.

The file uses a small metadata comment block before each position:

- `# name:` stable position id
- `# phase:` broad phase bucket used in benchmark summaries
- `# tags:` comma-separated diagnostic tags
- `# note:` short human-readable intent

Each board still uses the existing 9-line board format accepted by
`board_from_string`: eight board rows followed by `side=B` or `side=W`.

Keep this suite small and semantic. Generated samples and exact-label outputs
belong under `runs/`. The suite is diagnostic evidence only; it is not a
training distribution or a strength proof.
