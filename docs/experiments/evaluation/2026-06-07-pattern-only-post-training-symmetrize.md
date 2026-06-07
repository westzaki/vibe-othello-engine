# PatternOnly Post-Training Symmetrize

This report records a PatternOnly trainer-foundation follow-up to the PR324
soft exact-score target plus selected sign anchor objective. It is not a
strength claim, not an Elo estimate, and not a default-promotion
recommendation.

`data/eval/current_default.eval` was not changed. No retained preset or
committed eval table was added. Generated candidates and measurement artifacts
remain under `runs/`.

## Hypothesis

PR323/PR324 improved the scalar-free PatternOnly objective, but deterministic
match results against `current_default` were still strongly negative. Because
PatternOnly runtime uses only learned pattern tables, failing to reuse
same-family table symmetries may waste data and make exact-rank/sign gates less
stable.

This experiment starts with the safer post-training same-family TSV
symmetrization path rather than D4 data augmentation or row/column cross-family
sharing.

Runtime candidate constraints stayed unchanged:

- `--objective exact-aware-listwise`
- `--exact-score-soft-target`
- `--soft-sign-anchor-mode selected`
- `--soft-sign-anchor-weight 0.1`
- `--soft-sign-anchor-margin 1`
- `--candidate-eval-shape pattern-only`
- `--no-base-margin`
- no `current_default` distillation
- no base-plus-delta candidate
- no retained preset

## Implementation

`tools/scripts/regularized_pairwise_pattern_train.py` gained:

```sh
--post-training-symmetrize reversed-index,color-inversion,diagonal-reversal
```

The default is empty/off. Training behavior, pair cache keys, listwise feature
cache keys, and checkpoint learning state are unchanged. When modes are
provided, the trainer applies same-family TSV symmetrization only after writing
generated phase tables under `--out-dir/tables/`.

The implementation reuses `pattern_symmetry_diagnostics.py` helpers:

- `parse_symmetrize_modes`
- `symmetrize_pattern_table`
- the existing signed-orbit `symmetrize_weights` path for order-independent
  combined modes

The trainer rejects post-processing outputs outside `--out-dir` and rejects
source-controlled `data/` paths. If symmetrization removes every non-zero entry
from a generated phase table, the trainer writes the existing sentinel zero
entry so the generated candidate remains loadable.

`summary.json` and `report.md` record:

- modes
- phase-by-phase before/after violation counts
- phase-by-phase max absolute deltas
- changed entries
- entries read/written
- zero entries introduced/removed
- whether a sentinel entry was written

## 50K Runs

All runs used the PR324 dataset, seed, families, eval config, and analysis cache.
The PR324 no-symmetrize run was reused as baseline A.

Command shape:

```sh
python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --dataset-root /path/to/vibe-othello-datasets \
  --teacher-labels dataset:teacher/ntest-balanced300k-v0/labels/ntest12-local/shards/labels-0000.jsonl,...,dataset:teacher/ntest-balanced300k-v0/labels/ntest12-local/shards/labels-0049.jsonl \
  --exact-labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config data/eval/current_default.eval \
  --analyze-position build/release/othello_analyze_position \
  --analysis-cache-dir runs/pattern-training/ntest-balanced300k-v0/analysis-cache \
  --analysis-cache-mode read-write \
  --analysis-runner batch \
  --analysis-jobs 8 \
  --families all \
  --split train \
  --objective exact-aware-listwise \
  --exact-score-soft-target \
  --exact-score-temperature 4 \
  --exact-score-target-floor 0.0001 \
  --exact-score-near-best-window 8 \
  --soft-sign-anchor-mode selected \
  --soft-sign-anchor-weight 0.1 \
  --soft-sign-anchor-margin 1 \
  --pair-mode best-vs-all \
  --pair-weighting score-margin \
  --max-pairs-per-position 8 \
  --loss logistic \
  --l2 1e-3 \
  --epochs 5 \
  --learning-rate 0.05 \
  --no-base-margin \
  --candidate-eval-shape pattern-only \
  --seed 20260607
```

| run | modes | output |
| --- | --- | --- |
| A | off | `runs/pattern-training/ntest-balanced300k-v0/sign_anchor_50k_selected_w01_m1/candidate.eval` |
| B | `reversed-index` | `runs/pattern-training/ntest-balanced300k-v0/post_sym_50k_reversed_index/candidate.eval` |
| C | `reversed-index,color-inversion,diagonal-reversal` | `runs/pattern-training/ntest-balanced300k-v0/post_sym_50k_all_same_family/candidate.eval` |

