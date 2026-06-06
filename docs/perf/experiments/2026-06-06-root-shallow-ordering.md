# Root-Only Shallow Search Ordering Experiment

Date: 2026-06-06

## Hypothesis

Root-only shallow scoring might improve the first full-depth root candidate order, reducing PVS
scout re-searches without touching non-root dynamic ordering, TT hint policy, or history/killer
policy.

Two variants were tested:

- depth-2 root shallow ordering: sort root candidates by a side-effect-free shallow negamax score,
  capped at two plies below each root move.
- depth-1 root shallow ordering: same approach, capped at one ply below each root move.

Both variants only changed root candidate order. They did not skip moves or forward-prune.

## Commands

Baseline was `origin/main` at `b570c22`. Candidate builds were Release builds from the local
experiment branch.

```sh
ctest --test-dir build --output-on-failure
./build/othello_search_bench --mode fixed --depths 5,6 --positions suite --tt off --pvs off --exact-endgame-threshold 0 --format jsonl
./build/othello_search_bench --mode iterative --depths 5,6,7,8 --positions suite --tt on --pvs on --aspiration on --aspiration-profile score-delta-aware --exact-endgame-threshold adaptive16 --by-position --format jsonl
./build/othello_search_bench --mode iterative --depths 5,6,7,8 --positions suite --tt on --pvs on --aspiration on --aspiration-profile score-delta-aware --exact-endgame-threshold adaptive16 --format jsonl
```

The `--by-position --emit-iterative-depth-rows` form was also used to inspect root ordering
snapshots for the slow positions.

## Results

Depth-2 shallow ordering reduced aggregate PVS re-searches but failed the adoption bar because the
extra shallow work made depth 7 and depth 8 aggregate performance worse or nearly flat:

| mode | depth | nodes | elapsed | PVS researches | result checksum |
| --- | ---: | ---: | ---: | ---: | --- |
| iterative | 7 | +9.6% | +5.9% | -22.6% | changed |
| iterative | 8 | -2.0% | -1.0% | -20.6% | changed |

Depth-1 shallow ordering was better, but still failed the slow-position rank condition:

| mode | depth | nodes | elapsed | PVS researches | result checksum |
| --- | ---: | ---: | ---: | ---: | --- |
| iterative | 7 | -4.3% | -2.4% | -22.6% | changed |
| iterative | 8 | -8.0% | -4.2% | -19.3% | changed |
| fixed | 5 | -8.9% | -0.6% | n/a | unchanged |
| fixed | 6 | -16.2% | -2.7% | n/a | changed |

Slow-position depth-8 root-rank diagnostics for depth-1 shallow ordering:

| position | baseline best | candidate best | baseline rank | candidate final rank | score | result |
| --- | --- | --- | ---: | ---: | ---: | --- |
| opening-a1-access | d6 | b2 | 5 | 8 | 91 | changed |
| midgame-x-risk | b4 | c1 | 7 | 2 | 218 | changed |
| midgame-wide-x-risk | c2 | c2 | 6 | 11 | 332 | unchanged |
| midgame-lopsided-edge | g4 | f2 | 8 | 9 | -302 | changed |

Scores did not worsen in the measured rows, but best-move/PV/checksum changes were common because
root ordering changed tie resolution and the iterative search trajectory.

## Decision

Rejected. Root-only shallow ordering is promising for reducing PVS re-searches, but this version
does not satisfy the requested adoption conditions:

- slow-position best-move initial/final rank did not improve consistently and often worsened;
- depth-7 aggregate nodes did not reach a 5% reduction;
- wall-clock improvement was modest and not enough to offset the rank failures.

Next search-ordering work should prefer a more selective root mechanism, such as MPC-gated root
verification, rather than globally scoring every root candidate.
