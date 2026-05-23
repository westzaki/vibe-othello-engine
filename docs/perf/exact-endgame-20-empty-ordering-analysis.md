# Exact Endgame 20-Empty Ordering Analysis

This note reviews the current 20-empty exact endgame tail and chooses one
ordering experiment to try next. It is analysis only; solver behavior, fixture
data, benchmark tools, and public APIs are unchanged.

## Current Ordering

The exact endgame ordering in `src/endgame_ordering.hpp` currently scores each
legal move with these local signals:

- corner moves get a large bonus
- edge moves get a small bonus
- X-squares next to empty corners get a penalty
- moves that give the opponent an immediate legal corner get a large penalty
- moves that leave the opponent many legal moves get a mobility penalty
- moves that force the opponent to pass get a bonus, unless the position is game
  over

The ordering is exact-safe because it only changes traversal order. Root and
interior alpha-beta/PVS still compare exact scores, and no legal move is skipped.

## 20-Empty Bottleneck

The current exact endgame reference is
[`2026-05-24-pr78-8d31a40-exact-endgame-child-breakdown.md`](baselines/2026-05-24-pr78-8d31a40-exact-endgame-child-breakdown.md).
The 20-empty bucket remains the main tail:

| Position | Elapsed ms | Nodes | Best | Margin | TT hit rate |
| --- | ---: | ---: | --- | ---: | ---: |
| 20-empty-high-mobility-lite | 5,960.363 | 54,508,358 | h5 | 12 | 4.95% |
| 20-empty-edge-heavy-low-branching | 3,294.531 | 32,667,823 | a7 | -55 | 7.57% |
| 20-empty-corner-race-lite | 2,072.048 | 16,315,992 | a1 | -2 | 7.82% |
| 20-empty-low-mobility | 1,877.909 | 16,692,266 | h3 | -54 | 7.41% |
| 20-empty-edge-heavy-stress-lite | 1,292.678 | 11,046,330 | b1 | 16 | 5.73% |

The root and child diagnostics show that much of the heavy cost is spent on
non-best candidates:

| Position | Heavy root | Root rank | Heavy child | Child rank | Child is best |
| --- | --- | ---: | --- | ---: | --- |
| 20-empty-high-mobility-lite | f1 | 5 | b2 | 8 | no |
| 20-empty-edge-heavy-low-branching | b6 | 3 | g2 | 10 | no |
| 20-empty-edge-heavy-stress-lite | a4 | 2 | g7 | 10 | no |
| 20-empty-corner-race-lite | a1 | 1 | f7 | 11 | no |
| 20-empty-normal-mobility | g4 | 2 | f8 | 11 | no |

The slowest child candidates usually have several of these properties:

- high current and opponent legal move counts after the root move
- many edge empties
- corner access or competing corner access
- low TT hit rate in the surrounding position
- large enough subtrees that root PVS alone cannot remove the cost

This suggests that the next ordering experiment should make expensive
high-branching non-best candidates appear later, especially in 18/20-empty
positions where PVS can exploit a strong early alpha.

## Alternatives Considered

### Potential Mobility Penalty

A potential-mobility signal could penalize moves that leave many future opponent
options near empty squares. This is plausible, but it would add a new computation
to every ordered candidate. The existing bottleneck data does not yet prove that
potential mobility explains the heavy candidates better than immediate opponent
mobility, which is already computed.

Verdict: useful follow-up if immediate mobility gating is not enough, but too
broad as the first experiment.

### Region / Quasi-Parity Hint

The benchmark metrics expose empty-region count, odd-region count, singleton
regions, and largest region. However, previous simple parity ordering was mixed:
it helped some fixtures and hurt others. The current heavy set also includes
high-mobility and corner-race shapes where region count alone is not the common
signal.

Verdict: reject for the next experiment.

### Edge-Heavy Specific Adjustment

Several heavy fixtures are edge-heavy, and the worst child candidates often sit
in edge-heavy positions. But `20-empty-high-mobility-lite` is the largest normal
tail and the pattern is not only "edge move bad". A broad edge penalty risks
delaying good edge/corner-race moves and fighting the existing edge bonus.

Verdict: too narrow and likely noisy.

### Corner-Race Specific Adjustment

Corner-race logic could target `20-empty-corner-race-lite`, X-square risk, and
opponent corner access. The current ordering already has strong corner,
opponent-corner, and X-square terms. The heaviest bucket,
`20-empty-high-mobility-lite`, is not primarily explained by corner race alone.