Analysis cache behavior for B/C:

| run | hits | misses | writes |
| --- | ---: | ---: | ---: |
| B | 34867 | 0 | 0 |
| C | 34867 | 0 | 0 |

Trainer-local quantized diagnostics are unchanged by post-training TSV
symmetrization because they are computed before TSV post-processing.

| metric | PR324 baseline | B reversed-index | C all same-family |
| --- | ---: | ---: | ---: |
| soft target cross entropy | 1.683 | 1.683 | 1.683 |
| avg exact-best rank | 1.525 | 1.525 | 1.525 |
| exact-best top-group rate | 93.46% | 93.46% | 93.46% |
| exact sign agreement rate | 77.87% | 77.87% | 77.87% |
| wrong direction | 195 | 195 | 195 |
| high-confidence wrong direction | 0 | 0 | 0 |
| selected exact-score regret | 6.340 | 6.340 | 6.340 |

## Symmetry Violations

### B: Reversed Index

| phase | changed entries | entries read | entries written | zero introduced | zero removed |
| --- | ---: | ---: | ---: | ---: | ---: |
| opening | 0 | 7825 | 5370 | 0 | 0 |
| midgame | 24 | 59381 | 44185 | 24 | 0 |
| late | 9 | 70419 | 53873 | 9 | 0 |

Before/after violation counts:

| phase | before | after |
| --- | --- | --- |
| opening | `index_vs_reversed_index=0` | `index_vs_reversed_index=0` |
| midgame | `index_vs_reversed_index=59`, diagonal/edge/inner checks total `24` | `index_vs_reversed_index=35`, diagonal/edge/inner checks total `0` |
| late | `index_vs_reversed_index=33`, diagonal/edge checks total `9` | `index_vs_reversed_index=24`, diagonal/edge checks total `0` |

The remaining `index_vs_reversed_index` violations are from families not covered
by same-family reversed-index mode, for example row/column cross-family cases.
That sharing is intentionally out of scope for this experiment.

### C: Reversed Index + Color Inversion + Diagonal Reversal

| phase | changed entries | entries read | entries written | zero introduced | zero removed | sentinel |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| opening | 4 | 7825 | 1 | 5 | 0 | yes |
| midgame | 88 | 59381 | 2 | 88 | 0 | no |
| late | 46 | 70419 | 2 | 46 | 0 | no |

Before/after violation counts:

| phase | before | after |
| --- | --- | --- |
| opening | `index_vs_color_inverted_index=4`, `index_vs_reversed_index=0` | `index_vs_color_inverted_index=0`, `index_vs_reversed_index=0` |
| midgame | `index_vs_color_inverted_index=85`, `index_vs_reversed_index=59`, diagonal/edge/inner checks total `24` | `index_vs_color_inverted_index=0`, `index_vs_reversed_index=0`, diagonal checks `0` |
| late | `index_vs_color_inverted_index=45`, `index_vs_reversed_index=33`, diagonal/edge checks total `9` | `index_vs_color_inverted_index=0`, `index_vs_reversed_index=2` |

Negative evidence: color inversion was too aggressive for this sparse learned
table. It collapsed almost all non-zero entries, leaving opening with only the
sentinel and midgame/late with two non-zero entries each.

## External Exact Overlap

Command shape:

```sh
build/release/othello_eval_vs_exact \
  --labels /path/to/vibe-othello-datasets/teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config RUN/candidate.eval \
  --top 10000 \
  --move-rank-analysis \
  --phase-breakdown \
  --output runs/eval/REPORT.md
```

| metric | PR324 baseline | B reversed-index | C all same-family |
| --- | ---: | ---: | ---: |
| analyzed | 10000 | 10000 | 10000 |
| exact sign agreement | 4725 | 4526 | 564 |
| exact sign agreement rate | 47.25% | 45.26% | 5.64% |
| wrong direction | 1741 | 1356 | 68 |
| high-confidence wrong direction | 0 | 0 | 0 |
| exact-best top group | 6709 | 7474 | 9935 |
| exact-best top-group rate | 67.09% | 74.74% | 99.35% |
| exact-best rank sum | 18836 | 16640 | 10198 |
| avg exact-best rank | 1.884 | 1.664 | 1.020 |
| mean exact score gap exact-best minus top group | 3.927 | 3.095 | 0.052 |

