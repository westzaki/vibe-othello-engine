# Corner 2x3 Pattern Evaluation Baseline

This note records a preliminary evaluator experiment for the rule-generated
`corner_2x3_pattern` component. It is not an Elo estimate and does not make a
strength claim.

Default evaluator behavior is unchanged. The new pattern component has zero
weight in the default config and is available only through explicit experimental
presets.

## Experiment Source

- Base Git SHA: `2dfe8b1b68ffe2a588f643b5963fc8be6a9b1a3b`
- Experiment source: local working tree based on the SHA above; the same
  `corner_2x3_pattern` changes are committed in this PR.
- Build type: `Release`
- Run dir: `runs/eval/20260530-182735-corner-2x3-pattern`

## Feature Shape

- The table covers one corner-local 2x3 region with `3^6 = 729` entries.
- The canonical a1 cell order is `a1, b1, c1, a2, b2, c2`; other corners are
  mirrored into the same side-relative order.
- Cell encoding is empty `0`, own `1`, opponent `2`.
- The table is generated from conservative rules, not learned data.
- Raw values are clamped to `[-6, 6]`.

## Presets

- `corner_pattern_2x3_v1`
- `corner_pattern_2x3_aggressive`
- `frontier_corner_pattern_2x3_v1`
- `frontier_corner_pattern_edge_lite_v1`

None of these presets is a default-promotion candidate from this run.

## Default Compatibility

Pre/post default smoke checksums matched exactly for:

```sh
./build/othello_search_bench --mode both --depths 1,2,3,4,5 --positions smoke --repetitions 1 --eval-preset default --exact-endgame-threshold 0
```

The depth 1-5 fixed/iterative `result_checksum` and `work_checksum` values were
unchanged from the pre-change baseline.

## Validation Commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m unittest discover tools/scripts/tests
```

Result: C++ tests `165/165` passed; Python tests `98/98` passed.

Analyze breakdown visibility was confirmed with
`--eval-preset corner_pattern_2x3_v1`; the output includes
`corner_2x3_pattern`, `corner_2x3_pattern_weight`, and
`corner_2x3_pattern_score`.

## Match Summary

Reference `frontier_open2_mid2_late_plus1`:

| preset | depth | W-L-D | avg diff | node ratio | time ratio |
| :--- | ---: | :--- | ---: | ---: | ---: |
| `frontier_corner_pattern_2x3_v1` | 7 | 44-52-0 | -0.50 | 0.950 | 0.999 |
| `frontier_corner_pattern_2x3_v1` | 8 | 56-40-0 | 2.33 | 0.948 | 0.979 |
| `frontier_corner_pattern_edge_lite_v1` | 7 | 52-41-3 | 4.38 | 0.936 | 1.019 |
| `frontier_corner_pattern_edge_lite_v1` | 8 | 50-43-3 | 3.58 | 0.916 | 0.990 |

Reference `default`:

| preset | depth | W-L-D | avg diff | node ratio | time ratio |
| :--- | ---: | :--- | ---: | ---: | ---: |
| `frontier_corner_pattern_edge_lite_v1` | 7 | 65-29-2 | 9.96 | 1.005 | 1.063 |
| `frontier_corner_pattern_edge_lite_v1` | 8 | 55-40-1 | 6.02 | 0.981 | 1.064 |
| `classic_features_lite_v1` | 7 | 55-41-0 | 3.50 | 1.001 | 1.074 |
| `classic_features_lite_v1` | 8 | 54-40-2 | 4.04 | 1.086 | 1.154 |
| `frontier_classic_features_lite_v1` | 7 | 55-37-4 | 6.65 | 1.100 | 1.165 |
| `frontier_classic_features_lite_v1` | 8 | 56-38-2 | 7.50 | 1.127 | 1.184 |

Error games were `0`; no node-ratio value crossed the `1.20` concern
threshold. These numbers are preliminary self-play comparison data only.

## Recommendation

Keep the table infrastructure and explicit experimental presets. The most useful
next validation candidate from this run is
`frontier_corner_pattern_edge_lite_v1`, but there is no default promotion and no
strength claim. Next steps should be broader validation, optional NTest sanity
when a local config is available, and then either rule-table retuning or learned
corner 2x3 table preparation.
