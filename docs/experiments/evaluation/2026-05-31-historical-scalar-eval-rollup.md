# 2026-05-31 Historical Scalar Evaluation Rollup

Status: historical negative evidence / superseded archive.

This rollup compresses older scalar evaluator experiments that are no longer
current instructions. It preserves the important candidates, commands, results,
and rejection reasons from the original one-off reports while reducing the
number of active-looking files in `docs/experiments/evaluation/`.

Current evaluator work should prefer the pattern-first baselines documented in
[`README.md`](README.md) and the active configs described in
[`../../../data/eval/README.md`](../../../data/eval/README.md). Do not treat
the "next recommended action" from these historical scalar reports as current
guidance.

## Rolled-Up Reports

| Original report | Original date | Status here |
| --- | --- | --- |
| `2026-05-31-classic-othello-v1.md` | 2026-05-31 | superseded scalar baseline |
| `2026-05-31-edge-frontier-balance.md` | 2026-05-31 | historical negative / superseded |
| `2026-05-31-stability-lite-narrowing.md` | 2026-05-31 | historical negative |
| `2026-05-31-edge-frontier-interaction-factorial.md` | 2026-05-31 | superseded scalar ablation |
| `2026-05-31-teacher-mistake-mining-v2.md` | 2026-05-31 | superseded teacher-safe scalar follow-up |

The original files were documentation-only experiment reports. Their raw run
outputs, generated labels, candidate configs under `runs/`, local engine paths,
and benchmark logs were not committed.

## Classic Othello v1 Evaluator Baseline

Original title/date: `2026-05-31 Classic Othello v1 Evaluator Baseline`.

Reference metadata:

- base ref / git SHA: `47b48ecf5e6c604a343ee7ebdb8f68d89231476c`
- PR head SHA at report creation: `32643939948a52871b8549fe5527acb8683c2c44`
- candidate config path at the time: `data/eval/classic_othello_v1.eval`
- candidate sha256: `f43a1de59fc298d4576b75cf807c7cb040cd8ac16349ad01196bda4348af973c`
- raw output location: `runs/eval-classic-othello-v1/`

Evaluated candidate:

- `classic_othello_v1`, a coherent classic-control scalar weight set with
  stronger mobility, potential mobility, corner safety, frontier control, and
  conservative edge-shape weights versus `current_default`.

Key command summary:

- configured and built with `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`,
  `cmake --build build`, and `ctest --test-dir build --output-on-failure`
- generated 48 train and 48 held-out exact labels with
  `othello_position_sampler` and `othello_exact_label_dump --include-move-scores`
- ran `tools/scripts/eval_candidate_matrix.py` and
  `tools/scripts/eval_config_validate.py` against `current_default.eval`
- ran `othello_match_runner` for a 24-game swapped depth-5 smoke match with
  exact roots disabled

Core result:

- CTest passed: 257/257.
- Train exact-label sign agreement improved from 29 to 31, and train top
  exact-best improved from 24 to 26.
- Held-out sign agreement and validation objective were flat versus base:
  objective 16 for both `current_default` and `classic_othello_v1`.
- Held-out exact-best top count stayed flat at 27; exact-best rank sum improved
  by one, exact score gap improved, but evaluator score gap worsened.
- Search validation reduced nodes by 4.7% but increased elapsed time by 5.5%
  in that smoke profile.
- 24-game match smoke was only slightly positive: 12-11-1, average disc diff
  +1.92 for the candidate.
- NTest teacher evidence was unavailable locally.

Conclusion / reason rejected:

`classic_othello_v1` was kept only as an experimental scalar baseline at the
time. It was not a default candidate because held-out sign/objective results
were flat, timing and move-rank evidence were mixed, and no teacher evidence
was available. It is now superseded by later teacher-mining, v3 scalar, and
pattern-first work.

## Edge / Frontier Balance Iteration

Original title/date: `2026-05-31 Edge/Frontier Balance Evaluator Iteration`.

Reference metadata:

- base ref / git SHA: `959ce64c6b5f0bd04358d06d906d4d26f001af6c`
- baseline config: `data/eval/current_default.eval`
- raw output location: `runs/eval-iteration-20260531-edge-frontier/`

Evaluated candidates:

| Candidate | Summary |
| --- | --- |
| `edge_frontier_balance_soft` | Lower `edge_8_pattern` to 1/3/5. |
| `edge_frontier_balance_mobility` | Lower `edge_8_pattern`; raise mobility and midgame frontier. |
| `edge_frontier_balance_stability` | Lower `edge_8_pattern`; raise `edge_stability_lite` and frontier caution. |

