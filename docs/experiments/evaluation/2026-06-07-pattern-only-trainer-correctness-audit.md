# Pattern-Only Trainer Correctness Audit

Date: 2026-06-07

This is a correctness audit for `tools/scripts/regularized_pairwise_pattern_train.py`.
It does not run large-scale training, does not change `data/eval/current_default.eval`,
and does not add a retained preset.

## Hypotheses Checked

- Sign inversion: covered by synthetic one-feature pair tests and a hand-made
  `d1` preferred vs `b1` other root position. The trainer feature delta remains
  `other_child - preferred_child`, matching negated child-side root scores.
- Feature delta: covered by `preference_features()` tests that compare directly
  against independent child pattern counts.
- Phase assignment: covered by synthetic opening, midgame, and late pairs plus
  the existing child-phase boundary test for generated preference pairs.
- Output quantization: covered by a Python TSV roundtrip test with
  `output_scale=10`, asserting `0.5` renders as integer `5` within scale
  tolerance.
- Runtime evaluator reflection: covered by a C++ golden test that loads a
  generated-style `candidate.eval` and TSV through `PatternTableBundle`, then
  checks child eval scores produce the same root preference sign.

## Golden Fixture

The golden uses the existing hand-made board from the trainer tests:

- preferred root move: `d1`
- other root move: `b1`
- child side: White
- selected feature: `edge_8[2131]`
- quantized TSV value: `5`

Python trainer margin for the one-feature pair is positive. The loaded C++
runtime evaluator gives:

- preferred child eval: `0`
- other child eval: `5`
- root margin: `-0 - -5 = 5`

So the generated-table runtime score difference agrees with the trainer pair
margin and ranks the preferred root move above the other move.

## Verification

Commands:

```sh
python3 -m unittest tools.scripts.tests.test_regularized_pairwise_pattern_train
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Results:

- Trainer unittest: 56 tests passed.
- CTest: 352 tests passed.
- `pytest` was not available in this local Python environment (`pytest not found`).

## Outcome

No trainer correctness bug was found in the audited paths. Regression coverage
now guards sign, feature delta, phase-specific synthetic overfit, output
quantization, generated `candidate.eval` table loading, and runtime child eval
score reflection.
