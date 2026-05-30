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
