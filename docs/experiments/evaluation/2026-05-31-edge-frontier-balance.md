# 2026-05-31 Edge/Frontier Balance Evaluator Iteration

Status: experimental report, not current guidance.

This note preserves one evaluator-improvement iteration as historical evidence
for future agents. It is not a default-promotion recommendation, not a named
config promotion, and not a strength claim.

## Metadata

- base ref / git SHA: `959ce64c6b5f0bd04358d06d906d4d26f001af6c`
- baseline config: `data/eval/current_default.eval`
- raw output location: `runs/eval-iteration-20260531-edge-frontier/`
- raw outputs committed: no

## Hypothesis

The current default may overweight full-edge shape (`edge_8_pattern`) in small
endgame-like exact labels. This iteration tested softer `edge_8_pattern` weights
while shifting emphasis toward mobility/frontier pressure or corner-anchored
edge stability.

## Candidates

| candidate | short description |
| --- | --- |
| `edge_frontier_balance_soft` | Lower `edge_8_pattern` to 1/3/5 across phases while preserving the current mobility/frontier balance. |
| `edge_frontier_balance_mobility` | Lower `edge_8_pattern` to 1/3/5 and increase opening/midgame/late mobility plus midgame frontier. |
| `edge_frontier_balance_stability` | Lower `edge_8_pattern` to 1/3/5 and increase `edge_stability_lite` plus midgame/late frontier caution. |

The candidate `.eval` files were local experiment artifacts under `runs/` and
were not committed.

## Generated Label Setup

| split | seed | count | target empties | move scores |
| --- | ---: | ---: | --- | --- |
| train | 20260531 | 24 | `8,10,12` | included |
| held-out | 20260601 | 24 | `8,10,12` | included |

Command templates used:

```sh
./build/othello_position_sampler --output runs/eval-iteration-20260531-edge-frontier/train/positions.txt --count 24 --target-empties 8,10,12 --seed 20260531
./build/othello_exact_label_dump --input runs/eval-iteration-20260531-edge-frontier/train/positions.txt --output runs/eval-iteration-20260531-edge-frontier/train/exact_labels.jsonl --max-empties 12 --include-move-scores

./build/othello_position_sampler --output runs/eval-iteration-20260531-edge-frontier/heldout/positions.txt --count 24 --target-empties 8,10,12 --seed 20260601
./build/othello_exact_label_dump --input runs/eval-iteration-20260531-edge-frontier/heldout/positions.txt --output runs/eval-iteration-20260531-edge-frontier/heldout/exact_labels.jsonl --max-empties 12 --include-move-scores
```

## Correctness Verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Result: 257/257 CTest tests passed.

## Evaluation Commands

Train candidate matrix:

```sh
python3 tools/scripts/eval_candidate_matrix.py --build-dir build --labels runs/eval-iteration-20260531-edge-frontier/train/exact_labels.jsonl --baseline-config data/eval/current_default.eval --candidates runs/eval-iteration-20260531-edge-frontier/configs/edge_frontier_balance_soft.eval runs/eval-iteration-20260531-edge-frontier/configs/edge_frontier_balance_mobility.eval runs/eval-iteration-20260531-edge-frontier/configs/edge_frontier_balance_stability.eval --out runs/eval-iteration-20260531-edge-frontier/candidate-matrix-train --search-depths 5,6,7 --positions smoke --repetitions 1 --exact-endgame-threshold 0 --move-rank-analysis
```

Held-out validation:

```sh
python3 tools/scripts/eval_config_validate.py --validation-labels runs/eval-iteration-20260531-edge-frontier/heldout/exact_labels.jsonl --base-config data/eval/current_default.eval --candidate-configs runs/eval-iteration-20260531-edge-frontier/configs/edge_frontier_balance_soft.eval,runs/eval-iteration-20260531-edge-frontier/configs/edge_frontier_balance_mobility.eval,runs/eval-iteration-20260531-edge-frontier/configs/edge_frontier_balance_stability.eval --out runs/eval-iteration-20260531-edge-frontier/heldout-validation --build-dir build --top 4 --move-rank-analysis
```

Curated evaluation search and match smoke:

```sh
python3 tools/scripts/eval_config_search_validate.py --base-config data/eval/current_default.eval --candidate-configs runs/eval-iteration-20260531-edge-frontier/configs/edge_frontier_balance_soft.eval,runs/eval-iteration-20260531-edge-frontier/configs/edge_frontier_balance_mobility.eval,runs/eval-iteration-20260531-edge-frontier/configs/edge_frontier_balance_stability.eval --heldout-summary runs/eval-iteration-20260531-edge-frontier/heldout-validation/summary.tsv --build-dir build --out runs/eval-iteration-20260531-edge-frontier/search-validation-evaluation --top 3 --run-search-bench --run-match-smoke --positions evaluation --depths 5,6 --games 12 --openings data/openings/eval_regression_openings.txt --exact-endgame-threshold 0
```

Broader suite search smoke:

