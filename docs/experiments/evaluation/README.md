# Evaluation Experiment Reports

This directory is an evidence archive, not a queue of active instructions.
Historical reports preserve commands, measurements, and negative results so
future work does not repeat stale evaluator candidates. Current guidance lives
in repository docs, source code, active `.eval` files, and the current task or
PR context.
Archived commands may use interfaces or helper workflows that no longer exist;
preserve them as provenance, but do not copy them as current instructions.

Use this index to decide how actionable a report is before following any
experiment notes. In particular, do not resurrect a rejected or superseded
candidate just because an old report describes next steps.

For new pattern-training experiments, use
`tools/scripts/regularized_pairwise_pattern_train.py` as the canonical entry
point and pass `--eval-config` explicitly. Older reports that used
`pattern_teacher_v0_train.py` or `phase_pattern_table_train.py` remain useful
for provenance and dataset migration evidence, but those scripts have been
removed from active tooling. Do not copy those historical command blocks into
new work.

Reports may also mention removed `data/eval/pattern_teacher_v0.eval`,
`data/eval/pattern_reboot_v0.eval`, or
`data/eval/patterns/pattern_teacher_v0.tsv` artifacts. Those paths are
historical provenance only; the loadable source-controlled artifacts were
pruned from active eval configs. Current work should use `current_default.eval`,
`ntest_pairwise_full_v2.eval`, or an explicit new config.

