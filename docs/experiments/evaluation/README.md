# Evaluation Experiment Reports

This directory is an evidence archive, not a queue of active instructions.
Historical reports preserve commands, measurements, and negative results so
future work does not repeat stale evaluator candidates. Current guidance lives
in repository docs, source code, active `.eval` files, and the current task or
PR context.

Use this index to decide how actionable a report is before following any
experiment notes. In particular, do not resurrect a rejected or superseded
candidate just because an old report describes next steps.

| Report | Status/category | One-line purpose | Current actionability |
| --- | --- | --- | --- |
| [`2026-06-01-pattern-teacher-v0.md`](2026-06-01-pattern-teacher-v0.md) | current foundation | First retained sparse pattern-table baseline, trained from validated NTest residuals. | Use as the retained pattern comparison baseline; not a default-promotion signal. |
| [`2026-06-01-pattern-reboot-v0.md`](2026-06-01-pattern-reboot-v0.md) | current foundation | Clean pattern-only reboot config with scalar handcrafted weights zeroed. | Use as an interpretable pattern-learning starting point; expect weak strength. |
| [`2026-05-31-teacher-aggressive-v3.md`](2026-05-31-teacher-aggressive-v3.md) | current foundation | Scalar teacher-aggressive run that motivates the pattern-first pivot and remains a retained scalar comparison anchor. | Reference for comparison and provenance only; do not restart scalar tweaking from it. |
| [`2026-06-01-ntest-depth26-dataset-migration.md`](2026-06-01-ntest-depth26-dataset-migration.md) | dataset/evidence infrastructure | Migration record for the reusable 2027-row NTest depth-26 teacher dataset layout. | Use the dataset references and workflow notes; this is not evaluator-strength evidence. |
| [`2026-06-01-classic-pattern-v0.md`](2026-06-01-classic-pattern-v0.md) | dataset/evidence infrastructure | Broad classic sparse-pattern plumbing run over the 2027-row teacher data. | Keep as broad-pattern and dataset provenance; candidate is rejected as stronger evaluator. |
| [`2026-06-01-pattern-teacher-v1.md`](2026-06-01-pattern-teacher-v1.md) | active research line | Deterministic split and rank-aware pattern iteration after `pattern_teacher_v0`. | Use the split/rank lessons; do not treat v1 as stronger than v0. |
| [`2026-06-01-phase-broad-v0.md`](2026-06-01-phase-broad-v0.md) | active research line | Phase-specific broad pattern-table workflow validation and negative candidate result. | Use workflow evidence for phase-aware training; do not promote the generated candidate. |
| [`2026-06-01-pairwise-pattern-v0.md`](2026-06-01-pairwise-pattern-v0.md) | active research line | Regularized pairwise trainer smoke on the reusable teacher dataset. | Use as objective/regularization diagnostic evidence; do not keep the generated candidate. |
| [`2026-05-31-historical-scalar-eval-rollup.md`](2026-05-31-historical-scalar-eval-rollup.md) | historical negative evidence / superseded | Compressed archive for old scalar tuning iterations and early teacher-safe scalar follow-up. | Read only to avoid repeating rejected scalar candidates; not current tuning guidance. |

## Category Notes

- Current foundation reports describe retained baselines or comparison anchors
  that current docs still mention.
- Active research line reports may contain negative results, but they also
  document currently relevant trainer, dataset, or pattern-table workflow
  lessons.
- Dataset/evidence infrastructure reports explain reusable evidence plumbing,
  not evaluator promotion.
- Historical negative evidence and superseded reports are archived to preserve
  conclusions while making clear that they are not plans.