B satisfies the narrow external exact-overlap condition: wrong-direction
improved by 385 and avg exact-best rank/top-group improved relative to PR324.
However, exact sign agreement regressed by 199 rows.

C's rank/top-group numbers are misleading. The evaluator produced zero scores
for 9693 / 10000 positions, so the top group was huge and often contained an
exact-best move by tie. Its low wrong-direction count is also mostly caused by
near-zero output rather than meaningful sign direction. This is negative
evidence against applying color inversion as a blind post-training projection to
the current sparse PatternOnly table.

## Search Bench

Command shape:

```sh
build/release/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions evaluation \
  --repetitions 1 \
  --tt on \
  --pvs on \
  --exact-endgame-threshold 0 \
  --eval-config RUN/candidate.eval \
  --format jsonl
```

| run | depth | nodes | elapsed ms | eval calls | best move | score | checksum |
| --- | ---: | ---: | ---: | ---: | --- | ---: | --- |
| PR324 | 5 | 12495 | 14.012 | 11564 | d6 | n/a | 16363230256699643497 |
| PR324 | 6 | 32163 | 17.752 | 30175 | b2 | n/a | 1873949051240075749 |
| PR324 | 7 | 96340 | 35.269 | 90092 | d6 | n/a | 16215182189361450227 |
| B | 5 | 13741 | 15.509 | 12760 | d6 | 6 | 8057479803673684831 |
| B | 6 | 33072 | 19.790 | 31002 | b2 | -5 | 1874512001574120677 |
| B | 7 | 107702 | 41.634 | 100877 | d6 | 6 | 16263046261869162995 |
| C | 5 | 8975 | 12.301 | 8448 | b2 | 0 | 7977553579292120168 |
| C | 6 | 19960 | 15.082 | 18945 | b2 | 0 | 17437870475008221989 |
| C | 7 | 67085 | 22.364 | 63179 | b2 | 0 | 6846015445925060319 |

All candidates load and search after the sentinel fix. C's repeated score `0`
is further evidence that all-mode symmetrization largely erased the evaluator.

## Deterministic Match

Command shape:

```sh
build/release/othello_match_runner \
  --black search:depth=6,eval_config=RUN/candidate.eval \
  --white search:depth=6,eval_config=data/eval/current_default.eval \
  --games 40 \
  --swap-sides true \
  --seed 20260607 \
  --openings data/openings/eval_regression_openings.txt \
  --output runs/matches/OUT.jsonl \
  --format jsonl \
  --quiet
```

| metric | PR324 baseline | B reversed-index | C all same-family |
| --- | ---: | ---: | ---: |
| games | 40 | 40 | 40 |
| PatternOnly wins | 0 | 0 | 0 |
| PatternOnly losses | 40 | 39 | 40 |
| draws | 0 | 1 | 0 |
| errors | 0 | 0 | 0 |
| avg disc diff from PatternOnly | -44.60 | -47.50 | -49.85 |
| avg nodes PatternOnly | 193673.52 | 179558.12 | 87294.18 |
| avg nodes current_default | 397194.75 | 347280.25 | 290933.15 |
| avg ms PatternOnly | 55.94 | 49.83 | 23.11 |
| avg ms current_default | 88.30 | 79.57 | 67.64 |

Both post-training symmetrized candidates remain strongly negative in match.
B found one draw but worsened average disc difference. C lost every game by an
even larger average margin.

## Decision

B (`reversed-index`) is the only useful candidate from this pass. It improved
external exact-overlap avg exact-best rank, exact-best top group, and
wrong-direction relative to PR324, but it regressed exact sign agreement and did
not improve deterministic match.

C (`reversed-index,color-inversion,diagonal-reversal`) is not useful as tested.
Its impressive exact-rank/top-group metrics come from table collapse and broad
ties, not from a better evaluator.

No candidate is promotion-ready. Because both match runs still lose heavily to
`current_default`, promotion is not allowed from this evidence.

## Validation

- `python3 -m py_compile tools/scripts/regularized_pairwise_pattern_train.py tools/scripts/pattern_symmetry_diagnostics.py`
- `python3 -m unittest tools/scripts/tests/test_regularized_pairwise_pattern_train.py tools/scripts/tests/test_pattern_symmetry_diagnostics.py`
- `python3 -m unittest discover tools/scripts/tests`
- 50K PatternOnly B/C runs completed
- `eval_vs_exact --top 10000 --move-rank-analysis --phase-breakdown`
- `search_bench` depths 5,6,7
- deterministic 40-game match vs `current_default`
