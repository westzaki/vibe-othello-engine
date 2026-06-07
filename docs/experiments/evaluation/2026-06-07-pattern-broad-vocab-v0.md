# Pattern Broad Vocabulary v0

## Hypothesis

PatternOnly full v0 may have failed because the runtime pattern vocabulary was too thin: it improved some rank/top-group metrics, but selected move quality, exact sign, and match strength collapsed. Before falling back to scalar anchors, this experiment widened the pattern vocabulary and tested whether a broader PatternOnly candidate can recover top-1 selection and practical search behavior.

This is not a default-promotion experiment. `current_default.eval`, `default_evaluation_config()`, and exact endgame semantics were not changed.

## Implementation Summary

Added runtime/trainer support for these pattern families:

- `row_8`: all 8 ranks.
- `column_8`: all 8 files.
- `diagonal_4`, `diagonal_5`, `diagonal_6`, `diagonal_7`: exact-length diagonals and anti-diagonals.
- `corner_2x4`: 2x4 corner/edge context for all four corners.

Existing families remain compatible: `corner_2x3`, `corner_3x3`, `edge_8`, `edge_x_10`, `diagonal_8`, and `inner_row_8`.

The Python trainer now exposes `--families broad_v2`, defined as:

```text
corner_3x3, edge_8, edge_x_10, diagonal_8, inner_row_8,
row_8, column_8, diagonal_4, diagonal_5, diagonal_6,
diagonal_7, corner_2x4
```

Old pattern TSVs remain loadable: families absent from a TSV stay zero in the dense runtime bundle.

## Table Size

| family | instances | cells | entries | dense int16 bytes |
| --- | ---: | ---: | ---: | ---: |
| corner_2x3 | 4 | 6 | 729 | 1,458 |
| corner_3x3 | 4 | 9 | 19,683 | 39,366 |
| edge_8 | 4 | 8 | 6,561 | 13,122 |
| edge_x_10 | 4 | 10 | 59,049 | 118,098 |
| diagonal_8 | 2 | 8 | 6,561 | 13,122 |
| inner_row_8 | 4 | 8 | 6,561 | 13,122 |
| row_8 | 8 | 8 | 6,561 | 13,122 |
| column_8 | 8 | 8 | 6,561 | 13,122 |
| diagonal_4 | 4 | 4 | 81 | 162 |
| diagonal_5 | 4 | 5 | 243 | 486 |
| diagonal_6 | 4 | 6 | 729 | 1,458 |
| diagonal_7 | 4 | 7 | 2,187 | 4,374 |
| corner_2x4 | 4 | 8 | 6,561 | 13,122 |

`broad_v2` uses 121,338 dense int16 entries per phase table. The newly added families account for 22,923 entries, or 45,846 raw int16 bytes. The full `PatternTableBundle`, including legacy families, is about 244 KiB raw int16 data, or about 732 KiB for three phase-specific bundles.

`edge_adjacent_16` and `center_4x4` were intentionally not added: each dense 3^16 family would require 43,046,721 int16 entries, about 82.1 MiB per family per phase.

## Commands

10K candidate:

```bash
VIBE_OTHELLO_DATASET_ROOT=<dataset-root> \
python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --teacher-labels runs/pattern-training/ntest-balanced300k-v0/local-inputs/labels-10000.jsonl \
  --exact-labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config data/eval/current_default.eval \
  --analyze-position build/release/othello_analyze_position \
  --out-dir runs/pattern-training/ntest-balanced300k-v0/pattern_broad_vocab_v0_10k \
  --analysis-cache-dir runs/pattern-training/ntest-balanced300k-v0/analysis-cache \
  --analysis-cache-mode read-write \
  --analysis-runner batch \
  --analysis-jobs 8 \
  --families broad_v2 \
  --split train \
  --pair-mode best-vs-all \
  --pair-weighting score-margin \
  --max-pairs-per-position 8 \
  --loss logistic \
  --l2 1e-3 \
  --epochs 5 \
  --learning-rate 0.05 \
  --no-base-margin \
  --candidate-eval-shape pattern-only \
  --seed 20260608
```

