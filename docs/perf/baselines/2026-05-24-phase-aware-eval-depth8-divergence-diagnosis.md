# Phase-Aware Eval v1 Depth 8 Divergence Diagnosis

Status: historical baseline snapshot.

Recommendations in this snapshot describe follow-up suggested at the time of
collection. They are evidence, not current instructions, unless referenced by
the current user task, an active issue, or current project guidance.

## Environment

- Date: 2026-05-24
- Head engine commit SHA: `48b78bc84c70786dbd52894e8d2f45efd64925de`
- Base engine commit SHA from PR112: `c4c7d0022c0a091bb2694d4962bcd8b4c117912b`
- Match source: `runs/base-head/phase-aware-eval-v1-matrix/depth-8/match.jsonl`
- Match source status: local raw run output, not committed
- Machine: local arm64 macOS development machine
- OS: Darwin 24.6.0
- Compiler: Apple clang 17.0.0 (clang-1700.0.13.5)
- Build type: Release
- Openings: `data/openings/smoke_openings.txt`
- Search settings: depth 8, iterative, TT on, PVS on, aspiration on,
  exact-endgame-threshold 0

Machine note: hostnames and other personally identifying machine details are
intentionally omitted from this snapshot.

## Context

PR112 found a depth 8 regression for phase-aware eval v1 against the PR109 basic
evaluator on the smoke opening suite:

| opening | games | head wins | base wins | draws | avg diff |
| :--- | ---: | ---: | ---: | ---: | ---: |
| initial | 14 | 0 | 14 | 0 | -12.00 |
| c4-c3 | 14 | 0 | 14 | 0 | -12.00 |
| d3-c3-c4 | 12 | 0 | 12 | 0 | -12.00 |

The earlier root-start diagnosis did not show an obvious bad first move. This
snapshot therefore extracts the first position in each swap-side pair where head
and base choose different moves.

Base root-candidate diagnostics were not available. PR109 predates
`othello_analyze_position --root-candidates`, so candidate tables below are
head-only and use the base/head JSONL to identify the actual base move.

## Commands

```sh
python3 tools/scripts/extract_divergence_positions.py \
  --input runs/base-head/phase-aware-eval-v1-matrix/depth-8/match.jsonl \
  --format markdown
```

This is the historical command used when the snapshot was collected. The
Python extractor has since been removed. Current divergence diagnostics should
use `./build/othello_replay_game --match-jsonl` so replay uses the C++ rule
core.

The selected divergence board was analyzed with:

```sh
printf '<divergence_board>' | ./build/othello_analyze_position \
  --stdin \
  --depth 8 \
  --mode iterative \
  --tt on \
  --pvs on \
  --aspiration on \
  --exact-endgame-threshold 0 \
  --root-candidates
```

The same root-candidate diagnostic was also run at depth 10 as a stability
check. Raw analyze output was kept under `/private/tmp` and is not committed.

## Extraction Result

All 20 swap-side pairs in the depth 8 JSONL first diverged at the same board.
The three smoke openings transpose into this position by ply 5.

| pair | opening | head game | base game | ply | side | head move | base move | head diff | paired head diff | error | preceding moves |
| ---: | :--- | ---: | ---: | ---: | :--- | :--- | :--- | ---: | ---: | :--- | :--- |
| 0 | initial | 1 | 0 | 5 | white | d6 | a1 | -8 | -16 | no | d3 c3 c4 e3 b2 |
| 1 | c4-c3 | 3 | 2 | 5 | white | d6 | a1 | -8 | -16 | no | c4 c3 d3 e3 b2 |
| 2 | d3-c3-c4 | 5 | 4 | 5 | white | d6 | a1 | -8 | -16 | no | d3 c3 c4 e3 b2 |

The remaining pairs repeat the same divergence pattern:

- opening `initial`: 7 pairs, head `d6`, base `a1`
- opening `c4-c3`: 7 pairs, head `d6`, base `a1`
- opening `d3-c3-c4`: 6 pairs, head `d6`, base `a1`

## Divergence Position

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

White to move has an immediate corner move on `a1`. The phase-aware head chose
`d6`; the PR109 basic-evaluator base chose `a1`.

## Head Root Candidates At Depth 8

