# Pattern Symmetry Diagnostic

## Purpose

Diagnose whether PatternOnly / `tools/scripts/regularized_pairwise_pattern_train.py`
is leaving Othello board symmetries unused. This is diagnostic-only evidence:
no trainer behavior changed, no retained preset was added, and
`data/eval/current_default.eval` was not modified.

## Files Reviewed

- `tools/scripts/pattern_specs.py`
- `src/evaluation_patterns.cpp`
- `tools/scripts/regularized_pairwise_pattern_train.py`

The Python trainer and C++ runtime both enumerate the same pattern instances in
C++ square-index order (`a1..h1`, then ranks upward) and use the same ternary
side-relative index convention:

- empty: `0`
- side-to-score / own disc: `1`
- opponent disc: `2`

The trainer computes pair deltas from child boards using the child side, then
negates the child-position sign by subtracting preferred-child counts from
other-child counts. This matches root move preference scoring.

## Added Diagnostic Coverage

- D4 board transforms in Python:
  `identity`, `flip_horizontal`, `flip_vertical`, `rotate_90`,
  `rotate_180`, `rotate_270`, `transpose_main_diagonal`,
  `transpose_anti_diagonal`.
- Move coordinates are mapped through the same square-index transforms.
- Color inversion plus side inversion preserves side-relative pattern indexes.
- Synthetic board diagnostics transform:
  `board`, `teacher_move`, `root_scores`, and `exact_best_moves`.
- The transformed synthetic legal move sets, preferred child boards, compared
  child boards, and preference features all remain valid.

Verification command:

```sh
python3 -m unittest tools/scripts/tests/test_pattern_specs.py \
  tools/scripts/tests/test_pattern_symmetry_diagnostics.py \
  tools/scripts/tests/test_regularized_pairwise_pattern_train.py
```

Result: `Ran 68 tests ... OK`.

## Pattern Family Sharing

Counts are over `instances * 8 D4 transforms`.

| family | exact-order mappings | reversed-index mappings | missing in same family |
|---|---:|---:|---:|
| `corner_2x3` | 16 | 0 | 16 |
| `corner_3x3` | 16 | 0 | 16 |
| `edge_8` | 16 | 16 | 0 |
| `edge_x_10` | 16 | 0 | 16 |
| `row_8` | 16 | 16 | 32 |
| `column_8` | 16 | 16 | 32 |
| `diagonal_4` | 16 | 16 | 0 |
| `diagonal_5` | 16 | 16 | 0 |
| `diagonal_6` | 16 | 16 | 0 |
| `diagonal_7` | 16 | 16 | 0 |
| `diagonal_8` | 8 | 8 | 0 |
| `inner_row_8` | 16 | 16 | 0 |
| `corner_2x4` | 16 | 0 | 16 |

Interpretation:

- `exact-order` means transformed geometry reuses the same family table entry
  for the same ternary index.
- `reversed-index` means the transformed geometry is represented, but the
  ternary cell order reverses. The runtime will use a different table entry
  unless the learned table is explicitly symmetric.
- `missing in same family` means a transformed geometry is not represented by
  that family. It may still be represented by another family.

## Cross-Family Findings

| relation | same table? | exact-order mappings | reversed-index mappings | missing |
|---|---:|---:|---:|---:|
| `row_8 -> column_8` | no | 16 | 16 | 32 |
| `column_8 -> row_8` | no | 16 | 16 | 32 |
| `edge_8 -> edge_8` | yes | 16 | 16 | 0 |
| `edge_x_10 -> edge_x_10` | yes | 16 | 0 | 16 |
| `inner_row_8 -> inner_row_8` | yes | 16 | 16 | 0 |

`row_8` and `column_8` are rotational equivalents, but they are separate
families and therefore do not share learned parameters. This is the clearest
unused spatial symmetry in the broad vocabulary.

Corner families canonicalize horizontal/vertical corner mirrors, but not
90-degree rotations or diagonal reflections into the same family because their
cell order is row-major local corner shape rather than both row-major and
column-major local shape.

`edge_x_10` has same-family coverage for rotations that preserve the full-edge
plus X-square order, but horizontal reversal swaps both edge order and appended
X-square order; those transformed specs are not represented as simple reversed
ternary indexes.

## Learned TSV Symmetry Check

Command shape:

```sh
python3 tools/scripts/pattern_symmetry_diagnostics.py \
  --pattern-table data/eval/patterns/ntest_pairwise_full_v2_opening.tsv
```

