# 2026-05-31 Edge/Frontier/Stability Interaction Factorial

Status: historical experiment report, not current guidance.

This report is evidence only. It is not a strength claim, not permanent tuning
guidance, and not a default-promotion recommendation.

## Metadata

- base git SHA: `47b48ecf5e6c604a343ee7ebdb8f68d89231476c`
- baseline config: `data/eval/current_default.eval`
- baseline config sha256: `42d66384d725c55f46261624d78e4723846d63df1d2c1b41200429c4f4fa99cd`
- raw output location: `runs/eval-iteration-20260531-edge-frontier-interaction/`
- raw outputs committed: no
- default promotion: no
- C++ `EvaluationPreset` added: no

## Hypothesis

The previous `edge_frontier_balance_stability` signal may have come from an
interaction among:

- softer `edge_8_pattern` weights
- slightly higher mid/late frontier caution
- a small `edge_stability_lite` increase

The prior stability-lite narrowing rejected stability-only candidates, so this
iteration isolates the other terms and then retests the interaction on a larger
held-out label set.

## Why This Follows Stability-Lite Narrowing

`2026-05-31-stability-lite-narrowing.md` found that increasing
`edge_stability_lite` alone did not improve held-out objective, move-rank
metrics, or a 48-game smoke match. That means the earlier signal was unlikely
to be explained by stability alone. This batch therefore separates:

- `edge_8_pattern` softening alone
- frontier +1 alone
- `edge_8_pattern` plus frontier
- combinations that add stability back

## Candidate Table

| candidate | A: edge8 soft | B: frontier +1 | C: stability +1 | decision |
| --- | --- | --- | --- | --- |
| `edge8_soft_only` | yes | no | no | reject |
| `frontier_mid_late_plus1` | no | yes | no | keep as local ablation evidence; not committed |
| `edge8_soft_frontier` | yes | yes | no | keep as local ablation evidence; not committed |
| `edge8_soft_stability` | yes | no | yes | reject |
| `frontier_stability` | no | yes | yes | reject for this branch |
| `edge8_soft_frontier_stability` | yes | yes | yes | carry forward as an experimental hypothesis config |

## Candidate Config Diffs

All candidates start from `data/eval/current_default.eval`. Unlisted keys match
the baseline. Candidate config files were local artifacts under `runs/`.

### edge8_soft_only

| key | base | candidate |
| --- | ---: | ---: |
| `opening.edge_8_pattern` | 2 | 1 |
| `midgame.edge_8_pattern` | 4 | 3 |
| `late.edge_8_pattern` | 6 | 5 |

### frontier_mid_late_plus1

| key | base | candidate |
| --- | ---: | ---: |
| `midgame.frontier` | 6 | 7 |
| `late.frontier` | 3 | 4 |

### edge8_soft_frontier

| key | base | candidate |
| --- | ---: | ---: |
| `opening.edge_8_pattern` | 2 | 1 |
| `midgame.frontier` | 6 | 7 |
| `midgame.edge_8_pattern` | 4 | 3 |
| `late.frontier` | 3 | 4 |
| `late.edge_8_pattern` | 6 | 5 |

### edge8_soft_stability

| key | base | candidate |
| --- | ---: | ---: |
| `opening.edge_stability_lite` | 2 | 3 |
| `opening.edge_8_pattern` | 2 | 1 |
| `midgame.edge_stability_lite` | 4 | 5 |
| `midgame.edge_8_pattern` | 4 | 3 |
| `late.edge_stability_lite` | 8 | 9 |
| `late.edge_8_pattern` | 6 | 5 |

### frontier_stability

| key | base | candidate |
| --- | ---: | ---: |
| `opening.edge_stability_lite` | 2 | 3 |
| `midgame.frontier` | 6 | 7 |
| `midgame.edge_stability_lite` | 4 | 5 |
| `late.frontier` | 3 | 4 |
| `late.edge_stability_lite` | 8 | 9 |

### edge8_soft_frontier_stability

