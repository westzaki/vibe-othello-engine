# Evaluation Configs

This directory contains the small set of `.eval` v1 files that should work on
the latest `main`. These files are selected explicitly with `--eval-config PATH`
or `eval_config=PATH`; they do not change the built-in engine default.
Use `.eval` configs for new evaluation experiments. `--eval-preset` remains
available for built-in compatibility names and smoke checks, not as the normal
place to publish new research candidates.

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
- optional `schema_version=eval.v1` is metadata and is validated when present
- `name=...` is metadata only
- optional `mode=pattern_only` enables compact learned-table experiments
- numeric values are signed integers
- unknown, duplicate, or invalid keys are errors
- `pattern_table=...` paths are resolved relative to the `.eval` file and
  remain the global compatibility mode for using one table in every phase
- optional `pattern_table.opening=...`, `pattern_table.midgame=...`, and
  `pattern_table.late=...` paths select phase-specific learned tables
- when a phase-specific table is absent, that phase falls back to
  `pattern_table=...` when present
- `opening.pattern_table`, `midgame.pattern_table`, and `late.pattern_table`
  are weights for the learned `PatternTableBundle` score
- `legacy_corner_2x3_rule` and `legacy_edge_8_rule` are aliases for the old
  handcrafted pattern-rule features formerly written as `corner_2x3_pattern`
  and `edge_8_pattern`

For new configs, use `pattern_table` for learned pattern-table experiments.
Use `legacy_corner_2x3_rule` and `legacy_edge_8_rule` only for compatibility
snapshots, comparisons, or narrow ablations. The old `corner_2x3_pattern` and
`edge_8_pattern` keys are still accepted so existing configs keep loading, but a
config must not specify both the old key and its legacy alias for the same phase.

### Full Snapshot Configs

Plain `eval.v1` configs without `mode=pattern_only` are full snapshots. They
must explicitly list every handcrafted feature weight and phase threshold. Use
this form for compatibility snapshots such as `current_default.eval`, retained
scalar anchors, or configs that need to describe the whole evaluator state
without implicit feature-weight fallback.

### Pattern-Only Configs

`mode=pattern_only` is for learned pattern table experiments. In this mode,
omitted feature weights default to zero, so a compact experiment can usually
list only:

- `pattern_table=...` or phase-specific `pattern_table.opening`,
  `pattern_table.midgame`, and `pattern_table.late` paths
- `opening.pattern_table`, `midgame.pattern_table`, and `late.pattern_table`
  weights
- optional `opening_max_occupied` and `midgame_max_occupied` thresholds

If the thresholds are omitted in `mode=pattern_only`, the loader keeps the
built-in default phase thresholds. Non-pattern feature keys are still accepted
for narrow ablations or transitional experiments, but pattern-only configs
should normally omit them.

Parser smoke fixtures for `mode=pattern_only` belong under test fixtures rather
than this active config directory. A pattern-only config without any learned
table path is not a playable experiment or strength candidate.

New evaluation experiments should be represented as `.eval` files rather than
new C++ `EvaluationPreset` entries unless the behavior is intended to become a
stable public compatibility surface.

Pattern table storage is separate from scalar evaluator configuration. Sparse
TSV files remain the source and review format, but loading expands them into a
shared dense `PatternTableBundle` for runtime lookup. Scalar-only configs keep
the no-table case explicit and cheap. Phase-specific keys use the same TSV
format and the same dense runtime representation; numeric
`opening.pattern_table`, `midgame.pattern_table`, and `late.pattern_table`
weights still control each phase's contribution.

The legacy handcrafted pattern-rule feature weights are scalar evaluator
features, not learned table weights. In config files, prefer
`opening.legacy_corner_2x3_rule`, `opening.legacy_edge_8_rule`, and the matching
`midgame.*` / `late.*` keys when a retained comparison needs those old features.

Binary `.ptab` loading, manifests/checksums, compact runtime payloads, and
phase-specific table manifests are future work. Do not add those concerns to
`.eval` files until the corresponding runtime loader exists.

Raw experiment outputs belong under `runs/`. Raw teacher labels, exact labels,
match JSONL, benchmark logs, local NTest paths, and local absolute paths should
not be committed here.