Key command summary:

- generated 24 train and 24 held-out exact labels at empties 8, 10, and 12
  using `othello_position_sampler` and `othello_exact_label_dump`
- ran train matrix with `tools/scripts/eval_candidate_matrix.py`
- ran held-out validation with `tools/scripts/eval_config_validate.py`
- ran curated and broader search/match smoke with
  `tools/scripts/eval_config_search_validate.py`, exact roots disabled

Core result:

- CTest passed: 257/257.
- Training sign agreement improved by one row for all candidates.
- Held-out objective was flat for `soft` and `mobility`; `stability` improved
  objective by +3 and reduced wrong-direction count from 4 to 3.
- No candidate improved held-out top exact-best count.
- Search checksums changed for all candidates, as expected; the stability
  candidate increased elapsed time in both measured search profiles.
- 12-game match smoke was negative for `soft` (5-7), even for `mobility`
  (6-6), and positive for `stability` (8-4, +9.17 average diff).

Conclusion / reason rejected:

`soft` was rejected, `mobility` needed more data, and `stability` was kept only
as a temporary experimental starting point. There was no default or named-config
promotion. The branch is superseded by the stability-only narrowing and
interaction-factorial follow-ups.

## Edge Stability Lite Narrowing

Original title/date: `2026-05-31 Edge Stability Lite Narrowing Iteration`.

Reference metadata:

- base ref / git SHA: `d0397cb058e7382d010160f569b28a2346a14b5b`
- baseline config: `data/eval/current_default.eval`
- raw output location: `runs/eval-iteration-20260531-stability-narrow/`

Evaluated candidates:

| Candidate | Summary |
| --- | --- |
| `stability_late_plus1` | Increase only `late.edge_stability_lite` by +1. |
| `stability_mid_late_plus1` | Increase midgame and late `edge_stability_lite` by +1. |
| `stability_all_phase_small` | Increase all phase `edge_stability_lite` weights by +1. |
| `control_restore_frontier_mobility` | Reuse the prior stronger stability values while restoring edge/frontier/mobility baseline pieces. |

Key command summary:

- generated 48 train and 48 held-out exact labels at empties 8, 10, and 12
- ran train matrix with `tools/scripts/eval_candidate_matrix.py`
- ran held-out validation with `tools/scripts/eval_config_validate.py`
- ran search validation with `tools/scripts/eval_config_search_validate.py`
- ran a 48-game swapped depth-5 match smoke for selected candidates

Core result:

- CTest passed: 257/257.
- Training sign agreement improved from 31 to 32 for the narrow stability
  candidates.
- Held-out objective, sign agreement, wrong-direction count, top exact-best
  count, exact-best rank, and exact score gap did not improve for any candidate.
- Held-out evaluator score gap worsened for all candidates.
- Match smoke was not positive: `late_plus1` went 22-26 and
  `all_phase_small` went 23-24-1 versus base.
- The stronger control had the worst elapsed-time impact.

Conclusion / reason rejected:

All stability-only candidates were rejected. The useful lesson was that the
previous positive signal was unlikely to come from `edge_stability_lite` alone.
Future scalar work should not continue by increasing `edge_stability_lite` in
isolation.

## Edge / Frontier / Stability Interaction Factorial

Original title/date: `2026-05-31 Edge/Frontier/Stability Interaction Factorial`.

Reference metadata:

- base git SHA: `47b48ecf5e6c604a343ee7ebdb8f68d89231476c`
- baseline config: `data/eval/current_default.eval`
- raw output location: `runs/eval-iteration-20260531-edge-frontier-interaction/`

Evaluated candidates:

| Candidate | Components | Final decision at the time |
| --- | --- | --- |
| `edge8_soft_only` | soften edge_8 only | reject |
| `frontier_mid_late_plus1` | frontier +1 | local ablation evidence |
| `edge8_soft_frontier` | soften edge_8 plus frontier +1 | local ablation evidence |
| `edge8_soft_stability` | soften edge_8 plus stability +1 | reject |
| `frontier_stability` | frontier +1 plus stability +1 | reject for branch |
| `edge8_soft_frontier_stability` | all three components | experimental hypothesis only |
| `edge8_soft_frontier_stability_late_only` | A+B with late-only stability | reject as carry-forward |
| `edge8_soft_frontier_stability_plus2` | A+B with +2 stability | reject |

Key command summary:

- generated 48 train and 48 held-out exact labels with root move scores
- ran train matrix, held-out validation, search validation, and 48-game match
  smoke through `eval_candidate_matrix.py`, `eval_config_validate.py`, and
  `eval_config_search_validate.py`
- ran a Round 2 stability-intensity ablation over the same held-out labels and
  search profiles

Core result:

- CTest passed: 257/257.
- `edge8_soft_only` and `edge8_soft_stability` worsened held-out objective.
- `frontier_mid_late_plus1`, `edge8_soft_frontier`,
  `frontier_stability`, and `edge8_soft_frontier_stability` reached held-out
  objective +3.
- `edge8_soft_frontier` and `edge8_soft_frontier_stability` tied the best
  held-out move-rank tier; the version without stability had a lower evaluator
  score gap.
- `edge8_soft_frontier_stability` had the strongest 48-game match smoke:
  33-15, average diff +11.04.
- Round 2 showed +2 stability was worse; late-only stability was positive
  (29-19) but weaker than full +1 all-phase stability.
- A+B+C never clearly beat A+B on held-out objective or move-rank. Its
  differentiating evidence was limited match smoke.

Conclusion / reason rejected:

The interaction run produced useful ablation evidence but no current
instruction. It did not justify default promotion or a stable named preset, and
the unresolved A+B versus A+B+C question is now superseded by later
teacher-driven scalar and pattern-first work.

## Teacher Mistake Mining v2

Original title/date: `2026-05-31 Teacher Mistake Mining v2`.

Reference metadata:

- base git SHA: `df86050fec8fa8d72a2ed48a8ff50bc6757365fe`
- head git SHA at report creation: `6f933d0891507045e0100e3fb8d104d8aa6efffb`
- raw output location: `runs/teacher-mistake-mining-v2/`

Evaluated candidate:

- `classic_othello_v2_teacher_safe`, a conservative scalar follow-up to v1.
  It preserved current-default teacher choices where v1 regressed, kept smaller
  corner-safety and shape signals, avoided extra frontier/mobility increases,
  and kept edge pattern conservative.

Key command summary:

- ran NTest depth-26 teacher labeling through
  `tools/scripts/external_teacher_label_workflow.py` with C++ legal validation
  via `build/othello_validate_move`
- generated 48 train and 48 held-out exact labels plus small exact-overlap
  dumps with `othello_exact_label_dump --include-move-scores`
- ran `tools/scripts/teacher_label_mistake_mining.py` at depth 1, shelling out
  to `othello_analyze_position`
- ran `tools/scripts/eval_candidate_matrix.py` for search validation with exact
  roots disabled
- ran two 24-game swapped depth-5 match smokes with `othello_match_runner`

Core result:

- Teacher data: 107 usable NTest rows, all legal according to the C++ rule
  core; one diagnostic row was excluded because NTest returned no recognizable
  move token.
- In 22 teacher/exact overlap positions, NTest's validated move was exact-best
  every time.
- `v2_teacher_safe` matched current-default teacher top-1 agreement
  (44/107) and improved teacher move rank sum from 286 to 280.
- It recovered all four teacher regressions introduced by `classic_othello_v1`.
- Held-out exact validation improved objective from 19 to 20; top exact-best
  improved from 24 current / 25 v1 to 26 for v2.
- Search validation reduced nodes versus current on the evaluation diagnostic
  suite with exact roots disabled.
- 24-game match smoke was positive against both current_default (13-11) and v1
  (15-9), but too small for strength claims.
- Many teacher misses stayed in `unknown_or_needs_manual_review`, indicating
  the scalar feature set was too coarse for remaining disagreements.

Conclusion / reason rejected:

At the time, `classic_othello_v2_teacher_safe` was selected only for deeper
follow-up. It was not a default candidate and not a C++ preset. It is now
superseded by the larger `classic_othello_v3_teacher_aggressive` run and by the
pattern-first pivot. The durable lesson is that scalar tweaks can recover some
teacher regressions, but the remaining unknown/manual bucket pushed the project
toward pattern and teacher-label architecture.

## Durable Lessons From These Scalar Reports

- Small exact-label samples can be useful diagnostics but are not representative
  training data.
- Match smoke is directional evidence only, not Elo and not proof of strength.
- Search checksum changes are expected for evaluator changes and are not
  automatic regressions.
- Exact roots should stay disabled during heuristic evaluator search validation
  when measuring the evaluator itself.
- Repeated scalar weight nudges produced mixed, sample-sensitive evidence.
- The increasingly large `unknown_or_needs_manual_review` buckets are a major
  reason current evaluator work moved to pattern-table, dataset, and trainer
  foundations.