| key | base | candidate |
| --- | ---: | ---: |
| `opening.edge_stability_lite` | 2 | 3 |
| `opening.edge_8_pattern` | 2 | 1 |
| `midgame.frontier` | 6 | 7 |
| `midgame.edge_stability_lite` | 4 | 5 |
| `midgame.edge_8_pattern` | 4 | 3 |
| `late.frontier` | 3 | 4 |
| `late.edge_stability_lite` | 8 | 9 |
| `late.edge_8_pattern` | 6 | 5 |

## Label Generation Setup

| split | seed | requested count | labels | target empties | max empties | move scores |
| --- | ---: | ---: | ---: | --- | ---: | --- |
| train | 2026053131 | 48 | 48 | `8,10,12` | 12 | included |
| held-out | 2026053132 | 48 | 48 | `8,10,12` | 12 | included |

Commands:

```sh
./build/othello_position_sampler --output runs/eval-iteration-20260531-edge-frontier-interaction/train/positions.txt --count 48 --target-empties 8,10,12 --seed 2026053131
./build/othello_exact_label_dump --input runs/eval-iteration-20260531-edge-frontier-interaction/train/positions.txt --output runs/eval-iteration-20260531-edge-frontier-interaction/train/exact_labels.jsonl --max-empties 12 --include-move-scores

./build/othello_position_sampler --output runs/eval-iteration-20260531-edge-frontier-interaction/heldout/positions.txt --count 48 --target-empties 8,10,12 --seed 2026053132
./build/othello_exact_label_dump --input runs/eval-iteration-20260531-edge-frontier-interaction/heldout/positions.txt --output runs/eval-iteration-20260531-edge-frontier-interaction/heldout/exact_labels.jsonl --max-empties 12 --include-move-scores
```

## Correctness Verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Result: 257/257 tests passed.

## Eval-vs-Exact Summary

Training set, 48 records:

| config | sign agreements | wrong direction | high confidence wrong | top exact-best | exact-best rank sum | eval gap sum | exact gap sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| base | 36 | 10 | 3 | 27 | 89 | 1301 | 224 |
| `edge8_soft_only` | 36 | 10 | 3 | 26 | 90 | 1188 | 234 |
| `frontier_mid_late_plus1` | 36 | 10 | 3 | 27 | 89 | 1254 | 224 |
| `edge8_soft_frontier` | 36 | 10 | 2 | 26 | 90 | 1141 | 234 |
| `edge8_soft_stability` | 36 | 10 | 3 | 26 | 90 | 1232 | 234 |
| `frontier_stability` | 36 | 10 | 3 | 27 | 89 | 1298 | 224 |
| `edge8_soft_frontier_stability` | 36 | 10 | 2 | 26 | 90 | 1185 | 234 |

Held-out set, 48 records:

| config | objective | delta vs base | sign agreements | wrong direction | high confidence wrong |
| --- | ---: | ---: | ---: | ---: | ---: |
| base | 23 | 0 | 39 | 8 | 0 |
| `edge8_soft_only` | 20 | -3 | 38 | 9 | 0 |
| `frontier_mid_late_plus1` | 26 | +3 | 40 | 7 | 0 |
| `edge8_soft_frontier` | 26 | +3 | 40 | 7 | 0 |
| `edge8_soft_stability` | 20 | -3 | 38 | 9 | 0 |
| `frontier_stability` | 26 | +3 | 40 | 7 | 0 |
| `edge8_soft_frontier_stability` | 26 | +3 | 40 | 7 | 0 |

## Move-Rank Summary

Held-out set, 48 records with `move_scores`:

| config | top exact-best | exact-best rank sum | eval score gap sum | exact score gap sum |
| --- | ---: | ---: | ---: | ---: |
| base | 20 | 102 | 1812 | 330 |
| `edge8_soft_only` | 20 | 103 | 1684 | 330 |
| `frontier_mid_late_plus1` | 22 | 97 | 1791 | 324 |
| `edge8_soft_frontier` | 23 | 95 | 1672 | 290 |
| `edge8_soft_stability` | 20 | 102 | 1730 | 330 |
| `frontier_stability` | 21 | 98 | 1839 | 326 |
| `edge8_soft_frontier_stability` | 23 | 95 | 1715 | 290 |

