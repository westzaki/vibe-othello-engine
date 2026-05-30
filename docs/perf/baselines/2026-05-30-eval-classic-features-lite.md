# Classic Features Lite Evaluation Presets

Status: explicit experimental presets added; default evaluator unchanged.

This is not an Elo estimate or a default-promotion proposal. It records why the
lightweight classic-feature infrastructure is kept for future evaluator
experiments.

## Metadata

- Date: `2026-05-30`
- Base SHA: `dba6dbf04db782c6a62638636b0ab2a92ac36c4b`
- Experiment source: local working tree based on the base SHA with the classic
  feature changes applied before commit.
- Build type: `Release`
- Opening suite: `data/openings/eval_regression_openings.txt`
- Full local report:
  `runs/eval/20260530-155427-classic-features-lite/report.md`

## Features

- `corner_local_2x3`: empty-corner X/C risk plus owned-corner adjacent support.
- `edge_stability_lite`: continuous edge chains anchored at owned corners.

Both default weights are zero, so the existing `default` evaluator keeps the
same scores and search checksums. New scores are visible only when a preset or
custom config assigns nonzero weights.

Raw scale note: `edge_stability_lite` collects corner-anchored edge rays and
counts unique found squares, so a same-color full edge owned from both corners
is not double-counted.

## Presets

Use explicitly:

```sh
--eval-preset classic_features_lite_v1
```

or in match-runner specs:

```text
search:depth=6,tt=on,pvs=on,exact=off,eval=classic_features_lite_v1
```

Added presets:

- `classic_corner_lite_v1`
- `classic_edge_lite_v1`
- `classic_features_lite_v1`
- `classic_features_lite_aggressive`
- `frontier_classic_features_lite_v1`

## Default Compatibility

Smoke benchmark command:

```sh
./build/othello_search_bench \
  --mode both \
  --depths 1,2,3,4,5 \
  --positions smoke \
  --repetitions 1 \
  --eval-preset default \
  --exact-endgame-threshold 0
```

Result: pre/post result and work checksums matched for all fixed/iterative
depth 1-5 rows.

Representative unchanged rows:

| mode | depth | result checksum | work checksum |
| :--- | ---: | ---: | ---: |
| fixed | 5 | `2901503366677567014` | `5975795936820113455` |
| iterative | 5 | `6627488798797056017` | `4582133191318701787` |

## 48-Game Match Smoke

Candidate was player A, default was player B. All runs used
`--swap-sides true`, seed `20260530`, exact endgame disabled, and
`data/openings/eval_regression_openings.txt`. Error games were zero.

| preset | depth | games | A wins | B wins | draws | avg diff A | node ratio A/B |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `classic_features_lite_aggressive` | 5 | 48 | 30 | 17 | 1 | 6.04 | 1.058 |
| `classic_features_lite_aggressive` | 6 | 48 | 28 | 19 | 1 | 9.00 | 1.032 |
| `classic_features_lite_aggressive` | 7 | 48 | 28 | 20 | 0 | 2.50 | 1.069 |
| `classic_features_lite_aggressive` | 8 | 48 | 31 | 15 | 2 | 8.12 | 1.121 |
| `frontier_classic_features_lite_v1` | 5 | 48 | 34 | 12 | 2 | 11.25 | 1.092 |
| `frontier_classic_features_lite_v1` | 6 | 48 | 24 | 21 | 3 | 2.79 | 1.057 |
| `frontier_classic_features_lite_v1` | 7 | 48 | 29 | 17 | 2 | 8.54 | 1.109 |
| `frontier_classic_features_lite_v1` | 8 | 48 | 29 | 18 | 1 | 9.58 | 1.116 |

## Interpretation

The lightweight feature plumbing is correct enough to keep as explicit
experiment infrastructure. The small match results are promising for further
sweeps, especially the frontier + classic combined preset, but they are not
large enough to justify default promotion.
