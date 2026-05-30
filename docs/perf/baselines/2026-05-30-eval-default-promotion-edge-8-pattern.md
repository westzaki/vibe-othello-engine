# Edge 8 Pattern Default Promotion Baseline

Status: `edge_8_pattern` was promoted into the default evaluator after a gated
local validation run.

This is not an Elo estimate and does not make a broad strength claim. It records
the evidence used for this default evaluator change and the limits of that
evidence.

## Source

- Validation base SHA: `890694f442f10c93de2c4345aa94e58614791b4e`
- Experiment source: local working tree based on the validation base SHA with
  the edge 8 pattern changes applied before commit.
- PR head SHA: see PR metadata. The full validation matrix was run from the
  local working tree above before commit; after the default promotion wiring was
  finalized, correctness checks and default smoke were rerun on the final code
  state.
- Validation note: the promoted default uses the same conservative config that
  was validated as `default_edge_pattern_8_v1`; the full self-play matrix was
  not rerun after committing the PR branch.
- Build type: `Release`
- Run dir: `runs/eval/20260530-222837-edge-8-pattern`
- Opening suite: `data/openings/eval_regression_openings.txt`
- Exact endgame threshold for self-play validation: `0`

## Feature Shape

- `edge_8_pattern` indexes one full edge as `3^8 = 6561` side-relative states.
- Cell encoding is empty `0`, own `1`, opponent `2`.
- Canonical orders are stable:
  - top: `a1 b1 c1 d1 e1 f1 g1 h1`
  - bottom: `a8 b8 c8 d8 e8 f8 g8 h8`
  - left: `a1 a2 a3 a4 a5 a6 a7 a8`
  - right: `h1 h2 h3 h4 h5 h6 h7 h8`
- The table is rule-generated, not learned.
- Per-edge raw table values are clamped to `[-10, 10]`.
- Rules are conservative: corner ownership, anchored edge chains, full edges,
  empty-corner C-square risk, and mild unanchored-edge penalties.

## Presets

Added presets:

- `edge_pattern_8_v1`
- `edge_pattern_8_aggressive`
- `default_edge_pattern_8_v1`
- `default_edge_pattern_8_no_edge_lite`
- `default_edge_pattern_8_aggressive`

The previous promoted default remains available as
`frontier_corner_pattern_edge_lite_v1`; that preset keeps `edge_8_pattern = 0`.

## Default Change

Default now adds conservative `edge_8_pattern` weights on top of the previous
frontier + corner pattern + edge stability evaluator:

| phase | edge_8_pattern |
| :--- | ---: |
| opening | 2 |
| midgame | 4 |
| late | 6 |

Pre-promotion smoke checksum rows for the previous default:

| mode | depth | result checksum | work checksum |
| :--- | ---: | :--- | :--- |
| fixed | 5 | `2900483029563250214` | `6065216869992930095` |
| iterative | 5 | `6629072083868305425` | `4717395135335471579` |

Post-promotion smoke checksum rows:

| mode | depth | result checksum | work checksum |
| :--- | ---: | :--- | :--- |
| fixed | 5 | `2902611684074624550` | `5828777889555978287` |
| iterative | 5 | `6635352494286161937` | `4994366512418752859` |

The checksum change is intentional and comes from the default evaluator semantic
change.

## Validation Commands

Correctness and wiring:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m unittest discover tools/scripts/tests
```

Default smoke:

```sh
./build/othello_search_bench --mode both --depths 1,2,3,4,5 --positions smoke --repetitions 1 --eval-preset default --exact-endgame-threshold 0
```

Primary validation used `default_edge_pattern_8_v1` against the previous
`default` with exact endgame disabled:

```sh
python3 tools/scripts/eval_experiment_matrix.py \
  --presets edge_pattern_8_v1,edge_pattern_8_aggressive,default_edge_pattern_8_v1,default_edge_pattern_8_no_edge_lite,default_edge_pattern_8_aggressive \
  --reference-preset default \
  --small-depths 5,6 \
  --extended-depths 7,8 \
  --small-games 96 \
  --extended-games 192 \
  --promote-top 3 \
  --max-node-ratio 1.20 \
  --min-avg-diff 0.0 \
  --require-nonnegative-diff true \
  --openings data/openings/eval_regression_openings.txt \
  --seed 20260604 \
  --build-dir build \
  --out runs/eval/20260530-222837-edge-8-pattern/vs-default \
  --positions suite \
  --by-position
