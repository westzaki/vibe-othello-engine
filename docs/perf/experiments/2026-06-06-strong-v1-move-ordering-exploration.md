# Strong V1 Move Ordering Exploration

Date: 2026-06-06

## Goal

Explore small move-ordering-only changes for `strong-v1`:

- reduce node count
- raise `beta_cut_first_move_pct`
- preserve search score meaning
- avoid move skipping, forward pruning, MPC, eval-based pruning, and exact solver changes

The changes below were measured and rejected. No engine behavior change was kept
from this exploration.

## Scope And Commits

This is a rejected experiment log, not a current tuning recommendation and not
a stable performance baseline. The measured candidates were temporary local
patches used only to collect the numbers below; they were reverted after each
measurement and were not kept as engine commits.

- Measured base commit: `5d03b7b85f8b9ba301c504dbbb97b1726d157d04`
- Measured candidate commit: none; each candidate was an uncommitted temporary
  patch on the measured base commit
- Report commit before latest-main cleanup: `f266e230ff85c46d280df07d000cd5ba83539bf6`
- Latest main used for this docs-only PR cleanup:
  `e1a181f0745bea74840d891dffddf82dc6157bc3`

## Reproduction Setup

Baseline worktree:

```sh
git worktree add /private/tmp/vibe-othello-ordering-baseline \
  5d03b7b85f8b9ba301c504dbbb97b1726d157d04
cmake -S /private/tmp/vibe-othello-ordering-baseline \
  -B /private/tmp/vibe-othello-ordering-baseline/build \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /private/tmp/vibe-othello-ordering-baseline/build \
  --target othello_search_bench -j 6
```

Candidate worktree:

```sh
git switch --detach 5d03b7b85f8b9ba301c504dbbb97b1726d157d04
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target othello_tests othello_search_bench -j 6
```

For each candidate, apply only the temporary move-ordering patch described in
that section, run the listed commands, then revert the patch before trying the
next candidate.

Correctness smoke used during the experiments:

```sh
./build/othello_tests "[search]"
```

Fixed-depth suite checks used to separate correctness from ordering work:

```sh
./build/othello_search_bench \
  --mode fixed \
  --depths 5,6 \
  --positions suite \
  --repetitions 1 \
  --tt off \
  --pvs off \
  --exact-endgame-threshold 0 \
  --format jsonl

./build/othello_search_bench \
  --mode fixed \
  --depths 5,6 \
  --positions suite \
  --repetitions 1 \
  --tt on \
  --pvs off \
  --exact-endgame-threshold 0 \
  --format jsonl
```

## Candidate: Root Dynamic Ordering Plus Unguarded Shallow TT Hints

Hypothesis: using the existing cheap dynamic ordering at the iterative root,
and allowing any shallower TT best-move entry as an ordering hint, would improve
PVS first-move cutoffs without changing search semantics.

Result: rejected.

The candidate changed `strong-v1` aggregate best moves, PVs, and result
checksums at depths 5 and 7. Although scores stayed equal on the measured
suite, this was too much behavior movement for a move-ordering cleanup.

Single-repetition suite, iterative, TT on, PVS on, score-delta-aware aspiration:

| depth | baseline nodes | candidate nodes | node delta | baseline beta first % | candidate beta first % |
| --- | ---: | ---: | ---: | ---: | ---: |
| 5 | 36,664 | 37,381 | +1.96% | 69.22 | 71.27 |
| 6 | 96,789 | 96,059 | -0.75% | 75.10 | 79.25 |
| 7 | 327,495 | 322,604 | -1.49% | 70.59 | 74.80 |

## Candidate: Unguarded Shallow TT Hints, Non-Root Only

Hypothesis: keep root ordering stable, but split the TT depth guard so cutoff
still requires sufficient depth while ordering can use any matching shallower
best move.

Result: rejected.

This raised `beta_cut_first_move_pct`, but did not reduce nodes on depths 5-7.
It also changed iterative result checksums.

Single-repetition suite:

