# 2026-05-31 Edge Stability Lite Narrowing Iteration

Status: experimental report, not current guidance.

This note follows
[`2026-05-31-edge-frontier-balance.md`](2026-05-31-edge-frontier-balance.md).
The previous `edge_frontier_balance_stability` candidate produced useful but
non-promotable evidence: held-out objective +3, wrong-direction 4 to 3, and a
12-game match smoke result of 8-4, but no top exact-best improvement and worse
search elapsed time. This iteration narrows the question to whether
`edge_stability_lite` was the useful part.

No default promotion. No named config promotion. No strength claim.

## Metadata

- base ref / git SHA: `d0397cb058e7382d010160f569b28a2346a14b5b`
- baseline config: `data/eval/current_default.eval`
- baseline config sha256: `42d66384d725c55f46261624d78e4723846d63df1d2c1b41200429c4f4fa99cd`
- raw output location: `runs/eval-iteration-20260531-stability-narrow/`
- raw outputs committed: no

## Hypothesis

If the previous `edge_frontier_balance_stability` signal came mainly from
`edge_stability_lite`, then narrow `edge_stability_lite` increases should retain
some held-out or move-rank benefit without the larger search elapsed regression.

For the main candidates, `edge_8_pattern`, mobility, and frontier stayed fixed
at `current_default.eval` values.

## Candidates

| candidate | short description | sha256 |
| --- | --- | --- |
| `stability_late_plus1` | Increase only `late.edge_stability_lite` by +1. | `8157c79dcc2a61569193c47347f669af3a0a90ecb4bfeafa64189d50f74fd5f3` |
| `stability_mid_late_plus1` | Increase `midgame.edge_stability_lite` and `late.edge_stability_lite` by +1. | `c9f4c969111a4193159731c85bafe3e66b4dee6c23f3e25f173c739c1e413c30` |
| `stability_all_phase_small` | Increase all phase `edge_stability_lite` weights by +1. | `d83e55679b4522b27696761ceb0d2ef4eab817b6792f8abf58ca88481974acae` |
| `control_restore_frontier_mobility` | Keep edge_8/frontier/mobility at base while using the previous stability candidate's edge-stability values. | `6e0debaaafde349316e43777d53f7d23cde07335625c2635dd47850235655426` |

The candidate `.eval` files were local experiment artifacts under `runs/` and
were not committed.

## Candidate Config Diffs

To recreate a candidate, start from `data/eval/current_default.eval` at the base
ref above, then apply the key changes below. Unlisted keys match the baseline
config.

### stability_late_plus1

| key | base | candidate |
| --- | --- | --- |
| `name` | `current_default` | `stability_late_plus1` |
| `late.edge_stability_lite` | 8 | 9 |

### stability_mid_late_plus1

| key | base | candidate |
| --- | --- | --- |
| `name` | `current_default` | `stability_mid_late_plus1` |
| `midgame.edge_stability_lite` | 4 | 5 |
| `late.edge_stability_lite` | 8 | 9 |

### stability_all_phase_small

| key | base | candidate |
| --- | --- | --- |
| `name` | `current_default` | `stability_all_phase_small` |
| `opening.edge_stability_lite` | 2 | 3 |
| `midgame.edge_stability_lite` | 4 | 5 |
| `late.edge_stability_lite` | 8 | 9 |

### control_restore_frontier_mobility

| key | base | candidate |
| --- | --- | --- |
| `name` | `current_default` | `control_restore_frontier_mobility` |
| `opening.edge_stability_lite` | 2 | 3 |
| `midgame.edge_stability_lite` | 4 | 5 |
| `late.edge_stability_lite` | 8 | 10 |

This control intentionally kept `edge_8_pattern`, mobility, and frontier at
baseline values while retaining the previous broader stability candidate's
`edge_stability_lite` values.

## Generated Label Setup

| split | seed | count | target empties | move scores |
| --- | ---: | ---: | --- | --- |
| train | 2026053121 | 48 | `8,10,12` | included |
| held-out | 2026053122 | 48 | `8,10,12` | included |

Command templates used:

```sh
./build/othello_position_sampler --output runs/eval-iteration-20260531-stability-narrow/train/positions.txt --count 48 --target-empties 8,10,12 --seed 2026053121
./build/othello_exact_label_dump --input runs/eval-iteration-20260531-stability-narrow/train/positions.txt --output runs/eval-iteration-20260531-stability-narrow/train/exact_labels.jsonl --max-empties 12 --include-move-scores

./build/othello_position_sampler --output runs/eval-iteration-20260531-stability-narrow/heldout/positions.txt --count 48 --target-empties 8,10,12 --seed 2026053122
./build/othello_exact_label_dump --input runs/eval-iteration-20260531-stability-narrow/heldout/positions.txt --output runs/eval-iteration-20260531-stability-narrow/heldout/exact_labels.jsonl --max-empties 12 --include-move-scores
```

