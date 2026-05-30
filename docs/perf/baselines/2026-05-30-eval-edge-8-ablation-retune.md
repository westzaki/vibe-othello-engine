# Edge 8 ablation / retune preset

## Status

- Adds one explicit experimental preset: `default_edge_pattern_8_soft`.
- The default evaluator is unchanged.
- This is not an Elo estimate.
- This is not a broad strength claim.
- This is not a default-promotion proposal.

## Source

- Validation base SHA: `42af80775895d81b14a8dbacfa91d2f79f186224`
- PR base SHA after rebase: `ea742a9e834407c0a24f87c30d6713550be8a8d8`
- Branch: `codex/edge-8-ablation-retune`
- Build type: `Release`
- Experiment source: local working tree based on the validation base SHA with temporary
  preset-only variants for the ablation sweep. The final PR branch is based on the PR
  base SHA and keeps only `default_edge_pattern_8_soft`.
- Rebase note: the full staged sweep was not rerun after rebasing onto the PR base SHA.
  The final build, tests, default smoke checksum, and orchestrator dry-run were rerun on
  the rebased PR branch.
- Raw outputs: local `runs/eval/20260531-063145-edge-8-ablation-retune/`
  artifacts, not committed.

## Hypotheses

PR134 promoted `edge_8_pattern` into the default evaluator. This sweep checked whether
the current default double-counts edge concepts through both `edge_stability_lite` and
`edge_8_pattern`.

Candidates:

- `frontier_corner_pattern_edge_lite_v1`: previous default before edge 8
- `default_edge_pattern_8_no_edge_lite`: current default with edge stability lite off
- `default_edge_pattern_8_aggressive`: stronger edge 8 weights
- `default_edge_pattern_8_soft`: current default with edge 8 weights `1/3/5`
- `default_edge_pattern_8_edge_lite_soft`: temporary edge stability lite softening
- `default_edge_pattern_8_late_heavy`: temporary late-heavy edge 8 retune

The temporary variants that did not pass the gate are not retained.

## Setup

The match sweeps used:

- `exact=off`
- `data/openings/eval_regression_openings.txt`
- seed `20260606` for the primary sweeps
- seed `20260607` for the focused cross-seed check
- node ratio gate `<= 1.20`
- no Elo-style interpretation

## Primary sweep vs current default

Reference: `default`

`default_edge_pattern_8_soft` was the only candidate promoted to the extended stage.

| Preset | Depth | Games | W-L-D | Avg disc diff | Node ratio | Time ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `default_edge_pattern_8_soft` | 5 | 96 | 45-50-1 | +0.10 | 1.028 | 1.001 |
| `default_edge_pattern_8_soft` | 6 | 96 | 56-38-2 | +3.54 | 0.966 | 1.008 |
| `default_edge_pattern_8_soft` | 7 | 192 | 98-89-5 | +0.84 | 1.003 | 0.992 |
| `default_edge_pattern_8_soft` | 8 | 192 | 105-87-0 | +2.11 | 1.029 | 1.036 |

Rejected or not retained:

- `frontier_corner_pattern_edge_lite_v1`: aggregate 88-99-5, avg diff -0.95
- `default_edge_pattern_8_aggressive`: aggregate 85-100-7, avg diff -2.72
- `default_edge_pattern_8_no_edge_lite`: mixed, aggregate 92-92-8, avg diff +0.18,
  with a depth 6 avg diff -0.15
- `default_edge_pattern_8_edge_lite_soft`: mixed, aggregate 97-93-2, avg diff -0.26,
  with a depth 6 avg diff -4.33
- `default_edge_pattern_8_late_heavy`: mixed, aggregate 92-100-0, avg diff +0.56

## Check vs pre-edge8 default

Reference: `frontier_corner_pattern_edge_lite_v1`

The current default remains a valid fallback versus the pre-edge8 default.

| Preset | Depth | Games | W-L-D | Avg disc diff |
| --- | ---: | ---: | ---: | ---: |
| `default` | 5 | 96 | 50-46-0 | +0.92 |
| `default` | 6 | 96 | 49-42-5 | +0.98 |
| `default` | 7 | 192 | 110-79-3 | +6.70 |
| `default` | 8 | 192 | 95-87-10 | +1.81 |

`default_edge_pattern_8_soft` had a depth 5 dip versus the pre-edge8 default.

| Preset | Depth | Games | W-L-D | Avg disc diff | Node ratio | Time ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `default_edge_pattern_8_soft` | 5 | 96 | 41-52-3 | -3.00 | 1.033 | 1.033 |
| `default_edge_pattern_8_soft` | 6 | 96 | 56-40-0 | +7.06 | 0.998 | 1.040 |

Because of this depth 5 row, the soft retune is not promoted to default.

## Cross-seed focused check

Reference: `default`, seed `20260607`

| Preset | Depth | Games | W-L-D | Avg disc diff | Node ratio | Time ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `default_edge_pattern_8_soft` | 7 | 192 | 98-89-5 | +0.84 | 1.003 | 0.999 |
| `default_edge_pattern_8_soft` | 8 | 384 | 206-178-0 | +1.68 | 1.034 | 1.041 |

This is directionally positive versus the current default, but not enough to override
the pre-edge8 depth 5 concern and NTest sanity caveat.

## NTest sanity

NTest was run without book from `/Users/mnishizaki/Project/engine/ntest/build/ntest`.
This is a tiny integration sanity check, not a promotion criterion by itself.

Configuration:

- depth 6
- 12 games
- `swap-sides=true`
- seed `20260606`
- `data/openings/eval_regression_openings.txt`
- error games: 0 for all rows

| Preset | W-L-D vs NTest | Avg disc diff |
| --- | ---: | ---: |
| `default` | 2-10-0 | -28.00 |
| `default_edge_pattern_8_soft` | 2-10-0 | -32.67 |
| `frontier_corner_pattern_edge_lite_v1` | 0-12-0 | -30.50 |

The soft retune did not show an integration failure, but it did show a worse tiny
NTest avg diff than the current default. This is another reason to keep it
experimental rather than promoting it.

## Default compatibility

Default evaluator behavior is unchanged in this PR.

Pre-change default smoke representative rows:

| Mode | Depth | Result checksum | Work checksum |
| --- | ---: | ---: | ---: |
| fixed | 5 | `2902611684074624550` | `5828777889555978287` |
| iterative | 5 | `6635352494286161937` | `4994366512418752859` |

The post-change verification should keep these checksums unchanged because the new
retune is only available through `--eval-preset default_edge_pattern_8_soft`.

## Decision

Outcome B: keep `default_edge_pattern_8_soft` as an explicit experimental preset.

Default promotion is intentionally skipped because:

- it has a negative depth 5 row versus the pre-edge8 default,
- tiny NTest sanity is worse than current default on average disc diff,
- the positive current-default rows may still be self-play/opening sensitive.

## Risks

- The edge 8 table is rule-generated, not learned.
- `edge_8_pattern` can double-count with `edge_stability_lite`.
- Edge features can also interact with `corner_2x3_pattern`.
- Opening coverage is still limited.
- Self-play can overfit to evaluator-specific blind spots.
- NTest sanity is intentionally small.

## Next actions

- Run broader validation before any default retune.
- Repeat NTest sanity with larger samples when runtime permits.
- Consider rule-table retuning only after the edge interaction is better understood.
- Do not add another pattern table until edge 8 behavior is settled.
