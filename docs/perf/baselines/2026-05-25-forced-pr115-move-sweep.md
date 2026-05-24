# Forced PR115 Move Sweep

Status: local diagnostic intervention sweep collected.

## Environment

- Date: 2026-05-25 JST
- Branch: `codex/forced-move-intervention`
- Base branch: `origin/main`
- Base/head comparison target: PR109 basic evaluator
- PR109 base commit SHA: `c4c7d0022c0a091bb2694d4962bcd8b4c117912b`
- Head evaluator: unchanged `origin/main` phase-aware evaluator
- Build type: Release
- Openings: `data/openings/smoke_openings.txt`
- Games per row: 40
- Depth: 8
- Seed: `20260524`
- External timeout: 30000 ms

This snapshot uses only forced diagnostic NBoard wrappers. It does not change
evaluator behavior, evaluation weights, search semantics, move ordering, TT,
PVS, aspiration, or exact endgame behavior.

## PR115 Board

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

Legal White moves at this board:

```text
a1 b3 b4 b5 c5 c6 d6
```

The original phase-aware head chose `d6`. PR109 base chose `a1`.

## Forced Single-Move Sweep

For each row, the head engine was wrapped so that when it reached the PR115
board it returned the listed forced move. All other positions were delegated to
the unchanged head NBoard engine.

| forced move at PR115 | games | head wins | base wins | draws | avg diff from head | interpretation |
| :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| `a1` | 40 | 20 | 20 | 0 | 19.00 | best single forced move; immediate corner repairs depth-8 split |
| `b3` | 40 | 20 | 20 | 0 | 11.00 | helps materially, but less than `a1` |
| `b4` | 40 | 20 | 20 | 0 | -1.00 | repairs win split only; average margin remains neutral/negative |
| `b5` | 40 | 20 | 20 | 0 | 17.00 | nearly as good as `a1` in this narrow suite |
| `c5` | 40 | 0 | 40 | 0 | -13.00 | does not repair the regression |
| `c6` | 40 | 0 | 40 | 0 | -21.00 | worse than the original `d6` line |
| `d6` | 40 | 0 | 40 | 0 | -12.00 | original head behavior reproduced |

The result is not "any non-`d6` move is fine." The effective forced moves are
localized: `a1` is the best single intervention, `b5` is close, and `b3` is also
positive. `c5`, `c6`, and `d6` remain bad in this smoke setup.

## Two-Ply Intervention

After forcing `a1`, the next first-divergence board is:

```text
........
........
........
...BW...
..BWW...
..WWW...
.W......
W.......
side=B
```

At that board, unchanged head chose `f4` and PR109 base chose `c2`. A nested
wrapper then forced:

1. White `a1` at the PR115 board.
2. Black `c2` at the post-`a1` divergence board.

Result:

| forced line | games | head wins | base wins | draws | avg diff from head |
| :--- | ---: | ---: | ---: | ---: | ---: |
| `a1` only | 40 | 20 | 20 | 0 | 19.00 |
| `a1`, then `c2` | 40 | 40 | 0 | 0 | 28.00 |

First divergences moved to ply 7 after common line `... a1 c2`:

| ply | side | head move | base move | preceding moves |
| ---: | :--- | :--- | :--- | :--- |
| 7 | white | `c5` | `b1` | `d3 c3 c4 e3 b2 a1 c2` |

This suggests the PR115 `a1` decision is the first major causal point, and the
post-`a1` Black response is another high-impact tactical choice in the same
line.

## Interpretation

Most effective single PR115 intervention: `a1`.

Why:

- It changes the depth-8 smoke matrix from the original `0-40 / -12.00` to
  `20-20 / +19.00`.
- It is the only immediate corner capture and the move PR109 selected at the
  original divergence.
- It outperforms every other single forced move by average margin, though `b5`
  is close on this narrow suite.

Most effective combined intervention: `a1` followed by Black `c2`.

Why:

- It changes the same matrix to `40-0 / +28.00`.
- It shows that the line still contains another high-impact tactical divergence
  after the immediate corner is fixed.

## Recommendation

Use `a1` as the primary must-watch tactical regression in the corner suite, but
do not overfit to `a1` alone. Include at least:

- the PR115 immediate-corner board;
- the post-`a1` Black response board;
- sibling positions where non-corner alternatives like `b5` look surprisingly
  viable;
- negative controls where forcing `c5`, `c6`, or `d6` should not be treated as
  a repair.

The next real evaluator/search change should be judged against this broader
corner tactical suite, not just the PR115 board.
