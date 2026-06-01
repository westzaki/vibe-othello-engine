# Evaluation Configs

This directory contains the small set of `.eval` v1 files that should work on
the latest `main`. These files are selected explicitly with `--eval-config PATH`
or `eval_config=PATH`; they do not change the built-in engine default.

## Roles

### Engine Default

`current_default.eval` is the fully expanded snapshot of
`default_evaluation_config()`. It must stay synchronized with the built-in
default in the same PR as any intentional default evaluator change.

Default promotion is separate from research. It requires strong evidence from
correctness checks, exact-label diagnostics, search benchmarks, and match or
base/head validation.

### Pattern Research Baseline

`pattern_teacher_v0.eval` is the retained experimental pattern baseline for the
pattern-first research restart. It is not the engine default, not a C++
`EvaluationPreset`, and not a strength claim. It is kept because it is a compact
pattern-table baseline with durable experiment evidence.

`pattern_reboot_v0.eval` is the clean pattern-only reboot baseline. It reuses
the retained `pattern_teacher_v0` table, but zeros all scalar handcrafted
feature weights so future pattern learning can be interpreted independently
from scalar residual tuning. It is expected to be weaker initially and must not
be used as a strength candidate or default-promotion signal.

Pattern-first experiments may intentionally be weaker than the engine default
while they build better table ownership, dataset, trainer, and validation
foundations.

### Retained Comparison Anchor

`classic_othello_v3_teacher_aggressive.eval` is retained temporarily as a
scalar comparison anchor for pattern research. It is not a preferred candidate
and should not be used as a new scalar-tweaking baseline.

### Compatibility Fixture

`phase_aware_v1.eval` is retained as a compatibility snapshot for the public
`phase_aware_v1` preset. Public C++ presets are compatibility surface; do not
remove or rename them in cleanup PRs.

## Rejected or Superseded Configs

Rejected and superseded configs should normally be removed from `data/eval`
after their experiment report is merged. Keeping old `.eval` files active makes
future agents treat rejected candidates as current options.

Do not create an archive directory here by default. Durable reports under
`docs/experiments/evaluation/` are the archive. They preserve commands,
fingerprints, conclusions, and negative evidence without keeping stale configs
loadable as current candidates.

Keep a rejected config or table only when a current test, tool contract, or
explicit reproduction task truly needs the exact source-controlled file. If one
is kept, its header must say it is rejected or retained for reproducibility
only, and it must not be described as active or preferred.

## Format

- UTF-8 text
- blank lines and `#` comments are allowed
- one `key=value` per line
- ASCII whitespace around keys and values is ignored
- `name=...` is metadata only
- numeric values are signed integers
- unknown, duplicate, invalid, or missing required keys are errors
- `pattern_table=...` paths are resolved relative to the `.eval` file and
  remain the global compatibility mode for using one table in every phase
- optional `pattern_table.opening=...`, `pattern_table.midgame=...`, and
  `pattern_table.late=...` paths select phase-specific learned tables
- when a phase-specific table is absent, that phase falls back to
  `pattern_table=...` when present

Active configs must be fully expanded. They should not rely on implicit default
overlays or missing-key fallback behavior.

Pattern table storage is separate from scalar evaluator configuration. Sparse
TSV files remain the source and review format, but loading expands them into a
shared dense `PatternTableBundle` for runtime lookup. Scalar-only configs keep
the no-table case explicit and cheap. Phase-specific keys use the same TSV
format and the same dense runtime representation; numeric
`opening.pattern_table`, `midgame.pattern_table`, and `late.pattern_table`
weights still control each phase's contribution.

Binary `.ptab` loading, manifests/checksums, compact runtime payloads, and
compact runtime payloads are future work. Do not add those concerns to `.eval`
files until the corresponding runtime loader exists.

Raw experiment outputs belong under `runs/`. Raw teacher labels, exact labels,
match JSONL, benchmark logs, local NTest paths, and local absolute paths should
not be committed here.