Both sets produced 48 labels with root `move_scores`.

## Correctness Verification

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Result: 257/257 CTest tests passed.

## Evaluation Commands

Train candidate matrix:

```sh
python3 tools/scripts/eval_candidate_matrix.py --build-dir build --labels runs/eval-iteration-20260531-stability-narrow/train/exact_labels.jsonl --baseline-config data/eval/current_default.eval --candidates runs/eval-iteration-20260531-stability-narrow/configs/stability_late_plus1.eval runs/eval-iteration-20260531-stability-narrow/configs/stability_mid_late_plus1.eval runs/eval-iteration-20260531-stability-narrow/configs/stability_all_phase_small.eval runs/eval-iteration-20260531-stability-narrow/configs/control_restore_frontier_mobility.eval --out runs/eval-iteration-20260531-stability-narrow/candidate-matrix-train --search-depths 5,6,7 --positions smoke --repetitions 1 --exact-endgame-threshold 0 --move-rank-analysis
```

Held-out validation:

```sh
python3 tools/scripts/eval_config_validate.py --validation-labels runs/eval-iteration-20260531-stability-narrow/heldout/exact_labels.jsonl --base-config data/eval/current_default.eval --candidate-configs runs/eval-iteration-20260531-stability-narrow/configs/stability_late_plus1.eval,runs/eval-iteration-20260531-stability-narrow/configs/stability_mid_late_plus1.eval,runs/eval-iteration-20260531-stability-narrow/configs/stability_all_phase_small.eval,runs/eval-iteration-20260531-stability-narrow/configs/control_restore_frontier_mobility.eval --out runs/eval-iteration-20260531-stability-narrow/heldout-validation --build-dir build --top 5 --move-rank-analysis
```

Search validation:

```sh
python3 tools/scripts/eval_config_search_validate.py --base-config data/eval/current_default.eval --candidate-configs runs/eval-iteration-20260531-stability-narrow/configs/stability_late_plus1.eval,runs/eval-iteration-20260531-stability-narrow/configs/stability_mid_late_plus1.eval,runs/eval-iteration-20260531-stability-narrow/configs/stability_all_phase_small.eval,runs/eval-iteration-20260531-stability-narrow/configs/control_restore_frontier_mobility.eval --heldout-summary runs/eval-iteration-20260531-stability-narrow/heldout-validation/summary.tsv --build-dir build --out runs/eval-iteration-20260531-stability-narrow/search-validation-evaluation --top 4 --run-search-bench --positions evaluation --depths 5,6 --exact-endgame-threshold 0
```

48-game swapped match smoke:

```sh
python3 tools/scripts/eval_config_search_validate.py --base-config data/eval/current_default.eval --candidate-configs runs/eval-iteration-20260531-stability-narrow/configs/stability_late_plus1.eval,runs/eval-iteration-20260531-stability-narrow/configs/stability_all_phase_small.eval --heldout-summary runs/eval-iteration-20260531-stability-narrow/heldout-validation/summary.tsv --build-dir build --out runs/eval-iteration-20260531-stability-narrow/match-smoke-48 --top 2 --run-match-smoke --positions evaluation --depths 5 --games 48 --openings data/openings/eval_regression_openings.txt --exact-endgame-threshold 0
```

## Eval-vs-Exact Summary

Training set, 48 records:

| config | sign agreements | wrong direction | high confidence wrong |
| --- | ---: | ---: | ---: |
| base | 31 | 15 | 0 |
| late_plus1 | 32 | 14 | 0 |
| mid_late_plus1 | 32 | 14 | 0 |
| all_phase_small | 32 | 14 | 0 |
| control_restore_frontier_mobility | 32 | 14 | 0 |

Held-out set, 48 records:

| config | objective | delta vs base | sign agreements | wrong direction | high confidence wrong |
| --- | ---: | ---: | ---: | ---: | ---: |
| base | 20 | 0 | 38 | 9 | 0 |
| late_plus1 | 20 | 0 | 38 | 9 | 0 |
| mid_late_plus1 | 20 | 0 | 38 | 9 | 0 |
| all_phase_small | 20 | 0 | 38 | 9 | 0 |
| control_restore_frontier_mobility | 20 | 0 | 38 | 9 | 0 |

## Move-Rank Summary

Move-rank analysis was available because the generated labels included
`move_scores`.

Held-out set, 48 records:

| config | move rank analyzed | top exact-best | top exact-best rate | exact-best rank sum | mean exact-best rank | eval score gap sum | mean eval score gap | exact score gap sum | mean exact score gap |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| base | 48 | 22 | 45.833% | 95 | 1.979 | 2114 | 44.042 | 204 | 4.250 |
| late_plus1 | 48 | 22 | 45.833% | 95 | 1.979 | 2197 | 45.771 | 204 | 4.250 |
| mid_late_plus1 | 48 | 22 | 45.833% | 95 | 1.979 | 2197 | 45.771 | 204 | 4.250 |
| all_phase_small | 48 | 22 | 45.833% | 95 | 1.979 | 2197 | 45.771 | 204 | 4.250 |
| control_restore_frontier_mobility | 48 | 22 | 45.833% | 95 | 1.979 | 2280 | 47.500 | 204 | 4.250 |

No candidate improved top exact-best count, exact-best rank, or exact score gap.
All candidates worsened the held-out evaluator score gap sum.

## Search Validation Summary

Search validation used `--positions evaluation` and
`--exact-endgame-threshold 0`.

| config | nodes | node delta vs base | elapsed ms | elapsed delta vs base | checksum |
| --- | ---: | ---: | ---: | ---: | --- |
| base | 119486 | 0.0% | 24.810 | 0.0% | reference |
| late_plus1 | 119969 | +0.4% | 24.997 | +0.8% | changed |
| mid_late_plus1 | 120037 | +0.5% | 25.338 | +2.1% | changed |
| all_phase_small | 120037 | +0.5% | 25.314 | +2.0% | changed |
| control_restore_frontier_mobility | 120488 | +0.8% | 32.772 | +32.1% | changed |

Checksum changes are behavior changes, not automatic regressions. The control
variant repeated the previous concern: stronger stability weights can carry a
noticeable elapsed-time cost.

## Match Smoke Summary

Depth 5, exact disabled, 48 games, `eval_regression_openings.txt`, candidate as
player A vs base as player B with side swaps:

| candidate | A wins | B wins | draws | average disc diff from A | average time ms A/B |
| --- | ---: | ---: | ---: | ---: | --- |
| late_plus1 | 22 | 26 | 0 | -1.33 | 28.61 / 28.85 |
| all_phase_small | 23 | 24 | 1 | -1.75 | 30.72 / 30.29 |

This is directional smoke evidence only, not Elo.

## Evidence

Positive evidence:

- The narrow stability candidates reproduced the training-set sign improvement
  from 31 to 32 agreements.
- `late_plus1` had only a small search elapsed impact in the evaluation suite.
- The follow-up made the previous signal more interpretable: `edge_stability_lite`
  alone did not carry the held-out improvement.

Negative evidence:

- Held-out objective, sign agreements, wrong-direction count, top exact-best
  count, exact-best rank, and exact score gap did not improve for any candidate.
- Held-out evaluator score gap worsened for all candidates.
- The 48-game swapped match smoke was not positive for either tested candidate.
- The stronger control had the worst search elapsed impact.

## Decisions

| candidate | decision | reason |
| --- | --- | --- |
| `stability_late_plus1` | reject for this loop | No held-out or move-rank gain; 48-game match smoke was 22-26. |
| `stability_mid_late_plus1` | reject for this loop | Same held-out result as base and more search work than `late_plus1`. |
| `stability_all_phase_small` | reject for this loop | No held-out or move-rank gain; 48-game match smoke was 23-24-1. |
| `control_restore_frontier_mobility` | reject for this loop | No held-out gain, worse move-rank gap, and large elapsed-time regression. |

No candidate is promotable as an experimental config from this iteration.
The previous `edge_frontier_balance_stability` signal should not be discarded as
a dead end, but this follow-up suggests that `edge_stability_lite` alone was not
the useful part.

## Caveats

- This report is historical experiment evidence, not permanent tuning guidance.
- Smoke evidence only; no strength claim.
- Raw outputs remain under `runs/` and are not committed.
- Generated exact labels are not committed.
- Exact labels are small and not representative training data.
- The default evaluator was not promoted.
- No named experimental config was promoted.
- No new C++ `EvaluationPreset` was added.
- Engine behavior, evaluator semantics, search algorithm semantics, and exact
  solver semantics were not changed.

## Next Recommended Action

Do not continue by increasing `edge_stability_lite` alone. The next useful
candidate should isolate the other parts of the previous signal:

- `edge_8_pattern` softening with baseline frontier and baseline stability
- frontier +1 with baseline `edge_8_pattern` and baseline stability
- the previous full `edge_frontier_balance_stability` candidate on a larger
  held-out set to check whether the earlier +3 objective was sample-specific

Keep `--exact-endgame-threshold 0` for search validation and run at least a
48-game swapped match before considering any durable config.