| rank | move | search score | PV | eval total after move | disc score | mobility score | potential mobility score | corner occupancy score | corner access score | x-square danger score | frontier score | nodes | note |
| ---: | :--- | ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| 1 | d6 | 68 | d6->f3->e2->f4->c5->d2->f2->f6 | 23 | 0 | -8 | -12 | 0 | 30 | 25 | -12 | 23351 | head move |
| 2 | c5 | 61 | c5->e2->d2->f4->b4->d1->b3->f3 | 22 | 0 | -8 | -16 | 0 | 30 | 25 | -9 | 23618 |  |
| 3 | c6 | 42 | c6->d2->b3->f2->e2->f6->a1->e1 | 5 | 0 | -24 | -20 | 0 | 30 | 25 | -6 | 27958 |  |
| 4 | b3 | 27 | b3->c2->b4->d2->e2->c5->e1->d1 | 25 | 0 | -16 | -8 | 0 | 30 | 25 | -6 | 29654 |  |
| 5 | b5 | 25 | b5->f2->d6->c5->b4 | 13 | 0 | -16 | -20 | 0 | 30 | 25 | -6 | 28201 |  |
| 6 | b4 | 19 | b4->c5->d2->a4->a1->e2 | 35 | 0 | 8 | -16 | 0 | 30 | 25 | -12 | 41701 |  |
| 7 | a1 | -3 | a1->e2->b4->b3->c5->f3->a2->a3 | -31 | 0 | -8 | -40 | 35 | 0 | 0 | -18 | 13663 | base move |

Head move rank: 1. Base move rank in head search: 7. Score gap from head move
to base move: 71 centipawn-like evaluator units.

## Head Root Candidates At Depth 10

| rank | move | search score | PV | eval total after move | disc score | mobility score | potential mobility score | corner occupancy score | corner access score | x-square danger score | frontier score | nodes | note |
| ---: | :--- | ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| 1 | c5 | 68 | c5->e2->d2->e1->c1->c2 | 22 | 0 | -8 | -16 | 0 | 30 | 25 | -9 | 294217 |  |
| 2 | d6 | 56 | d6->f3->e2->f4->g2->f6->c2->e1->f5->c1 | 23 | 0 | -8 | -12 | 0 | 30 | 25 | -12 | 235048 | head depth-8 move |
| 3 | c6 | 50 | c6->d2->b3->d6->c5->f4->a1->b5->d1->b4 | 5 | 0 | -24 | -20 | 0 | 30 | 25 | -6 | 340162 |  |
| 4 | b5 | 34 | b5->b3->c5->e2->d2->c2->a3->b4->a4->f6 | 13 | 0 | -16 | -20 | 0 | 30 | 25 | -6 | 357531 |  |
| 5 | b4 | 28 | b4->b3->c5->e2->d2->f4->b5->f2->a1->a4 | 35 | 0 | 8 | -16 | 0 | 30 | 25 | -12 | 362627 |  |
| 6 | b3 | 26 | b3->c2->b5->d2->e2->a3->c5->c6->c1 | 25 | 0 | -16 | -8 | 0 | 30 | 25 | -6 | 259487 |  |
| 7 | a1 | 1 | a1->c2->b1->d2->e2->f6->b3->f3->g3->d1 | -31 | 0 | -8 | -40 | 35 | 0 | 0 | -18 | 142339 | base move |

The depth 10 candidate check did not rescue `a1` under the phase-aware evaluator
at this checkpoint. `a1` remained last in the head candidate table.

## Observations

- This is a much sharper diagnostic than the starting-position snapshot: all
  depth 8 smoke pairs first diverge at the same board.
- The base move is an immediate corner capture on `a1`. The phase-aware head
  strongly prefers not taking it.
- In the immediate post-move breakdown, every non-corner candidate keeps
  `corner_access_score = 30` and `x_square_danger_score = 25`. Taking `a1`
  receives `corner_occupancy_score = 35`, but loses those two terms and also has
  worse `potential_mobility_score` and `frontier_score`.
- This means the captured breakdown makes "retaining a corner threat while the
  opponent owns the adjacent X-square" look better than actually taking the
  corner in this position.
- The sign conventions appear internally consistent with the documented v1
  feature definitions. This does not look like a clear implementation bug such
  as reversed sign, wrong corner mapping, or bitboard wrapping.
- The root-candidate `evaluation_after_move` is reported from the original root
  side's perspective. For child boards where the opponent is now to move,
  `corner_access_score` should be read as "the root side has a legal corner move
  in this static child board", not necessarily "the root side can play that
  corner immediately next".

## Classification

Likely tuning / feature-semantics issue, not a clear implementation bug.

The suspicious interaction is:

- `corner_access` rewards the still-available `a1` corner after non-corner moves;
- `x_square_danger` rewards Black's `b2` disc while `a1` remains empty;
- `corner_occupancy` does not compensate enough when White actually takes `a1`;
- potential mobility and frontier also penalize the immediate corner capture in
  this position.

Because those signs matched the feature definitions at this checkpoint, this PR
does not tune weights or change evaluator behavior.

## Historical Recommendation

At the time of this snapshot, the suggested follow-up was to change one
evaluation term only and rerun the depth 8 base/head matrix. The most focused
candidates were:

- reduce or gate `corner_access` so it does not overvalue a corner that the side
  declines to take when the opponent moves next;
- increase `corner_occupancy` relative to `corner_access + x_square_danger`;
- or make `x_square_danger` less valuable once the side already has the
  adjacent corner as an immediate legal move.

This snapshot also suggested adding one direct regression test around this
divergence position before tuning so the chosen behavior would be intentional
and reviewable.