50K candidate used the same command with:

```text
--teacher-labels runs/pattern-training/ntest-balanced300k-v0/local-inputs/labels-50000.jsonl
--out-dir runs/pattern-training/ntest-balanced300k-v0/pattern_broad_vocab_v0_50k
```

Exact-overlap:

```bash
build/release/othello_eval_vs_exact \
  --labels <dataset-root>/teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --eval-config runs/pattern-training/ntest-balanced300k-v0/pattern_broad_vocab_v0_50k/candidate.eval \
  --top 10000 \
  --move-rank-analysis \
  --output runs/eval/pattern_broad_vocab_v0_50k_exact.md
```

Search smoke:

```bash
build/release/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions evaluation \
  --repetitions 1 \
  --tt on \
  --pvs on \
  --exact-endgame-threshold 0 \
  --eval-config runs/pattern-training/ntest-balanced300k-v0/pattern_broad_vocab_v0_50k/candidate.eval \
  --format jsonl
```

Match smoke:

```bash
build/release/othello_match_runner \
  --black search:depth=6,eval_config=runs/pattern-training/ntest-balanced300k-v0/pattern_broad_vocab_v0_50k/candidate.eval \
  --white search:depth=6,eval_config=data/eval/current_default.eval \
  --games 200 \
  --swap-sides true \
  --seed 20260608 \
  --openings data/openings/eval_regression_openings.txt \
  --output runs/matches/pattern_broad_vocab_v0_50k-vs-current-default-depth6.jsonl \
  --format jsonl \
  --quiet
```

## Training Results

| candidate | teacher rows | train rows | cache hits | cache misses | cache writes | analysis seconds | preference pairs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| pattern_broad_vocab_v0_10k | 10,000 | 7,036 | 0 | 7,036 | 7,036 | 35.239 | 50,733 |
| pattern_broad_vocab_v0_50k | 50,000 | 34,867 | 7,036 | 27,831 | 27,831 | 140.122 | 251,281 |

The 50K run reused the 10K run cache entries after the rebuilt analyzer established a new hash.

## Teacher Diagnostics

Move-choice diagnostics use the same first 9,995 usable teacher rows for each config.

| config | selected agreement | avg teacher rank | rank sum | rank1 | rank2+ | rank3+ | avg top group | top tie rate | teacher top group | exact-best top group | avg exact-best rank |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| current_default | 45.33% | 2.389 | 23,877 | 4,578 | 6,709 | 7,955 | 1.017 | 1.70% | 45.80% | 55.38% | 1.858 |
| pattern_only_full_v0 | 30.51% | 1.951 | 19,500 | 6,189 | 8,115 | 8,803 | 3.025 | 56.61% | 61.92% | 65.23% | 1.582 |
| pattern_broad_vocab_v0_10k | 27.30% | 2.377 | 23,757 | 4,999 | 7,059 | 8,082 | 2.510 | 53.41% | 50.02% | 64.62% | 1.603 |
| pattern_broad_vocab_v0_50k | 26.98% | 2.526 | 25,246 | 4,703 | 6,743 | 7,731 | 2.274 | 50.53% | 47.05% | 56.62% | 1.892 |

The broader vocabulary reduces top-group size versus PatternOnly full v0, but selected teacher agreement is worse and rank quality does not recover.

## Exact-Overlap

| config | sign agreement | wrong direction | high-confidence wrong | exact-best top group | exact-best rank sum | avg exact-best rank |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| current_default | 80.05% | 1,650 | 220 | 55.82% | 18,434 | 1.843 |
| pattern_only_full_v0 | 51.38% | 2,400 | 0 | 60.17% | 20,119 | 2.012 |
| pattern_broad_vocab_v0_10k | 43.58% | 1,994 | 0 | 56.91% | 19,941 | 1.994 |
| pattern_broad_vocab_v0_50k | 51.40% | 2,269 | 0 | 52.34% | 20,992 | 2.099 |

