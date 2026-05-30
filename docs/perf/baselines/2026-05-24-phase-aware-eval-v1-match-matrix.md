# Phase-Aware Evaluation v1 Base/Head Match Matrix

Status: historical baseline snapshot.

Recommendations in this snapshot describe follow-up suggested at the time of
collection. They are evidence, not current instructions, unless referenced by
the current user task, an active issue, or current project guidance.

## Environment

- Date: 2026-05-24
- Base: PR109 basic evaluator
- Base commit SHA: `c4c7d0022c0a091bb2694d4962bcd8b4c117912b`
- Head: phase-aware evaluation v1 on latest main with the base/head matrix workflow
- Head commit SHA: `55535a6af98fed2b94b4f41c1eea75274e171d2d`
- Machine: local arm64 macOS development machine
- OS: Darwin 24.6.0
- Compiler: Apple clang 17.0.0 (clang-1700.0.13.5)
- Build type: Release
- Openings: `data/openings/smoke_openings.txt`
- Depths: 4, 6, 8, 10
- Games per depth: 40
- Seed: 20260524
- External timeout: 30000 ms

Machine note: hostnames and other personally identifying machine details are
intentionally omitted from this snapshot.

## Command

Base and head were built as separate external NBoard engine binaries. The raw
JSONL, generated engine configs, command files, summaries, and report were
written under `runs/base-head/phase-aware-eval-v1-matrix/`, which is gitignored.

```sh
cmake -S /tmp/vibe-othello-base-pr109 \
  -B /tmp/vibe-othello-base-pr109/build \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/vibe-othello-base-pr109/build --target othello_nboard_engine

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target othello_match_runner othello_nboard_engine
```

```sh
python3 tools/scripts/base_head_match_matrix.py \
  --base-build /tmp/vibe-othello-base-pr109/build \
  --head-build build \
  --base-repo /tmp/vibe-othello-base-pr109 \
  --head-repo . \
  --openings data/openings/smoke_openings.txt \
  --depths 4,6,8,10 \
  --games 40 \
  --seed 20260524 \
  --external-timeout-ms 30000 \
  --out runs/base-head/phase-aware-eval-v1-matrix
```

## Summary

Head is phase-aware evaluation v1. Base is PR109's basic evaluator.

| depth | games | head wins | base wins | draws | head win rate | avg disc diff from head | avg plies | avg passes | errors | avg head time ms | avg base time ms | notes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| 4 | 40 | 27 | 13 | 0 | 67.50% | 6.55 | 58.40 | 1.68 | 0 | 25.17 | 21.38 | head wins clearly at shallow depth |
| 6 | 40 | 20 | 20 | 0 | 50.00% | -3.00 | 58.40 | 0.50 | 0 | 50.77 | 35.58 | even wins, negative average margin |
| 8 | 40 | 0 | 40 | 0 | 0.00% | -12.00 | 58.40 | 1.00 | 0 | 260.79 | 174.28 | clear regression on this opening set |
| 10 | 40 | 20 | 20 | 0 | 50.00% | 5.00 | 58.40 | 1.00 | 0 | 2687.59 | 1482.46 | even wins, positive average margin, large time cost |

## Per-Opening Summary

### Depth 4

| opening | games | head wins | base wins | draws | avg diff |
| :--- | ---: | ---: | ---: | ---: | ---: |
| initial | 14 | 14 | 0 | 0 | 34.00 |
| c4-c3 | 14 | 7 | 7 | 0 | 1.00 |
| d3-c3-c4 | 12 | 6 | 6 | 0 | -19.00 |

### Depth 6

| opening | games | head wins | base wins | draws | avg diff |
| :--- | ---: | ---: | ---: | ---: | ---: |
| initial | 14 | 7 | 7 | 0 | -3.00 |
| c4-c3 | 14 | 7 | 7 | 0 | -3.00 |
| d3-c3-c4 | 12 | 6 | 6 | 0 | -3.00 |

### Depth 8

| opening | games | head wins | base wins | draws | avg diff |
| :--- | ---: | ---: | ---: | ---: | ---: |
| initial | 14 | 0 | 14 | 0 | -12.00 |
| c4-c3 | 14 | 0 | 14 | 0 | -12.00 |
| d3-c3-c4 | 12 | 0 | 12 | 0 | -12.00 |

### Depth 10

| opening | games | head wins | base wins | draws | avg diff |
| :--- | ---: | ---: | ---: | ---: | ---: |
| initial | 14 | 7 | 7 | 0 | 5.00 |
| c4-c3 | 14 | 7 | 7 | 0 | 5.00 |
| d3-c3-c4 | 12 | 6 | 6 | 0 | 5.00 |

## Interpretation

This is not an Elo estimate. The opening suite is still tiny and deterministic,
but the run is large enough to reject the earlier 12-game smoke as a strength
claim.

At collection time, the result was mixed and inconclusive:

- Depth 4 is favorable to phase-aware eval v1.
- Depth 6 is even on wins and negative on average margin.
- Depth 8 is a clear regression on this opening set.
- Depth 10 is even on wins with a positive average margin.
- There were no error games.
- Head is consistently slower than base, and the depth 10 average time is about
  1.81x base.

The per-opening rows also show highly regular outcomes, which suggested the
smoke opening set used in this snapshot was too narrow for a robust strength
claim. Depth 8 was flagged for investigation before weight tuning could be
treated as a straightforward improvement path.

## Historical Recommendation

At the time of this snapshot, the suggested follow-up was not to tune weights
based only on this run. The suggested next step was to keep phase-aware eval v1
as an experimental baseline and either:

- expand opening coverage and rerun this matrix, or
- add evaluation/root-candidate diagnostics to explain the depth 8 regression, or
- profile/optimize feature calculation if the slowdown blocks deeper testing.

If a broader matrix repeated the depth 8 regression, this snapshot suggested
simplifying or reverting parts of phase-aware eval v1 before tuning numeric
weights.
