# Phase-Aware Eval v1 Investigation

Status: active investigation notes.

This page summarizes the key evidence from the phase-aware evaluation v1
depth-8 regression work. Detailed local JSONL, generated engine configs, command
files, and per-run summaries should stay under `runs/`. Use
`docs/perf/baselines/` only for stable benchmark baselines that are expected to
be compared repeatedly.

## Context

PR112 found a deterministic smoke-opening regression for phase-aware eval v1
against the PR109 basic evaluator:

| run | depth | games | head wins | base wins | draws | avg diff from head |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| original PR112 matrix | 8 | 40 | 0 | 40 | 0 | -12.00 |

PR115 extracted the first divergence positions from that depth-8 JSONL. All 20
swap-side pairs first diverged at the same ply-5 board:

```text
........
........
........
...BW...
..BBW...
..BWW...
.B......
........
side=W
```

The phase-aware head chose `d6`; the PR109 basic-eval base chose the immediate
corner `a1`.

## Forced-Move Evidence

The forced-move NBoard wrapper is diagnostic-only. It does not change evaluator
behavior, search behavior, move ordering, TT/PVS/aspiration behavior, exact
endgame behavior, or default search options.

Forcing the PR109 move at the PR115 board materially repairs the depth-8 smoke
matrix:

| intervention | depth | games | head wins | base wins | draws | avg diff from head |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| none, original PR112 behavior | 8 | 40 | 0 | 40 | 0 | -12.00 |
| force White `a1` at PR115 board | 8 | 40 | 20 | 20 | 0 | 19.00 |
| force White `a1`, then Black `c2` at next divergence | 8 | 40 | 40 | 0 | 0 | 28.00 |

The first intervention confirms that the immediate-corner decision is a strong
causal signal for the observed depth-8 regression on the smoke openings. The
two-ply intervention shows there is another high-impact tactical decision in
the same line after `a1`.

This is not an Elo estimate. The smoke openings are narrow, and the forced
wrapper adds process/proxy overhead, so timings from these runs are not engine
speed comparisons.

## Ablation Findings

Local one-feature ablations were useful diagnostically but were not mergeable as
strength changes:

- Removing potential mobility made the PR115 root search prefer `a1`, but it
  was worse than the normal evaluator in internal smoke and did not hold up at
  depth 10 against PR109.
- Stronger corner occupancy repaired the depth-8 split in one narrow smoke run,
  but collapsed at depth 10.
- Removing X-square danger was not enough to rescue the immediate-corner
  behavior.
- A local owned-corner potential mobility gate moved `a1` in the intended
  direction, but depth 8 still remained `0-40`.
- Immediate corner-access semantics were cleaner locally, but the combined
  smoke result worsened at depth 8.

## Current Interpretation

The PR115 `a1` decision is very likely a primary cause of the phase-aware
depth-8 smoke regression, but that does not prove any specific evaluator change
is correct. The current evidence suggests the issue is an interaction between
immediate corner capture, X-square danger, corner access, potential mobility,
and frontier, not a single obvious sign or bitboard bug.

## Recommendation

Next PR: add a corner tactical regression suite before changing evaluator or
search behavior again.

That suite should include:

- the PR115 immediate-corner board;
- the post-`a1` Black response board;
- several small immediate-corner capture positions;
- sibling positions where non-corner alternatives appear viable;
- negative controls where a superficially similar non-corner move should not be
  treated as a repair.

Use the suite to judge future one-feature evaluation or search-ordering
experiments. Avoid optimizing only for the single PR115 board.