```sh
python3 tools/scripts/eval_config_search_validate.py --base-config data/eval/current_default.eval --candidate-configs runs/eval-iteration-20260531-edge-frontier/configs/edge_frontier_balance_soft.eval,runs/eval-iteration-20260531-edge-frontier/configs/edge_frontier_balance_mobility.eval,runs/eval-iteration-20260531-edge-frontier/configs/edge_frontier_balance_stability.eval --heldout-summary runs/eval-iteration-20260531-edge-frontier/heldout-validation/summary.tsv --build-dir build --out runs/eval-iteration-20260531-edge-frontier/search-validation-suite --top 3 --run-search-bench --positions suite --depths 4,5 --exact-endgame-threshold 0
```

## Eval-vs-Exact Summary

Training set, 24 records:

| config | sign agreements | wrong direction | high confidence wrong |
| --- | ---: | ---: | ---: |
| base | 18 | 6 | 2 |
| soft | 19 | 5 | 1 |
| mobility | 19 | 5 | 1 |
| stability | 19 | 5 | 1 |

Held-out set, 24 records:

| config | objective | delta vs base | sign agreements | wrong direction | high confidence wrong |
| --- | ---: | ---: | ---: | ---: | ---: |
| base | 12 | 0 | 20 | 4 | 0 |
| soft | 12 | 0 | 20 | 4 | 0 |
| mobility | 12 | 0 | 20 | 4 | 0 |
| stability | 15 | 3 | 21 | 3 | 0 |

## Move-Rank Summary

Move-rank analysis was available because the generated labels included
`move_scores`.

| config | held-out top exact-best | exact-best rank sum | eval score gap sum | exact score gap sum |
| --- | ---: | ---: | ---: | ---: |
| base | 14 | 59 | 796 | 68 |
| soft | 14 | 58 | 748 | 68 |
| mobility | 14 | 56 | 751 | 68 |
| stability | 14 | 57 | 769 | 68 |

No candidate improved the held-out top exact-best count. The worst cases still
included confident non-best edge moves, so root move quality remains unresolved.

## Search Validation Summary

Search validation used `--exact-endgame-threshold 0`.

Curated `--positions evaluation`, depths 5,6:

| config | nodes | elapsed ms | checksum vs base |
| --- | ---: | ---: | --- |
| base | 119486 | 26.375 | reference |
| soft | 119761 | 25.315 | changed |
| mobility | 124815 | 26.932 | changed |
| stability | 127901 | 34.760 | changed |

Broader `--positions suite`, depths 4,5:

| config | nodes | elapsed ms | checksum vs base |
| --- | ---: | ---: | --- |
| base | 103000 | 20.713 | reference |
| soft | 101075 | 20.228 | changed |
| mobility | 101870 | 20.965 | changed |
| stability | 100923 | 28.859 | changed |

Checksum changes are behavior changes, not automatic regressions. The elapsed
times are smoke-scale evidence and may include noise.

## Match Smoke Summary

Depth 5, exact disabled, 12 games, `eval_regression_openings.txt`, candidate as
player A vs base as player B with side swaps:

| candidate | A wins | B wins | draws | average disc diff from A |
| --- | ---: | ---: | ---: | ---: |
| soft | 5 | 7 | 0 | -1.17 |
| mobility | 6 | 6 | 0 | -2.83 |
| stability | 8 | 4 | 0 | 9.17 |

This is directional smoke evidence only, not Elo.

## Evidence

Positive evidence:

- All candidates improved training sign agreement by one record and reduced
  high-confidence wrong-direction cases from 2 to 1.
- `edge_frontier_balance_stability` improved held-out objective by +3 and
  reduced held-out wrong-direction count from 4 to 3.
- `edge_frontier_balance_stability` had the best match smoke result at 8-4.
- `edge_frontier_balance_mobility` had the best held-out exact-best rank sum.

Negative evidence:

- No candidate improved held-out top exact-best count.
- `edge_frontier_balance_soft` did not improve held-out objective and lost the
  match smoke 5-7.
- `edge_frontier_balance_mobility` did not improve held-out objective and had
  negative average disc diff in match smoke.
- `edge_frontier_balance_stability` increased elapsed time in both search
  validation profiles.
- The exact labels were small and not representative training data.

## Decisions

| candidate | decision |
| --- | --- |
| `edge_frontier_balance_soft` | reject for this loop |
| `edge_frontier_balance_mobility` | needs more data |
| `edge_frontier_balance_stability` | keep experimental |

There is no default promotion and no named config promotion from this iteration.

## Caveats

- This report is historical experiment evidence, not permanent tuning guidance.
- Smoke evidence only; no strength claim.
- Raw outputs remain under `runs/` and are not committed.
- Generated exact labels are not committed.
- Exact labels are small and not representative training data.
- The default evaluator was not promoted.
- No new C++ `EvaluationPreset` was added.
- Engine behavior, evaluator semantics, and exact solver semantics were not
  changed.

## Next Recommended Action

Continue from `edge_frontier_balance_stability`, but narrow the
`edge_stability_lite` increase to reduce runtime/search impact. Validate on a
larger held-out exact-label set, run at least a 48-game swapped match, and keep
exact endgame semantics unchanged.
