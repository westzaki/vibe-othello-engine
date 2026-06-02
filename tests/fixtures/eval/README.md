# Eval Test Fixtures

These files are test fixtures only. They are intentionally tiny and are not
runtime presets, strength candidates, promotion evidence, or default
evaluators.

- `minimal_pattern_eval.eval` exercises `.eval` parsing, pattern-table loading,
  and relative `pattern_table` path resolution from a focused fixture config.
- `minimal_pattern_table.tsv` exercises sparse pattern-table parsing with a few
  deterministic non-zero entries.

Keep these fixtures small. Active learned artifacts belong under `data/eval/`;
generated experiment outputs belong under `runs/`.
