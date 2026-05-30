# Phase-Aware Eval v1 Depth 8 Root Candidate Diagnosis

Status: historical baseline snapshot.

Recommendations in this snapshot describe follow-up suggested at the time of
collection. They are evidence, not current instructions, unless referenced by
the current user task, an active issue, or current project guidance.

## Environment

- Date: 2026-05-24
- Head commit SHA: `48b78bc84c70786dbd52894e8d2f45efd64925de`
- Base evidence: PR112 base/head matrix against PR109 basic evaluator
- Base commit SHA from PR112: `c4c7d0022c0a091bb2694d4962bcd8b4c117912b`
- Base root-candidate diagnostics: unavailable; PR109 predates
  `othello_analyze_position --root-candidates`
- Machine: local arm64 macOS development machine
- OS: Darwin 24.6.0
- Compiler: Apple clang 17.0.0 (clang-1700.0.13.5)
- Build type: Release
- Openings: `data/openings/smoke_openings.txt`
- Search settings: iterative, TT on, PVS on, aspiration on,
  exact-endgame-threshold 0

Machine note: hostnames and other personally identifying machine details are
intentionally omitted from this snapshot.

## Context

PR112 found a depth 8 regression for phase-aware eval v1 on the smoke opening
suite:

| opening | games | head wins | base wins | draws | avg diff |
| :--- | ---: | ---: | ---: | ---: | ---: |
| initial | 14 | 0 | 14 | 0 | -12.00 |
| c4-c3 | 14 | 0 | 14 | 0 | -12.00 |
| d3-c3-c4 | 12 | 0 | 12 | 0 | -12.00 |

This snapshot uses the PR113 root-candidate analysis tool on the phase-aware
evaluator at this checkpoint to inspect the candidate search scores and immediate
post-move evaluation breakdowns. It does not compare base candidate rows because
the base binary did not have the root-candidate diagnostic option.

Root candidate analysis searches each legal candidate independently. The
candidate score and PV are useful for explaining choices, but per-candidate
nodes, TT stats, and PVS stats are diagnostic and may not exactly match the work
trace of one normal shared-context root search.

## Commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The analyzed board strings were obtained from the smoke opening suite through
the existing match runner:

```sh
./build/othello_match_runner \
  --black first \
  --white first \
  --games 6 \
  --swap-sides true \
  --seed 1 \
  --openings data/openings/smoke_openings.txt \
  --output /private/tmp/vibe-othello-smoke-openings.jsonl \
  --quiet
```

Each selected board was analyzed with:

```sh
printf '<board_from_opening_suite>' | ./build/othello_analyze_position \
  --stdin \
  --depth 8 \
  --mode iterative \
  --tt on \
  --pvs on \
  --aspiration on \
  --exact-endgame-threshold 0 \
  --root-candidates
```

The same diagnostic was also run at depth 10 for a quick stability check.

## Positions

### initial

```text
........
........
........
...BW...
...WB...
........
........
........
side=B
```

### c4-c3

```text
........
........
........
...BW...
..BWB...
..W.....
........
........
side=B
```

### d3-c3-c4

```text
........
........
........
...BW...
..BBB...
..WB....
........
........
side=W
```

## Depth 8 Candidate Tables

### initial

| rank | move | search score | PV | eval total after move | mobility score | potential mobility score | corner access score | x-square danger score | frontier score | nodes |
| ---: | :--- | ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | d3 | 20 | d3->c5->e6->d2->c2->f5->d6->c3 | -41 | 0 | -32 | 0 | 0 | -9 | 5065 |
| 2 | c4 | 20 | c4->e3->f5->b4->b3->e6->f4->c3 | -41 | 0 | -32 | 0 | 0 | -9 | 5996 |
| 3 | f5 | 20 | f5->d6->c4->f3->f4->b3->f2->d3 | -41 | 0 | -32 | 0 | 0 | -9 | 6145 |
| 4 | e6 | 20 | e6->f4->d3->e7->f7->c4->e3->f6 | -41 | 0 | -32 | 0 | 0 | -9 | 6395 |

### c4-c3

| rank | move | search score | PV | eval total after move | mobility score | potential mobility score | corner access score | x-square danger score | frontier score | nodes |
| ---: | :--- | ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | d3 | 21 | d3->e3->c2->d6->f4->c5->f6->b3 | 7 | 32 | -16 | 0 | 0 | -9 | 8800 |
| 2 | f5 | 1 | f5->c5->d3->e6->b5->g4->b3->e3 | -53 | -16 | -28 | 0 | 0 | -9 | 7571 |
| 3 | e6 | 0 | e6->b4->d3->e2->d2->e1->d1->e3 | -53 | -16 | -28 | 0 | 0 | -9 | 9920 |
| 4 | c2 | -67 | c2->c5->e6->d6->b5->b6->d3->e3 | -61 | -16 | -36 | 0 | 0 | -9 | 4831 |

