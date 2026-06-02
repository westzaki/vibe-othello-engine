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
`tools/scripts/regularized_pairwise_pattern_train.py` by default. Older reports
that used `pattern_teacher_v0_train.py` or `phase_pattern_table_train.py`
remain useful for provenance and dataset migration evidence, but those scripts
have been removed from active tooling. Do not copy those historical command
blocks into new work.

| Report | Status/category | One-line purpose | Current actionability |
| --- | --- | --- | --- |
| [`2026-06-01-pattern-teacher-v0.md`](2026-06-01-pattern-teacher-v0.md) | current foundation | First retained sparse pattern-table baseline, trained from validated NTest residuals. | Use as the retained pattern comparison baseline; not a default-promotion signal. |
| [`2026-06-01-pattern-reboot-v0.md`](2026-06-01-pattern-reboot-v0.md) | current foundation | Clean pattern-only reboot config with scalar handcrafted weights zeroed. | Use as an interpretable pattern-learning starting point; expect weak strength. |
| [`2026-06-02-ntest-pairwise-full-v2-candidate.md`](2026-06-02-ntest-pairwise-full-v2-candidate.md) | promotion candidate | Source-controlled NTest 300K regularized pairwise evaluator candidate. | Review with the linked evidence and known risks; not automatic default promotion. |
| [`2026-05-31-teacher-aggressive-v3.md`](2026-05-31-teacher-aggressive-v3.md) | historical scalar provenance | Scalar teacher-aggressive run that motivates the pattern-first pivot. | Reference for comparison and provenance only; do not restart scalar tweaking from it or keep its config active. |
| [`2026-06-01-ntest-depth26-dataset-migration.md`](2026-06-01-ntest-depth26-dataset-migration.md) | dataset/evidence infrastructure | Migration record for the reusable 2027-row NTest depth-26 teacher dataset layout. | Use the dataset references and workflow notes; this is not evaluator-strength evidence. |
| [`2026-06-01-classic-pattern-v0.md`](2026-06-01-classic-pattern-v0.md) | dataset/evidence infrastructure | Broad classic sparse-pattern plumbing run over the 2027-row teacher data. | Keep as broad-pattern and dataset provenance; candidate is rejected as stronger evaluator. |
| [`2026-06-01-pattern-teacher-v1.md`](2026-06-01-pattern-teacher-v1.md) | transitional trainer provenance | Deterministic split and rank-aware pattern iteration after `pattern_teacher_v0`. | Use the split/rank lessons only; the trainer entry point has been removed from active tooling. |
| [`2026-06-01-phase-broad-v0.md`](2026-06-01-phase-broad-v0.md) | specialized transitional workflow | Phase-specific broad pattern-table workflow validation and negative candidate result. | Use workflow evidence only; the trainer entry point has been removed from active tooling. |
| [`2026-06-01-pairwise-pattern-v0.md`](2026-06-01-pairwise-pattern-v0.md) | canonical trainer foundation | Regularized pairwise trainer smoke on the reusable teacher dataset. | Use as objective/regularization diagnostic evidence and as the current trainer family; do not keep the generated v0 candidate. |
| [`2026-05-31-historical-scalar-eval-rollup.md`](2026-05-31-historical-scalar-eval-rollup.md) | historical negative evidence / superseded | Compressed archive for old scalar tuning iterations and early teacher-safe scalar follow-up. | Read only to avoid repeating rejected scalar candidates; not current tuning guidance. |

## Category Notes

- Current foundation reports describe retained pattern baselines that current
  docs still mention.
- Canonical trainer foundation reports document the current trainer family even
  when an individual generated candidate was rejected.
- Transitional trainer reports may contain workflow lessons, but their script
  entry points were removed from active tooling and are not a starting point for
  new pattern experiments.
- Dataset/evidence infrastructure reports explain reusable evidence plumbing,
  not evaluator promotion.
- Historical negative evidence and superseded reports are archived to preserve
  conclusions while making clear that they are not plans.
