# Experiment Notes

This directory contains experiment notes that may become stale.

Experiment notes preserve evidence, interpretation, and context from a specific
investigation. They are not current project guidance and they are not a task
queue unless the current user task, an active issue, or current project guidance
explicitly refers to them.

Task-specific hypotheses belong in issues, PR descriptions, task prompts, or
experiment reports. Raw local outputs belong under `runs/`, which is gitignored.

Operational runbooks:

- [`ntest_teacher_dataset.md`](ntest_teacher_dataset.md): first reusable local
  NTest 300K teacher dataset workflow.
- [`ntest_balanced300k_v0_report.md`](ntest_balanced300k_v0_report.md):
  completion report for the first reusable local NTest 300K teacher dataset.
- [`ntest_balanced300k_v0_validation_report.md`](ntest_balanced300k_v0_validation_report.md):
  exact-overlap validation results and high-depth follow-up plan for
  `ntest-balanced300k-v0`.

Experiment notes must not override [`AGENTS.md`](../../AGENTS.md),
[`roadmap.md`](../roadmap.md), [`capability-map.md`](../capability-map.md),
[`benchmarks.md`](../benchmarks.md), or the current user task.
