# Evaluation Configs

This directory contains fully expanded `.eval` v1 evaluator configs for tools
that accept `--eval-config PATH` or `eval_config=PATH`.

Format:

- UTF-8 text
- blank lines and `#` comments are allowed
- one `key=value` per line
- ASCII whitespace around keys and values is ignored
- `name=...` is metadata only
- numeric values are signed integers
- unknown, duplicate, invalid, or missing required keys are errors

These files are data for experiments. They do not change the built-in default
evaluator unless a tool is explicitly run with the config file.

## Lifecycle Policy

Active configs live in this directory and are expected to work on the latest
`main`. Archive configs are historical evidence and are not guaranteed to work
on the latest `main`.

Active configs must be fully expanded. They should not rely on implicit default
overlays or missing-key fallback behavior.

Schema changes must update all active configs in the same pull request.
`current_default.eval` must match `default_evaluation_config()`.

Experimental configs should not stay active indefinitely. Promote a useful
experiment into a durable active config with current validation notes, or move
it to an archive when it no longer represents a current candidate.

Raw experiment outputs belong under `runs/`. Durable summaries and baselines
belong under `docs/perf/baselines/`.