`edge8_soft_frontier` and `edge8_soft_frontier_stability` were tied for the
best top exact-best count and exact-best rank sum. `edge8_soft_frontier` had the
lower evaluator score gap, so the exact-label diagnostics did not prove that
A+B+C is better than A+B. Stability did not improve held-out labels without
frontier.

## Search Validation Summary

All search validation used `--exact-endgame-threshold 0`. Result and work
checksums changed for every candidate in both profiles; those are behavior
changes, not automatic regressions.

Curated `--positions evaluation`, depths 5,6:

| config | result checksum vs base | work checksum vs base | nodes | elapsed ms |
| --- | --- | --- | ---: | ---: |
| base | reference | reference | 119486 | 25.105 |
| `edge8_soft_only` | changed | changed | 119761 | 24.823 |
| `frontier_mid_late_plus1` | changed | changed | 119329 | 24.713 |
| `edge8_soft_frontier` | changed | changed | 127986 | 34.615 |
| `edge8_soft_stability` | changed | changed | 121727 | 25.185 |
| `frontier_stability` | changed | changed | 119711 | 24.865 |
| `edge8_soft_frontier_stability` | changed | changed | 128523 | 26.529 |

Broader `--positions suite`, depths 4,5:

| config | result checksum vs base | work checksum vs base | nodes | elapsed ms |
| --- | --- | --- | ---: | ---: |
| base | reference | reference | 103000 | 19.831 |
| `edge8_soft_only` | changed | changed | 101075 | 19.934 |
| `frontier_mid_late_plus1` | changed | changed | 103063 | 19.735 |
| `edge8_soft_frontier` | changed | changed | 100369 | 19.296 |
| `edge8_soft_stability` | changed | changed | 100742 | 19.383 |
| `frontier_stability` | changed | changed | 103816 | 28.551 |
| `edge8_soft_frontier_stability` | changed | changed | 100614 | 19.332 |

The A+B and A+B+C candidates increased nodes on the curated evaluation profile
but reduced nodes on the broader suite profile. Elapsed time was noisy in at
least one run: the first `edge8_soft_frontier` evaluation elapsed result was
34.615 ms, while a Round 2 rerun with the same command reported 26.560 ms for
the same 127986 nodes. Treat the search evidence as profile-dependent smoke,
not as a stable runtime win or loss.

## Match Smoke Summary

Depth 5, exact disabled, 48 games, `eval_regression_openings.txt`, candidate as
player A vs base as player B with side swaps:

| candidate | A wins | B wins | draws | average disc diff from A | average time ms A/B |
| --- | ---: | ---: | ---: | ---: | --- |
| `frontier_mid_late_plus1` | 27 | 21 | 0 | +4.08 | 27.47 / 26.89 |
| `edge8_soft_frontier` | 28 | 20 | 0 | +5.38 | 27.31 / 26.62 |
| `edge8_soft_frontier_stability` | 33 | 15 | 0 | +11.04 | 27.39 / 26.45 |

This is smoke evidence only, not Elo, and it is not enough for default
promotion or named preset promotion.

## Round 2

Because `edge8_soft_frontier_stability` was promising as a carry-forward
hypothesis, Round 2 kept A+B fixed and varied only stability intensity:

| candidate | stability change | held-out objective | top exact-best | rank sum | eval gap | exact gap |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `edge8_soft_frontier` | none | 26 | 23 | 95 | 1672 | 290 |
| `edge8_soft_frontier_stability` | +1 all phases | 26 | 23 | 95 | 1715 | 290 |
| `edge8_soft_frontier_stability_late_only` | late +1 only | 26 | 23 | 95 | 1715 | 290 |
| `edge8_soft_frontier_stability_plus2` | +2 all phases | 26 | 22 | 96 | 1759 | 292 |

Round 2 search, `--positions evaluation`, depths 5,6:

| config | nodes | elapsed ms |
| --- | ---: | ---: |
| base | 119486 | 25.109 |
| `edge8_soft_frontier` | 127986 | 26.560 |
| `edge8_soft_frontier_stability` | 128523 | 29.606 |
| `edge8_soft_frontier_stability_late_only` | 128296 | 28.593 |
| `edge8_soft_frontier_stability_plus2` | 127546 | 27.003 |

Round 2 search, `--positions suite`, depths 4,5:

| config | nodes | elapsed ms |
| --- | ---: | ---: |
| base | 103000 | 19.824 |
| `edge8_soft_frontier` | 100369 | 19.316 |
| `edge8_soft_frontier_stability` | 100614 | 19.363 |
| `edge8_soft_frontier_stability_late_only` | 100512 | 19.324 |
| `edge8_soft_frontier_stability_plus2` | 100723 | 21.644 |

Round 2 match smoke:

| candidate | A wins | B wins | draws | average disc diff from A |
| --- | ---: | ---: | ---: | ---: |
| `edge8_soft_frontier_stability_late_only` | 29 | 19 | 0 | +7.88 |

The +2 stability variant worsened move-rank metrics and suite elapsed time, so
it was not matched. Late-only stability was positive but did not match the
full +1 all-phase match-smoke result. This still does not resolve A+B+C versus
A+B; that direct ablation needs broader labels, repeated search profiles, and
larger paired matches.

## Commands

Train matrix:

```sh
python3 tools/scripts/eval_candidate_matrix.py --build-dir build --labels runs/eval-iteration-20260531-edge-frontier-interaction/train/exact_labels.jsonl --baseline-config data/eval/current_default.eval --candidates runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_only.eval runs/eval-iteration-20260531-edge-frontier-interaction/configs/frontier_mid_late_plus1.eval runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_frontier.eval runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_stability.eval runs/eval-iteration-20260531-edge-frontier-interaction/configs/frontier_stability.eval runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_frontier_stability.eval --out runs/eval-iteration-20260531-edge-frontier-interaction/candidate-matrix-train --search-depths 5,6,7 --positions smoke --repetitions 1 --exact-endgame-threshold 0 --move-rank-analysis
```

Held-out validation:

```sh
python3 tools/scripts/eval_config_validate.py --validation-labels runs/eval-iteration-20260531-edge-frontier-interaction/heldout/exact_labels.jsonl --base-config data/eval/current_default.eval --candidate-configs runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_only.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/frontier_mid_late_plus1.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_frontier.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_stability.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/frontier_stability.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_frontier_stability.eval --out runs/eval-iteration-20260531-edge-frontier-interaction/heldout-validation --build-dir build --top 7 --move-rank-analysis
```

Search validation:

```sh
python3 tools/scripts/eval_config_search_validate.py --base-config data/eval/current_default.eval --candidate-configs runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_only.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/frontier_mid_late_plus1.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_frontier.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_stability.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/frontier_stability.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_frontier_stability.eval --heldout-summary runs/eval-iteration-20260531-edge-frontier-interaction/heldout-validation/summary.tsv --build-dir build --out runs/eval-iteration-20260531-edge-frontier-interaction/search-validation-evaluation --top 6 --run-search-bench --positions evaluation --depths 5,6 --exact-endgame-threshold 0

python3 tools/scripts/eval_config_search_validate.py --base-config data/eval/current_default.eval --candidate-configs runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_only.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/frontier_mid_late_plus1.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_frontier.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_stability.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/frontier_stability.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_frontier_stability.eval --heldout-summary runs/eval-iteration-20260531-edge-frontier-interaction/heldout-validation/summary.tsv --build-dir build --out runs/eval-iteration-20260531-edge-frontier-interaction/search-validation-suite --top 6 --run-search-bench --positions suite --depths 4,5 --exact-endgame-threshold 0
```

Match smoke:

```sh
python3 tools/scripts/eval_config_search_validate.py --base-config data/eval/current_default.eval --candidate-configs runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_only.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/frontier_mid_late_plus1.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_frontier.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_stability.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/frontier_stability.eval,runs/eval-iteration-20260531-edge-frontier-interaction/configs/edge8_soft_frontier_stability.eval --heldout-summary runs/eval-iteration-20260531-edge-frontier-interaction/heldout-validation/summary.tsv --build-dir build --out runs/eval-iteration-20260531-edge-frontier-interaction/match-smoke-48 --top 3 --run-match-smoke --positions evaluation --depths 5 --games 48 --openings data/openings/eval_regression_openings.txt --exact-endgame-threshold 0
```

Round 2 commands used the same held-out labels and search profiles with local
configs under `runs/eval-iteration-20260531-edge-frontier-interaction/round2-configs/`.

