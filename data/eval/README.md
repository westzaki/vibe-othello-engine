# Evaluation Configs

This directory contains the small source-controlled set of `.eval` v1 files
that should work on the latest `main`: the project default and promoted source
preset. These files are selected explicitly with
`--eval-config PATH` or `eval_config=PATH`, and repo tools also load
`current_default.eval` as the project default when `--eval-config` is omitted.
Use `.eval` configs for new evaluation experiments.

## Roles

### Built-In Fallback

`default_evaluation_config()` returns the file-free C++ built-in fallback. It
is safe for public library and core use, and it does not load `.eval` files or
source-controlled learned tables.

The built-in fallback remains available to C++ callers and tests. It may
intentionally differ from the project default evaluator when the project
default uses source-controlled learned tables.

### Project Default

`current_default.eval` is the source-controlled project default evaluator.
Repository tools load it when `--eval-config` is omitted. It may reference
source-controlled learned tables through the `.eval` loader.

The current project default is promoted from `ntest_pairwise_full_v2.eval`. It
keeps the lightweight scalar fallback unchanged in C++ and adds phase-specific
learned pattern-table deltas when repo tools load `current_default.eval`.

Changing `current_default.eval` does not automatically change the C++ built-in
fallback unless a caller loads the file. Default promotion must update this
project-default file, the tests that define the expected default-selection
policy, and any docs/evidence in the same PR.

Default promotion is separate from research. It requires strong evidence from
correctness checks, exact-label diagnostics, search benchmarks, and match or
base/head validation.

### Explicit Evaluator Preset

Any active `.eval` file can also be selected explicitly with
`--eval-config PATH` or `eval_config=PATH`. Explicit presets are useful for
comparison, reproduction, and reversible evaluator candidates.

### Promoted NTest Pairwise Default

`ntest_pairwise_full_v2.eval` is the explicit source preset for the promoted
project default. It was trained from the NTest 300K regularized pairwise loop,
uses the built-in scalar fallback as its scalar anchor, and adds phase-specific
learned pattern-table deltas. `current_default.eval` now resolves to an
equivalent evaluation config for repo tools that omit `--eval-config`.

The validation report is
[`docs/experiments/ntest_balanced300k_regularized_pairwise_full_v2_report.md`](../../docs/experiments/ntest_balanced300k_regularized_pairwise_full_v2_report.md).
The promoted config is favorable in the recorded deterministic matches and
improves exact sign metrics, but exact-best rank metrics regress slightly and
search overhead increases. Specifically, exact-best top group regresses from
5166 to 5121, exact-best rank sum regresses from 19602 to 19790, and recorded
search overhead increases by nodes +34.73% and elapsed +16.70%.

This promotion is not a formal Elo claim, and NTest teacher agreement is not
exact truth.

## Active Artifact Audit

This audit records the intended status of each source-controlled eval artifact
after the NTest pairwise full v2 project-default promotion.

Historical experiment reports may mention older meanings of `current_default`.
Treat those references as timestamped evidence, not current policy.

No active artifact is currently classified as a promotion candidate. The former
`ntest_pairwise_full_v2` candidate is now promoted and retained as the named
source preset for the project default.

Legacy pattern-teacher artifacts were pruned from this directory after active
parser, evaluation, match-runner, and script smoke tests moved to focused
fixtures under `tests/fixtures/eval`. Historical experiment reports may still
mention `pattern_teacher_v0.eval`, `pattern_reboot_v0.eval`, or
`patterns/pattern_teacher_v0.tsv` as timestamped provenance, but those paths are
not active source-controlled eval artifacts and should not be used for new work.

| Artifact | Classification | Current references | Next action |
| --- | --- | --- | --- |
| `current_default.eval` | current default snapshot | Loaded by repo tools when `--eval-config` is omitted through `tools/common/evaluator_selection.cpp`; used by CMake tool smoke tests, eval config tests, match-runner tests, and current docs. | Keep. This is the project-default pointer. Future default changes should update this file and the default-selection tests in the same PR. |
| `ntest_pairwise_full_v2.eval` | current default snapshot source; required test fixture | Explicit source preset for `current_default.eval`; loaded by eval config tests; referenced by the full-v2 experiment reports and promotion docs. | Keep. It is the named source artifact that makes the promoted default reproducible and reviewable. |
| `patterns/ntest_pairwise_full_v2_opening.tsv` | current default snapshot table; required runtime/test fixture | Referenced by `current_default.eval`, `ntest_pairwise_full_v2.eval`, eval config tests, and full-v2 experiment docs. | Keep. Required for the current project default and source preset. |
| `patterns/ntest_pairwise_full_v2_midgame.tsv` | current default snapshot table; required runtime/test fixture | Referenced by `current_default.eval`, `ntest_pairwise_full_v2.eval`, eval config tests, and full-v2 experiment docs. | Keep. Required for the current project default and source preset. |
| `patterns/ntest_pairwise_full_v2_late.tsv` | current default snapshot table; required runtime/test fixture | Referenced by `current_default.eval`, `ntest_pairwise_full_v2.eval`, eval config tests, and full-v2 experiment docs. | Keep. Required for the current project default and source preset. |

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

Scalar comparison evidence is historical provenance, not active evaluator data.
Reports such as
`docs/experiments/evaluation/2026-05-31-teacher-aggressive-v3.md` preserve the
commands, fingerprints, and comparison results that motivated the pattern-first
pivot without keeping scalar experiment anchors loadable as current configs.
If a scalar project-default revert is needed, regenerate a scalar `.eval`
snapshot from `default_evaluation_config()` in that revert PR, update the
project-default file and tests, and rerun eval config plus search smoke checks.

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
this form for `current_default.eval` or configs that need to describe the whole
evaluator state without implicit feature-weight fallback.

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

New evaluation experiments should be represented as `.eval` files selected with
`--eval-config`.

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
