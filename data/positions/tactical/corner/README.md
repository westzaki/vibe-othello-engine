# Corner Tactical Regression Positions

This directory contains small diagnostic positions for corner-capture tactics.
They are intended to protect the phase-aware evaluation investigation from
overfitting one smoke-opening line while still keeping the evidence easy to
rerun.

These positions are not a general strength benchmark. They are a focused
tactical safety net for evaluator and search experiments that might affect
immediate corner capture, X-square danger, potential mobility, frontier, or
corner access behavior.

## Positions

- `pr115_immediate_corner.txt`: White to move. Legal moves include `a1`. PR115
  found that the phase-aware evaluator chose `d6` here while the PR109 basic
  evaluator chose the immediate corner `a1`.
- `pr115_after_a1_black_response.txt`: Black to move after forced White `a1`.
  PR116 forced-intervention diagnostics found `c2` to be another high-impact
  tactical response in the repaired line.

## Known Forced-Intervention Evidence

Depth-8 smoke-opening results from the PR116 diagnostic wrapper:

| forced move at PR115 board | result | avg diff from head |
| :--- | :--- | ---: |
| none / original `d6` behavior | 0-40 | -12.00 |
| `a1` | 20-20 | 19.00 |
| `b5` | 20-20 | 17.00 |
| `b3` | 20-20 | 11.00 |
| `b4` | 20-20 | -1.00 |
| `c5` | 0-40 | -13.00 |
| `c6` | 0-40 | -21.00 |
| `d6` | 0-40 | -12.00 |

Forcing White `a1` and then Black `c2` at the next divergence improved the same
depth-8 smoke matrix to 40-0 / +28.00.

These results are causal diagnostics, not Elo estimates. The smoke openings are
narrow, and the wrapper adds process overhead.

## Analyze Commands

```bash
./build/othello_analyze_position \
  --board-file data/positions/tactical/corner/pr115_immediate_corner.txt \
  --depth 8 \
  --mode iterative \
  --tt on \
  --pvs on \
  --aspiration on \
  --exact-endgame-threshold 0 \
  --root-candidates
```

```bash
./build/othello_analyze_position \
  --board-file data/positions/tactical/corner/pr115_after_a1_black_response.txt \
  --depth 8 \
  --mode iterative \
  --tt on \
  --pvs on \
  --aspiration on \
  --exact-endgame-threshold 0 \
  --root-candidates
```

## Current Diagnostics

Use `othello_analyze_position --root-candidates` for focused root-move
inspection, and use `othello_match_runner` or the base/head workflow for
match-level comparisons. The forced-intervention results above are historical
causal diagnostics only; they are not a current workflow.

Use sibling and negative-control outcomes when judging future changes. A change
that only rescues `a1` on this exact board may still be too narrow.
