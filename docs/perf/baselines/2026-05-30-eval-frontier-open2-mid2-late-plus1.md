# Frontier Open2/Mid2/Late1 Evaluation Preset

Status: selected as an explicit experimental preset; default evaluator unchanged.

This is not an Elo estimate. It records the local evidence for preserving
`frontier_open2_mid2_late_plus1` as a named preset.

## Metadata

- Date: `2026-05-30`
- Base SHA: `d3c56408fd915c103fcf5c609d74011cbe89676f`
- PR head SHA for the evaluated code: `3d1b6b4e25dbaaa625df5165b3374d13f9fb64d4`
- Experiment source: local working tree based on the base SHA with the frontier
  preset changes applied before they were committed. Those same evaluator code
  changes were later committed in this PR as
  `3d1b6b4e25dbaaa625df5165b3374d13f9fb64d4`; later documentation-only commits
  in the PR do not change the measured code behavior.
- Build type: `Release`
- Opening suite: `data/openings/eval_regression_openings.txt`
- Full local report: `runs/eval/20260530-151321-frontier-refinement-sweep/report.md`

## Preset

`frontier_open2_mid2_late_plus1` changes only frontier weights:

| phase | default | preset |
| :--- | ---: | ---: |
| opening | 3 | 5 |
| midgame | 4 | 6 |
| late | 2 | 3 |

Use it explicitly:

```sh
--eval-preset frontier_open2_mid2_late_plus1
```

or in match runner specs:

```text
search:depth=6,tt=on,pvs=on,exact=off,eval=frontier_open2_mid2_late_plus1
```

## 96-Game Internal Match Check

Candidate was player A, default was player B. All runs used
`--swap-sides true`, seed `20260530`, exact endgame disabled, and
`data/openings/eval_regression_openings.txt`.

| depth | games | A wins | B wins | draws | avg diff A | node ratio A/B | errors |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 | 96 | 51 | 45 | 0 | 4.06 | 1.061 | 0 |
| 6 | 96 | 54 | 38 | 4 | 4.77 | 1.018 | 0 |
| 7 | 96 | 61 | 33 | 2 | 7.06 | 1.035 | 0 |
| 8 | 96 | 48 | 44 | 4 | 4.19 | 1.055 | 0 |

## Base/Head External Process Check

Base was the base SHA with `--eval-preset default`. Head was a local working
tree based on the base SHA with the frontier preset changes applied, using
`--eval-preset frontier_open2_mid2_late_plus1`. Both used
`--exact-endgame-threshold 0`. The generated local base/head report may show the
head git SHA as the base SHA because the experiment ran before the frontier
preset changes were committed.

| depth | games | head wins | base wins | draws | avg diff head | errors |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 4 | 48 | 28 | 19 | 1 | 5.25 | 0 |
| 6 | 48 | 28 | 18 | 2 | 4.88 | 0 |
| 8 | 48 | 25 | 21 | 2 | 6.00 | 0 |

## Interpretation

The preset met the local adoption bar for a named experimental preset: error
games were zero, depth 5-8 internal checks were non-worse or positive, and the
external-process base/head check was positive at depths 4/6/8.

It should not be promoted to default yet. The opening suite is broader than the
old smoke suite but still small, and the preset costs roughly 2-6% more nodes in
the internal 96-game checks.