| Report | Status/category | One-line purpose | Current actionability |
| --- | --- | --- | --- |
| [`2026-06-01-pattern-teacher-v0.md`](2026-06-01-pattern-teacher-v0.md) | historical pattern provenance | First sparse pattern-table baseline, trained from validated NTest residuals. | Provenance only; the loadable artifact was pruned from active eval configs. |
| [`2026-06-01-pattern-reboot-v0.md`](2026-06-01-pattern-reboot-v0.md) | historical pattern provenance | Clean pattern-only reboot config with scalar handcrafted weights zeroed. | Provenance only; the loadable artifact was pruned from active eval configs. |
| [`2026-06-02-ntest-pairwise-full-v2-candidate.md`](2026-06-02-ntest-pairwise-full-v2-candidate.md) | promotion candidate | Source-controlled NTest 300K regularized pairwise evaluator candidate. | Review with the linked evidence and known risks; not automatic default promotion. |
| [`2026-05-31-teacher-aggressive-v3.md`](2026-05-31-teacher-aggressive-v3.md) | historical scalar provenance | Scalar teacher-aggressive run that motivates the pattern-first pivot. | Reference for comparison and provenance only; do not restart scalar tweaking from it or keep its config active. |
| [`2026-06-01-ntest-depth26-dataset-migration.md`](2026-06-01-ntest-depth26-dataset-migration.md) | dataset/evidence infrastructure | Migration record for the reusable 2027-row NTest depth-26 teacher dataset layout. | Use the dataset references and workflow notes; this is not evaluator-strength evidence. |
| [`2026-06-01-classic-pattern-v0.md`](2026-06-01-classic-pattern-v0.md) | dataset/evidence infrastructure | Broad classic sparse-pattern plumbing run over the 2027-row teacher data. | Keep as broad-pattern and dataset provenance; candidate is rejected as stronger evaluator. |
| [`2026-06-01-pattern-teacher-v1.md`](2026-06-01-pattern-teacher-v1.md) | transitional trainer provenance | Deterministic split and rank-aware pattern iteration after `pattern_teacher_v0`. | Use the split/rank lessons only; the trainer entry point has been removed from active tooling. |
| [`2026-06-01-phase-broad-v0.md`](2026-06-01-phase-broad-v0.md) | specialized transitional workflow | Phase-specific broad pattern-table workflow validation and negative candidate result. | Use workflow evidence only; the trainer entry point has been removed from active tooling. |
| [`2026-06-01-pairwise-pattern-v0.md`](2026-06-01-pairwise-pattern-v0.md) | canonical trainer foundation | Regularized pairwise trainer smoke on the reusable teacher dataset. | Use as objective/regularization diagnostic evidence and as the current trainer family; do not keep the generated v0 candidate. |
| [`2026-06-07-ntest-pattern-only-full-v0.md`](2026-06-07-ntest-pattern-only-full-v0.md) | canonical trainer foundation | PatternOnly candidate shape and rendering foundation before full training was feasible. | Use for PatternOnly render provenance only; full evidence is in the follow-up analysis report. |
| [`2026-06-07-ntest-pattern-only-full-v0-analysis.md`](2026-06-07-ntest-pattern-only-full-v0-analysis.md) | canonical trainer foundation / negative full candidate evidence | Batch root analysis and streaming cache enabled full NTest 300K PatternOnly training. | Use the batch/cache workflow evidence; do not promote the generated PatternOnly full v0 candidate. |
| [`2026-06-07-ntest-teacher-dataset-quality-diagnostic.md`](2026-06-07-ntest-teacher-dataset-quality-diagnostic.md) | dataset/evidence infrastructure | Data-quality diagnostics for NTest teacher 300K exact-aware PatternOnly follow-up. | Use the diagnostic commands and full-300K gate before further exact-aware training. |
| [`2026-06-07-pattern-symmetry-diagnostic.md`](2026-06-07-pattern-symmetry-diagnostic.md) | canonical trainer foundation / diagnostic | D4 and color-inversion symmetry audit for pattern specs, trainer pair features, and learned TSV entries. | Use before adding `--symmetry-augment` or table symmetrization; no trainer behavior changed. |
| [`2026-06-07-pattern-objective-modes.md`](2026-06-07-pattern-objective-modes.md) | experimental objective foundation | Pairwise, listwise, and exact-aware-listwise PatternOnly trainer objective modes with 10K positive and exact-overlap negative evidence. | Use as objective-shape evidence only; selected agreement and top tie rate improved, but exact-best rank remains unresolved and no default promotion is implied. |
| [`2026-06-07-pattern-listwise-speed.md`](2026-06-07-pattern-listwise-speed.md) | experimental objective infrastructure | Listwise feature cache, compact listwise training data, and 10K/50K timing evidence for exact-aware-listwise comparison runs. | Use for 50K+ PatternOnly objective comparisons; it preserves 10K metrics and does not imply default promotion. |
| [`2026-06-07-pattern-exact-best-rank-objective.md`](2026-06-07-pattern-exact-best-rank-objective.md) | experimental objective evidence | Exact-best rank-pressure objective for PatternOnly 50K comparisons against the PR318 baseline. | Use as mixed/negative evidence: trainer-side rank improves, but external avg exact-best rank and wrong-direction remain unresolved; do not promote the generated candidates. |
| [`2026-05-31-historical-scalar-eval-rollup.md`](2026-05-31-historical-scalar-eval-rollup.md) | historical negative evidence / superseded | Compressed archive for old scalar tuning iterations and early teacher-safe scalar follow-up. | Read only to avoid repeating rejected scalar candidates; not current tuning guidance. |

## Category Notes

- Historical pattern provenance reports describe pruned pattern-teacher
  baselines that remain useful only as timestamped evidence.
- Canonical trainer foundation reports document the current trainer family even
  when an individual generated candidate was rejected.
- Transitional trainer reports may contain workflow lessons, but their script
  entry points were removed from active tooling and are not a starting point for
  new pattern experiments.
- Dataset/evidence infrastructure reports explain reusable evidence plumbing,
  not evaluator promotion.
- Experimental objective foundation reports document trainer objective
  capabilities and diagnostics without claiming evaluator strength.
- Experimental objective infrastructure reports document measurement/runtime
  plumbing that makes those objective comparisons reproducible at larger scale.
- Historical negative evidence and superseded reports are archived to preserve
  conclusions while making clear that they are not plans.