### d3-c3-c4

| rank | move | search score | PV | eval total after move | mobility score | potential mobility score | corner access score | x-square danger score | frontier score | nodes |
| ---: | :--- | ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | e3 | -10 | e3->b2->a1->e2->b4->b3->d2->d1 | -62 | -32 | -24 | 0 | 0 | -6 | 24580 |
| 2 | c5 | -10 | c5->d6->e3->b5->e6->f6 | -62 | -32 | -24 | 0 | 0 | -6 | 31147 |

## Depth 10 Stability Check

### initial

| rank | move | search score | PV | eval total after move | mobility score | potential mobility score | corner access score | x-square danger score | frontier score | nodes |
| ---: | :--- | ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | d3 | 21 | d3->c3->c4->e3->c2->d6->f4->c5->f6->b3 | -41 | 0 | -32 | 0 | 0 | -9 | 32909 |
| 2 | c4 | 21 | c4->c3->d3->e3->c2->d6->f4->c5->f6->b3 | -41 | 0 | -32 | 0 | 0 | -9 | 35478 |
| 3 | f5 | 21 | f5->f4->e3->f6->d3->c4->f3->c2->d2->c3 | -41 | 0 | -32 | 0 | 0 | -9 | 41937 |
| 4 | e6 | 21 | e6->f4->c3->c4->d3->d6->e3->c2->b5->c5 | -41 | 0 | -32 | 0 | 0 | -9 | 38944 |

### c4-c3

| rank | move | search score | PV | eval total after move | mobility score | potential mobility score | corner access score | x-square danger score | frontier score | nodes |
| ---: | :--- | ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | d3 | 22 | d3->e3->c2->d6->f3->f2->f6->f4->g3->b1 | 7 | 32 | -16 | 0 | 0 | -9 | 85148 |
| 2 | f5 | -9 | f5->b4->d3->e3->d2->c5 | -53 | -16 | -28 | 0 | 0 | -9 | 60414 |
| 3 | e6 | -14 | e6->b4->d3->e2->c2->e3->d2->c5->f2->c1 | -53 | -16 | -28 | 0 | 0 | -9 | 44638 |
| 4 | c2 | -72 | c2->c5->c6->b5->e6->b4->a5->a4 | -61 | -16 | -36 | 0 | 0 | -9 | 67933 |

### d3-c3-c4

| rank | move | search score | PV | eval total after move | mobility score | potential mobility score | corner access score | x-square danger score | frontier score | nodes |
| ---: | :--- | ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | e3 | -8 | e3->b2->a1->d2->b4->f4->e2->b3->d1->c2 | -62 | -32 | -24 | 0 | 0 | -6 | 238862 |
| 2 | c5 | -8 | c5->d6->e3->f3->f4->d2->c2->f2->c6->b3 | -62 | -32 | -24 | 0 | 0 | -6 | 297008 |

## Observations

- The initial position does not explain the depth 8 regression at the root.
  Every legal move has the same depth 8 score and identical immediate
  phase-aware breakdown. Depth 10 remains similarly tied.
- `c4-c3` is not an obvious case where the immediate phase-aware breakdown
  prefers a bad searched move. `d3` has both the best search score and the best
  immediate evaluation, mostly from actual mobility.
- `d3-c3-c4` is also not decisive at the root. Both legal moves tie at depth 8
  and depth 10, and their immediate breakdowns are identical.
- The selected root positions do not exercise corner access or X-square danger;
  both scores are zero in these candidate rows.
- The nonzero early-position terms here are mainly actual mobility, potential
  mobility, and frontier. Frontier is small and consistently negative, while
  potential mobility is a larger negative term in all three openings.
- The depth 10 candidate check does not show a clear correction of a depth 8
  root blunder. The regression likely emerges later in the deterministic games
  or from a base/head divergence that is not visible from these starting root
  rows alone.

## Interpretation

This diagnosis is inconclusive. It did not identify a single evaluation feature
or weight for immediate tuning.

The most useful finding is negative: the depth 8 PR112 regression is probably
not caused by an obvious first root move in the three smoke opening start
positions. The suggested follow-up at the time was to inspect later positions
from the actual depth 8 base/head games, especially the first positions where
head and base choose different moves, or temporarily apply the PR113
root-candidate diagnostic tooling to the PR109 basic-evaluator base for direct
candidate-by-candidate comparison.

## Historical Recommendation

At the time of this snapshot, the suggested follow-up was not to tune
phase-aware weights from these three root snapshots alone. A better follow-up
would have done one of:

- extract divergence positions from the depth 8 base/head match JSONL and run
  root-candidate diagnostics there;
- create a temporary diagnostic base branch that combines PR109 evaluation with
  PR113 root-candidate tooling, then compare candidate rows directly;
- broaden the opening suite before tuning if the smoke openings prove too
  narrow to localize the regression.

The snapshot suggested changing one evaluation term at a time only after a
specific feature or position class was implicated.