The source-controlled `ntest_pairwise_full_v2` tables contain
`corner_3x3`, `edge_8`, `edge_x_10`, `diagonal_8`, and `inner_row_8`.
They do not contain `row_8`, `column_8`, `diagonal_4..7`, or `corner_2x4`.

| phase TSV | check | checked pairs | violations | max abs delta |
|---|---|---:|---:|---:|
| opening | index vs reversed index | 49,572 | 1,120 | 16 |
| opening | index vs color-inverted index | 49,210 | 1,068 | 16 |
| opening | diagonal_8 reversal | 3,321 | 113 | 7 |
| midgame | index vs reversed index | 49,572 | 5,592 | 14 |
| midgame | index vs color-inverted index | 49,210 | 5,147 | 16 |
| midgame | diagonal_8 reversal | 3,321 | 316 | 6 |
| late | index vs reversed index | 49,572 | 10,218 | 13 |
| late | index vs color-inverted index | 49,210 | 9,455 | 16 |
| late | diagonal_8 reversal | 3,321 | 525 | 7 |

The color-inverted table check is a diagnostic of learned value antisymmetry
(`w[color_inv(index)] = -w[index]`),
not a runtime correctness failure. Runtime index construction is side-relative;
the unit tests confirm color inversion plus side inversion preserves indexes.
Learned tables can still assign asymmetric weights to color-inverted states
because the trainer currently does not tie or augment those entries.

## Optional Post-Training Symmetrization Tool

Follow-up implementation added an optional TSV rewrite mode to the diagnostic
script. This remains an experiment tool, not default training behavior:

- `regularized_pairwise_pattern_train.py` still emits unsymmetrized tables by
  default.
- `data/eval/current_default.eval` is unchanged.
- No retained preset was added.
- Source-controlled learned TSVs are not rewritten in place.

Example smoke command:

```sh
python3 tools/scripts/pattern_symmetry_diagnostics.py \
  --pattern-table data/eval/patterns/ntest_pairwise_full_v2_opening.tsv \
  --symmetrize-output runs/symmetry/ntest_pairwise_full_v2_opening.sym.tsv \
  --symmetrize reversed-index \
  --json \
  --out runs/symmetry/opening.symmetry.json
```

Supported modes:

- `reversed-index`: average same-family reversed ternary indexes for line-like
  families currently treated as safe (`edge_8`, `inner_row_8`,
  `diagonal_4..8`).
- `diagonal-reversal`: same as the diagonal portion of `reversed-index`; useful
  for narrower diagonal-only experiments.
- `color-inversion`: enforce side-relative antisymmetry with
  `w[color_inv(index)] = -w[index]`.

The implementation builds signed same-family orbits and averages each orbit in
one pass, so combining modes does not depend on the order in the command line.
Color-inversion self-orbits are forced to zero.

`row_8 <-> column_8` cross-family averaging is intentionally not included yet:
it ties parameters across families and is broad-v2-specific. D4 data
augmentation is also left for a later PR.

Local smoke on the opening `ntest_pairwise_full_v2` table:

| mode | processed families | entries read | entries written | changed entries | before -> after |
|---|---|---:|---:|---:|---|
| `reversed-index` | `edge_8`, `diagonal_8`, `inner_row_8` | 3,232 | 2,281 | 632 | global reversed `1120 -> 668`; `edge_8`, `diagonal_8`, `inner_row_8` reversal all `-> 0` |
| `reversed-index,color-inversion,diagonal-reversal` | `corner_3x3`, `edge_8`, `edge_x_10`, `diagonal_8`, `inner_row_8` | 3,232 | 1,014 | 1,583 | color inversion `1068 -> 0`; target reversals `-> 0`; global reversed `1120 -> 424` |

The remaining global reversed-index violations after symmetrization are
expected for families where simple reversed ternary order is not the D4
equivalent geometry (`corner_3x3`, `edge_x_10`).

## Conclusion

Pattern index correctness is consistent between Python and C++ and
side-relative color handling is correct.

PatternOnly is not fully exploiting Othello symmetry at the parameter level:

- reversed line/edge/diagonal states generally use different learned entries;
- `row_8` and `column_8` rotational equivalents are separate families;
- corner and `edge_x_10` families omit some 90-degree/diagonal transformed
  local orderings;
- learned TSVs show many nonzero violations between symmetry-equivalent
  reversed entries.

This supports a follow-up PR that compares `--symmetry-augment` with
post-training table symmetrization. The next PR can now swap a symmetrized TSV
into a local `candidate.eval` and compare eval-vs-exact, search smoke, and a
small deterministic match without changing default training artifacts.
