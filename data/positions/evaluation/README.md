# Evaluation Diagnostic Positions

This directory contains small, curated board-position inputs for evaluator
diagnostics. They are intended for repeated analysis of evaluator candidates,
not for proving playing strength.

`diagnostic_suite.txt` uses the existing 9-line board format accepted by
`board_from_string`: eight board rows followed by `side=B` or `side=W`. Blank
lines and `#` comments are ignored by existing exact-label tooling.

Keep this suite small and semantic. Generated samples and exact-label outputs
belong under `runs/`.