```

## Primary Validation Summary

Reference: previous `default`.

| preset | depth | games | W-L-D | avg diff | node ratio | time ratio | errors |
| :--- | ---: | ---: | :--- | ---: | ---: | ---: | ---: |
| `default_edge_pattern_8_v1` | 5 | 96 | 50-46-0 | 0.92 | 1.031 | 1.036 | 0 |
| `default_edge_pattern_8_v1` | 6 | 96 | 49-42-5 | 0.98 | 0.977 | 1.007 | 0 |
| `default_edge_pattern_8_v1` | 7 | 192 | 110-79-3 | 6.70 | 1.019 | 1.053 | 0 |
| `default_edge_pattern_8_v1` | 8 | 192 | 95-87-10 | 1.81 | 0.907 | 0.941 | 0 |
| `default_edge_pattern_8_aggressive` | 7 | 192 | 97-92-3 | 1.75 | 0.934 | 0.977 | 0 |
| `default_edge_pattern_8_aggressive` | 8 | 192 | 111-78-3 | 4.15 | 0.904 | 0.933 | 0 |

The conservative candidate was selected for default promotion because it met the
gate while changing the previous default less aggressively.

## Secondary Phase-Aware Check

Reference: `phase_aware_v1`.

| preset | depth | games | W-L-D | avg diff | node ratio | time ratio | errors |
| :--- | ---: | ---: | :--- | ---: | ---: | ---: | ---: |
| `default` before this PR | 7 | 192 | 130-57-5 | 9.78 | 1.006 | 1.064 | 0 |
| `default` before this PR | 8 | 192 | 112-78-2 | 5.99 | 0.973 | 1.051 | 0 |
| `default_edge_pattern_8_v1` | 7 | 192 | 119-71-2 | 5.18 | 0.955 | 1.052 | 0 |
| `default_edge_pattern_8_v1` | 8 | 192 | 101-79-12 | 5.29 | 0.878 | 0.983 | 0 |

This was a sanity comparison to the pre-PR131 evaluator, not the main promotion
gate.

## Cross-Seed Check

Reference: previous `default`; seed `20260605`.

| preset | depth | games | W-L-D | avg diff | node ratio | time ratio | errors |
| :--- | ---: | ---: | :--- | ---: | ---: | ---: | ---: |
| `default_edge_pattern_8_v1` | 7 | 192 | 110-79-3 | 6.70 | 1.019 | 1.055 | 0 |
| `default_edge_pattern_8_v1` | 8 | 384 | 188-175-21 | 1.47 | 0.915 | 0.947 | 0 |

## NTest Sanity

NTest no-book sanity was run with local NTest depth 2, Vibe depth 6, 12 games,
swap-sides enabled, and `data/openings/eval_regression_openings.txt`.

| preset | games | W-L-D vs NTest | avg diff | errors |
| :--- | ---: | :--- | ---: | ---: |
| previous `default` | 12 | 0-12-0 | -30.50 | 0 |
| `default_edge_pattern_8_v1` | 12 | 2-10-0 | -28.00 | 0 |

This is only a small sanity check. It does not mean the engine is expected to
beat NTest.

## Risks

- The table is rule-generated, not learned.
- The component may double-count some edge/corner concepts already represented
  by `corner_occupancy`, `corner_2x3_pattern`, and `edge_stability_lite`.
- Opening coverage is still limited to the regression opening suite.
- Self-play can overfit to the current search and evaluation family.
- NTest sanity is tiny and should not be treated as an external strength claim.

## Recommendation

Promote the conservative edge 8 pattern weights into default and keep the
explicit presets for follow-up validation. Next steps should be broader
validation, more NTest sanity, rule-table retuning if depth-specific reversals
appear, and only then consideration of larger edge or learned pattern tables.