The 50K candidate recovers sign agreement only to PatternOnly full v0 level, while exact-best top group and rank are worse.

## Search Smoke

Current default:

| depth | best move | nodes | elapsed ms | eval calls | checksum |
| --- | --- | ---: | ---: | ---: | --- |
| 5 | b2 | 16,933 | 30.032 | 15,484 | 3407339372377443460 |
| 6 | d6 | 46,720 | 45.299 | 42,810 | 6917595495085306980 |
| 7 | b2 | 150,234 | 155.763 | 138,775 | 12106001709669006217 |

`pattern_broad_vocab_v0_50k`:

| depth | best move | nodes | elapsed ms | eval calls | checksum |
| --- | --- | ---: | ---: | ---: | --- |
| 5 | f6 | 13,207 | 17.279 | 12,259 | 18040833704711744350 |
| 6 | b2 | 35,056 | 22.489 | 32,933 | 11099234354022866654 |
| 7 | d6 | 114,104 | 77.085 | 106,927 | 7683126379340188895 |

The candidate loads and searches successfully. It is faster in this smoke set, but it changes best moves/checksums and the match evidence below is strongly negative.

## Match Smoke

Depth 6, 200 games, swap sides, seed `20260608`.

| candidate vs current_default | wins | losses | draws | errors | avg disc diff | avg candidate nodes | avg current nodes | avg candidate ms | avg current ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| pattern_only_full_v0 | 2 | 198 | 0 | 0 | -44.08 | n/a | n/a | n/a | n/a |
| pattern_broad_vocab_v0_10k | 2 | 198 | 0 | 0 | -44.37 | 213,699 | 428,509 | 122.601 | 241.267 |
| pattern_broad_vocab_v0_50k | 0 | 200 | 0 | 0 | -47.61 | 225,655 | 411,143 | 62.736 | 114.862 |

Broad vocabulary alone did not improve the practical PatternOnly failure. The 50K run is worse than PatternOnly full v0 in this deterministic match smoke.

## Positive Evidence

- Runtime evaluator, TSV loader, and Python trainer now agree on the wider pattern index order.
- Existing eval files still load because missing families remain zero.
- `broad_v2` candidates can be generated end-to-end with batch analysis and phase-specific tables.
- Search smoke passes for generated candidates.
- 50K cache reuse worked after the 10K warmup: 7,036 hits, 27,831 misses, 27,831 writes.

## Negative Evidence

- Selected teacher agreement remains much worse than `current_default` and worse than PatternOnly full v0.
- Tie/top-group behavior is still too broad: 50K top tie rate is 50.53% versus 1.70% for `current_default`.
- Exact sign agreement is not materially improved over PatternOnly full v0, and exact-best rank/top-group are worse.
- Deterministic match is catastrophic: 0 wins / 200 losses for the 50K candidate.
- Larger 50K training did not improve the 10K candidate; it reduced top-group size but also reduced rank and match quality.

## Decision

Reject `pattern_broad_vocab_v0_10k` and `pattern_broad_vocab_v0_50k` as strength candidates. Do not add a retained `data/eval` preset.

Keep the vocabulary/tooling changes because they make wider pattern experiments possible and remain compatible with current eval artifacts.

## Next Action

The failure now points less to vocabulary size alone and more to objective/output-shape issues:

- Add tie/sign-aware training: exact-aware preferences or sign-safe loss for exact-overlap rows.
- Try calibrated output scaling and stronger anti-tie regularization before full 300K.
- Revisit phase granularity only after the candidate can beat PatternOnly full v0 in selected agreement and match smoke.
- Consider PatternFirst small anchors after proving the pattern table can choose a sane top move distribution.