Verdict: not the best first experiment.

### Opponent Pass Bonus Retuning

Opponent-pass positions are important for correctness, but the heavy 20-empty
tail is not concentrated in pass-bonus cases. Retuning this bonus is unlikely to
address the high-mobility and edge-heavy child tails.

Verdict: reject.

### X-Square / Corner Access Retuning

Changing the X-square or corner-access constants is risky because those terms
encode strong tactical priorities. The heavy non-best candidates do not show a
clean "X-square constant is too weak" pattern across the whole bucket.

Verdict: reject until a focused corner-access counter shows a clearer miss.

## Chosen Experiment

Try one gated ordering experiment:

**High-mobility gated opponent-mobility pressure.**

The experiment should strengthen the existing opponent mobility penalty only in
high-branching late-endgame contexts. A conservative first gate would be:

- current node empties are at least 18
- current legal move count is high, for example at least 6
- candidate child leaves the opponent with many legal moves, for example at
  least 10

The implementation should reuse already-computed data where possible:

- `ordered_legal_move_indexes()` already has the root/current legal move bitboard
- `move_order_score()` already computes `opponent_moves`
- the extra term can be applied only when the gate is active

The follow-up PR should tune only one or two constants. It should not add region,
potential mobility, edge, and corner-race logic in the same change.

## Expected Impact

This should delay candidates that create broad opponent replies in high-branching
20-empty subtrees. That is the shared pattern behind:

- `20-empty-high-mobility-lite` root `f1` and child `b2`
- `20-empty-edge-heavy-stress-lite` root `a4` and child `g7`
- `20-empty-edge-heavy-low-branching` root `b6` and child `g2`
- `20-empty-normal-mobility` root `g4` and child `f8`

If the best move is found earlier or expensive non-best candidates move later,
root/interior PVS should reject more work after a better alpha is established.
The main success metric is 20-empty max elapsed, not only node count.

## Risks

- Some exact best moves may also leave high opponent mobility, so the gate can
  make ordering worse on tactical corner races.
- A stronger mobility penalty may increase nodes even if elapsed improves on one
  fixture, so the full 14/16/18/20 set must be checked.
- The gate may overfit the current 20-empty fixture set. It should be rejected if
  it only helps `20-empty-high-mobility-lite` while hurting 18-empty or other
  20-empty positions.

## Correctness Considerations

This experiment is exact-safe if it only changes ordering:

- all legal moves must still be searched
- root best move must still be selected by exact score
- equal final margins must still prefer lower square index
- pass handling must remain fake-pass-free in PVs
- TT lookup/store semantics must not change
- no forward pruning, ProbCut, LMR, null-move pruning, or eval-based pruning
  should be introduced

## Follow-Up Benchmark Plan

Stage 0 baseline:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/othello_endgame_bench --empties 14 --repetitions 1
./build-release/othello_endgame_bench --empties 16 --repetitions 1
./build-release/othello_endgame_bench --empties 18 --repetitions 1
./build-release/othello_endgame_bench --empties 20 --repetitions 1
./build-release/othello_endgame_bench --empties 20 --repetitions 1 --root-breakdown --expand-worst-candidate
```

Stage 1 screen the single gated mobility variant:

```sh
./build-release/othello_endgame_bench --empties 20 --repetitions 1
./build-release/othello_endgame_bench --empties 18 --repetitions 1
./build-release/othello_endgame_bench --breakdown-position 20-empty-high-mobility-lite --repetitions 1 --root-breakdown --expand-worst-candidate
./build-release/othello_endgame_bench --breakdown-position 20-empty-edge-heavy-stress-lite --repetitions 1 --root-breakdown --expand-worst-candidate
./build-release/othello_endgame_bench --breakdown-position 20-empty-edge-heavy-low-branching --repetitions 1 --root-breakdown --expand-worst-candidate
```

Stage 2 confirmation only if Stage 1 is promising:

```sh
./build-release/othello_endgame_bench --empties 14 --repetitions 1
./build-release/othello_endgame_bench --empties 16 --repetitions 1
./build-release/othello_endgame_bench --empties 18 --repetitions 1
./build-release/othello_endgame_bench --empties 20 --repetitions 3
./build-release/othello_endgame_bench --positions endgame --repetitions 1
```

Adopt the variant only if 20-empty p95/max elapsed improves clearly, 18-empty is
neutral or better, 14/16 do not meaningfully regress, and best_move/disc_margin
remain stable.