| depth | baseline nodes | candidate nodes | node delta | baseline beta first % | candidate beta first % |
| --- | ---: | ---: | ---: | ---: | ---: |
| 5 | 36,664 | 38,166 | +4.10% | 69.22 | 70.85 |
| 6 | 96,789 | 98,192 | +1.45% | 75.10 | 78.70 |
| 7 | 327,495 | 327,884 | +0.12% | 70.59 | 74.62 |

## Candidate: One-Ply-Shallow TT Hints

Hypothesis: previous-iteration TT hints are useful, but older shallow hints are
too stale. Allow ordering hints only when `entry.depth + 1 >= requested_depth`.

Result: rejected.

The deeper single-repetition aggregate had a tiny node reduction over depths
5-9, but the more stable depth 5-7 repetition run was worse overall.

Three-repetition suite, depths 5-7:

| depth | baseline nodes | candidate nodes | node delta | baseline beta first % | candidate beta first % |
| --- | ---: | ---: | ---: | ---: | ---: |
| 5 | 109,992 | 114,960 | +4.52% | 69.22 | 70.66 |
| 6 | 290,367 | 296,160 | +2.00% | 75.10 | 78.40 |
| 7 | 982,485 | 978,237 | -0.43% | 70.59 | 74.48 |

Single-repetition deeper check:

| depth | baseline nodes | candidate nodes | node delta | baseline beta first % | candidate beta first % |
| --- | ---: | ---: | ---: | ---: | ---: |
| 8 | 860,707 | 885,341 | +2.86% | 75.61 | 79.31 |
| 9 | 2,696,590 | 2,656,769 | -1.48% | 71.89 | 76.23 |

Ordering overhead proxy:

- `dyn_nodes` and `dyn_moves` stayed similar to baseline for the one-ply hint
  experiment because the dynamic-ordering gate itself was unchanged.
- `tt_order_hits` rose substantially, confirming that the new hint path was
  active, but this did not convert into a consistent node reduction.

## Candidate: Lighter Mobility And Corner-Danger Ordering Penalties

Hypothesis: the current dynamic ordering may overweight opponent mobility,
potential mobility, and corner-danger terms. Lowering those penalties might
avoid overfitting the ordering score.

Tested values:

- `dynamic_opponent_corner_penalty`: `80'000` to `60'000`
- `dynamic_opponent_mobility_penalty`: `500` to `300`
- `dynamic_potential_mobility_penalty`: `25` to `10`
- `dynamic_static_risk_penalty`: `25` to `10`

Result: rejected.

The fixed-depth suite preserved scores and best moves in the shallow check, but
changed the depth-6 result checksum and increased node counts. It also broke the
representative midgame node-count snapshots in `tests/search_tests.cpp`.

Fixed-depth suite, TT off, PVS off:

| depth | baseline nodes | candidate nodes | node delta | baseline result checksum | candidate result checksum |
| --- | ---: | ---: | ---: | --- | --- |
| 5 | 37,465 | 39,560 | +5.59% | `9026091185920752710` | `9026091185920752710` |
| 6 | 89,563 | 95,575 | +6.71% | `5793025129917076089` | `5793098585132126841` |

Fixed-depth suite, TT on, PVS off:

| depth | baseline nodes | candidate nodes | node delta | baseline result checksum | candidate result checksum |
| --- | ---: | ---: | ---: | --- | --- |
| 5 | 34,480 | 36,194 | +4.97% | `9026091185920752710` | `9026091185920752710` |
| 6 | 80,782 | 85,822 | +6.24% | `5793025129917076089` | `5793098585132126841` |

## Conclusion

No tested change met the goal strongly enough to keep.

Root dynamic ordering and shallow TT hints do improve
`beta_cut_first_move_pct`, but they either move iterative best moves/PVs or fail
to reduce nodes consistently. Lighter dynamic mobility penalties were worse for
fixed-depth work and changed a fixed-depth checksum.

Recommended next experiments:

- try root-only shallow search ordering, but constrain it to root candidates and
  compare root scores/checksums explicitly
- inspect per-position rows for the depth-7 and depth-9 wins before applying a
  global TT hint policy
- test a history/killer scaling change separately from mobility/corner ordering
  terms
