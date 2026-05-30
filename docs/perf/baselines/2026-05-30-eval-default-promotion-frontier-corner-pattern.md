# Frontier Corner Pattern Default Promotion Gate

Status: `frontier_corner_pattern_edge_lite_v1` was promoted to the default
evaluator. The previous default remains available as `phase_aware_v1`.

This is not an Elo estimate and does not make a broad strength claim. It records
the local validation gate used for a default evaluator change.

## Source

- Date: `2026-05-30`
- Base SHA validated before promotion: `f01d05b0686b653414f90e1d5e928bb0ea87920d`
- Branch: `codex/promote-frontier-corner-pattern-default`
- Experiment source: latest main with `frontier_corner_pattern_edge_lite_v1`
  selected explicitly, then a local working tree that makes the same config the
  default evaluator.
- Build type: `Release`
- Opening suite: `data/openings/eval_regression_openings.txt`
- Local run dir: `runs/eval/20260530-210437-frontier-corner-pattern-default-promotion-gate`

Raw run output stays under `runs/` and is not committed.

## Evaluator Change

The new default config is equivalent to the existing explicit preset
`frontier_corner_pattern_edge_lite_v1`:

| phase | frontier | corner_2x3_pattern | edge_stability_lite |
| :--- | ---: | ---: | ---: |
| opening | 5 | 4 | 2 |
| midgame | 6 | 6 | 4 |
| late | 3 | 4 | 8 |

The previous phase-aware default remains available as:

```text
--eval-preset phase_aware_v1
```

No search, move ordering, exact endgame, MPC, ProbCut, or LMR semantics are
changed by this promotion.

## Validation Gate

All validation used exact endgame disabled via `--exact-endgame-threshold 0`.
Candidate was player A and the reference preset was player B.

### Versus Previous Default

Reference: `default` before this PR, represented by `phase_aware_v1` after this
PR.

| depth | games | W-L-D | avg diff | node ratio | time ratio | errors |
| ---: | ---: | :--- | ---: | ---: | ---: | ---: |
| 5 | 96 | 60-33-3 | 7.44 | 1.017 | 1.052 | 0 |
| 6 | 96 | 61-33-2 | 9.54 | 0.973 | 1.058 | 0 |
| 7 | 192 | 130-57-5 | 9.78 | 1.006 | 1.063 | 0 |
| 8 | 192 | 112-78-2 | 5.99 | 0.973 | 1.056 | 0 |

### Versus Current Frontier Reference

Reference: `frontier_open2_mid2_late_plus1`.

| depth | games | W-L-D | avg diff | node ratio | time ratio | errors |
| ---: | ---: | :--- | ---: | ---: | ---: | ---: |
| 5 | 96 | 55-41-0 | 2.44 | 0.996 | 1.024 | 0 |
| 6 | 96 | 54-38-4 | 5.42 | 0.987 | 1.053 | 0 |
| 7 | 192 | 103-84-5 | 4.52 | 0.956 | 1.030 | 0 |
| 8 | 192 | 95-89-8 | 2.46 | 0.919 | 0.976 | 0 |

### Cross-Seed Check

Reference: `frontier_open2_mid2_late_plus1`, seed `20260603`.

| depth | games | W-L-D | avg diff | node ratio | time ratio | errors |
| ---: | ---: | :--- | ---: | ---: | ---: | ---: |
| 7 | 192 | 103-84-5 | 4.52 | 0.956 | 1.031 | 0 |
| 8 | 384 | 194-175-15 | 2.52 | 0.920 | 0.988 | 0 |

## NTest Sanity

A local NTest build was available outside this repository. Its default
`parameters.txt` enables book use, but a no-book workdir under `runs/` with
`fUseBook = 0` allowed the engine to run. This is a very small sanity check
only, not an adoption criterion and not a strength claim.

All checks used `search:depth=6,tt=on,pvs=on,exact=off`, `external:ntest6_nobook`,
4 games, `data/openings/smoke_openings.txt`, and seed `20260602`.

| preset | W-L-D | avg diff | errors |
| :--- | :--- | ---: | ---: |
| `phase_aware_v1` | 0-4-0 | -57.00 | 0 |
| `frontier_open2_mid2_late_plus1` | 0-4-0 | -55.00 | 0 |
| `frontier_corner_pattern_edge_lite_v1` | 0-4-0 | -45.50 | 0 |

## Checksum Compatibility

After the promotion, `--eval-preset default` matched
`--eval-preset frontier_corner_pattern_edge_lite_v1` exactly on the smoke search
benchmark:

```sh
./build/othello_search_bench --mode both --depths 1,2,3,4,5 --positions smoke --repetitions 1 --eval-preset default --exact-endgame-threshold 0
```

| mode | depth | result checksum | work checksum |
| :--- | ---: | ---: | ---: |
| fixed | 1 | 959874938194144048 | 3604605076574334160 |
| iterative | 1 | 15448387155809625736 | 12936632949908081873 |
| fixed | 2 | 12870107131423905180 | 17294073531376405614 |
| iterative | 2 | 3391749791189669388 | 7129501981295144306 |
| fixed | 3 | 13708049332724192735 | 17213153318421205678 |
| iterative | 3 | 12205549328033224009 | 7271551554699962803 |
| fixed | 4 | 13639237172806117867 | 6798708112870278383 |
| iterative | 4 | 13922273217391139153 | 16136763174585352603 |
| fixed | 5 | 2900483029563250214 | 6065216869992930095 |
| iterative | 5 | 6629072083868305425 | 4717395135335471579 |

`phase_aware_v1` remains the compatibility path for the previous default
evaluator.

## Verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m unittest discover tools/scripts/tests
git diff --check
```

Analyze visibility was checked with `--eval-preset default`; output includes
`corner_2x3_pattern`, `corner_2x3_pattern_weight`,
`corner_2x3_pattern_score`, `edge_stability_lite`,
`edge_stability_lite_weight`, and `edge_stability_lite_score`.

## Risks

- The corner 2x3 table is rule-generated, not learned.
- There may be double-counting with existing corner, X-square, and edge
  features.
- The opening suite is broader than smoke but still limited.
- Sample sizes are suitable for a promotion gate, not for an Elo estimate.
- NTest sanity was intentionally tiny and only checked integration health.
- Self-play can overfit to this evaluator and search configuration.

## Next Actions

- Run broader validation against more openings and seeds.
- Keep NTest no-book config as a local sanity option and increase games only
  when runtime is acceptable.
- Retune the rule-generated table if a focused regression appears.
- Consider an edge 8 pattern table only after the corner table behavior is
  understood.