## Positive Evidence

- `edge8_soft_frontier_stability` improved held-out objective by +3.
- It matched the best held-out move-rank top exact-best count at 23/48 and rank
  sum 95.
- It had the best 48-game match-smoke evidence among tested candidates, 33-15
  with average disc diff +11.04.
- Its suite search nodes and elapsed were slightly lower than the base in the
  measured smoke profile.
- Round 2 showed that increasing stability to +2 was not better, while the full
  +1 all-phase variant retained the strongest match-smoke result among the
  matched variants.

## Negative Evidence

- `edge8_soft_only` and `edge8_soft_stability` worsened held-out objective.
- `edge8_soft_frontier_stability` did not clearly beat `edge8_soft_frontier` on
  held-out objective or move-rank; `edge8_soft_frontier` matched the top
  exact-best count and rank sum and had a lower evaluator score gap.
- A+B+C must not be treated as proven better than A+B from this experiment.
  Its differentiating evidence is match smoke, which is limited.
- A+B and A+B+C increased nodes on the curated evaluation search profile.
- Search validation showed profile-dependent node and elapsed tradeoffs.
- Exact labels are small and label-distribution dependent.
- Match smoke used only 48 games per selected candidate and is not Elo.

## Decision Table

| candidate | decision | reason |
| --- | --- | --- |
| `edge8_soft_only` | reject | Held-out objective -3 and no top exact-best gain. |
| `frontier_mid_late_plus1` | keep as local ablation evidence | Held-out +3 and 27-21 match smoke, but weaker move-rank than the A+B variants. |
| `edge8_soft_frontier` | keep as local ablation evidence | Tied best held-out move-rank tier and 28-20 match smoke; direct comparison against A+B+C remains unresolved. |
| `edge8_soft_stability` | reject | Held-out objective -3 and no move-rank gain. |
| `frontier_stability` | reject for this branch | Held-out +3, but move-rank was weaker than A+B variants and no match smoke was justified after stronger candidates emerged. |
| `edge8_soft_frontier_stability` | keep as experimental config | Strongest 48-game match-smoke evidence and tied best move-rank tier, but held-out/move-rank does not clearly beat `edge8_soft_frontier`. Carry forward only as an experimental hypothesis for direct A+B+C vs A+B validation. |
| `edge8_soft_frontier_stability_late_only` | reject as carry-forward | Positive 29-19 match, but weaker than full +1 all-phase stability. |
| `edge8_soft_frontier_stability_plus2` | reject | Worse move-rank and suite elapsed than the +1 variant. |

## Caveats

- No default promotion.
- No C++ `EvaluationPreset` added.
- The committed config is experimental, not default, not promoted, and caveated.
- The match-smoke advantage is not enough for default promotion or named preset
  promotion.
- The committed config is a carry-forward hypothesis only because evidence is
  mixed and sample size is limited.
- Raw outputs remain under `runs/` and are not committed.
- Generated exact labels are small and not representative training data.
- Match smoke is not Elo.
- This report is evidence, not permanent tuning guidance.
- Engine code, rule behavior, search algorithm semantics, and exact solver
  semantics were not changed.

## Next Recommended Action

Use `data/eval/experimental_edge8_soft_frontier_stability.eval` as the next
starting point only. The mandatory discriminating follow-up is a direct A+B+C
versus A+B ablation:

- reproduce `edge8_soft_frontier` from this report as the A+B comparison
  candidate
- compare it directly against
  `data/eval/experimental_edge8_soft_frontier_stability.eval`
- generate at least 192 held-out labels from new seeds with `move_scores`, if
  practical
- use repeated and/or deeper search profiles on the same machine, build type,
  command options, and position set with `--exact-endgame-threshold 0`
- run at least 192 swapped games on `eval_regression_openings.txt`, if practical

Reject the experimental config if it fails to retain positive held-out
objective, loses move-rank to `edge8_soft_frontier`, or shows repeated
search-node/elapsed regression above roughly 10% without a compensating match
advantage. The roughly 10% search-regression check must be based on repeated
profiles with the same machine, build type, command options, and position set;
do not judge it from a single noisy elapsed run.
