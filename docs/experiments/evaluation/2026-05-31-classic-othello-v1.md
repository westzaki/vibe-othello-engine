# 2026-05-31 Classic Othello v1 Evaluator Baseline

Status: experimental report, not current guidance.

このレポートは `classic_othello_v1` という `.eval` 設定候補の実験記録である。
デフォルト昇格の提案ではなく、強さ証明でもない。現行ドキュメントと最新の
`main` が古い実験メモより優先される。

## Metadata

- base ref / git SHA: `47b48ecf5e6c604a343ee7ebdb8f68d89231476c`
- PR head SHA at report creation: `32643939948a52871b8549fe5527acb8683c2c44`
- baseline config: `data/eval/current_default.eval`
- baseline config sha256: `42d66384d725c55f46261624d78e4723846d63df1d2c1b41200429c4f4fa99cd`
- candidate config path: `data/eval/classic_othello_v1.eval`
- candidate config sha256: `f43a1de59fc298d4576b75cf807c7cb040cd8ac16349ad01196bda4348af973c`
- measurement config sha256 before PR-review header metadata update:
  `dbc9ed61a2f3910746994b6c14ad6438042a2ef6ea4b1d548a712f6ac60d9487`
- raw output location: `runs/eval-classic-othello-v1/`
- raw outputs committed: no
- NTest teacher evidence: not available locally; `runs/local-engines/engines.txt` was not present

## Hypothesis

`current_default.eval` already contains corner, frontier, corner-pattern,
edge-stability, and edge-pattern features. The next useful baseline should not be
another one-feature nudge. `classic_othello_v1` tests a coherent classic Othello
control idea:

- opening: avoid disc greed, prefer mobility/potential mobility, corner access,
  X-square safety, and local corner shape;
- midgame: emphasize mobility, frontier control, and corner structure while
  keeping edge-shape rewards conservative;
- late pre-endgame: reintroduce disc difference modestly while preserving
  mobility, corner, and stability relevance;
- exact endgame remains separate and is disabled in search validation when the
  heuristic evaluator is being measured.

## Evaluator Design Rationale

Phase cutoffs stay at `opening_max_occupied=20` and
`midgame_max_occupied=44`. These are the current default cutoffs, and this
experiment is intended to test a weight-set hypothesis rather than a phase-policy
change.

| Feature | Opening | Midgame | Late | Rationale |
| --- | ---: | ---: | ---: | --- |
| `disc_difference` | 0 | 1 | 6 | Early disc count is deliberately ignored. Midgame keeps the current tiny tie-breaker. Late pre-endgame raises disc count only modestly because exact solving remains separate. |
| `mobility` | 12 | 13 | 7 | Mobility is the main classic control signal. It is strongest before the late phase, then remains relevant without overwhelming corners/stability. |
| `potential_mobility` | 8 | 6 | 3 | Potential mobility is emphasized in the opening to value future options and reduce premature locking. It tapers as exact endgame approaches. |
| `corner_occupancy` | 36 | 48 | 55 | Owned corners remain high-value throughout. The value rises with phase because corner ownership increasingly anchors stable structure. |
| `corner_access` | 45 | 38 | 24 | Immediate legal corner access is especially important in opening/midgame tactics. It is reduced late because actual ownership and exact search become more meaningful than access alone. |
| `x_square_danger` | 42 | 36 | 24 | Empty-corner X-square danger is strongly penalized early, when the mistake can hand over corners. It tapers later as corners become occupied or tactically forced. |
| `frontier` | 7 | 9 | 5 | Fewer own frontier discs is a core classic principle. Midgame gets the highest weight because frontier exposure most directly affects mobility and tempo there. |
| `corner_local_2x3` | 4 | 5 | 3 | This lightly reinforces C/X-square and owned-corner support structure. It stays below `corner_2x3_pattern` because it overlaps with the richer pattern feature and explicit X-square danger. |
| `corner_2x3_pattern` | 8 | 9 | 6 | Corner-neighborhood pattern structure is a primary shape term in this candidate. It is high in opening/midgame and softened late to avoid double-counting with corner ownership/stability. |
| `edge_stability_lite` | 1 | 4 | 9 | Corner-anchored edge stability is weak in the opening, useful in the midgame, and more meaningful late. The late value is only slightly above current default. |
| `edge_8_pattern` | 1 | 3 | 4 | Full-edge shape is intentionally conservative to avoid greedy edge overweighting. It remains present as edge-shape evidence but is lower than current default in every phase. |

## Exact Commands

Configure, build, and CTest:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Generate train exact labels with root move scores:

```sh
./build/othello_position_sampler --output runs/eval-classic-othello-v1/train/positions.txt --count 48 --target-empties 8,10,12 --seed 2026053101
./build/othello_exact_label_dump --input runs/eval-classic-othello-v1/train/positions.txt --output runs/eval-classic-othello-v1/train/exact_labels.jsonl --max-empties 12 --include-move-scores
```

Generate held-out exact labels with root move scores:

```sh
./build/othello_position_sampler --output runs/eval-classic-othello-v1/heldout/positions.txt --count 48 --target-empties 8,10,12 --seed 2026053102
./build/othello_exact_label_dump --input runs/eval-classic-othello-v1/heldout/positions.txt --output runs/eval-classic-othello-v1/heldout/exact_labels.jsonl --max-empties 12 --include-move-scores
```

Eval-vs-exact and move-rank matrix:

```sh
python3 tools/scripts/eval_candidate_matrix.py --build-dir build --labels runs/eval-classic-othello-v1/train/exact_labels.jsonl --baseline-config data/eval/current_default.eval --candidates data/eval/classic_othello_v1.eval --out runs/eval-classic-othello-v1/candidate-matrix-train --search-depths 5,6,7 --positions evaluation --repetitions 1 --exact-endgame-threshold 0 --move-rank-analysis
python3 tools/scripts/eval_candidate_matrix.py --build-dir build --labels runs/eval-classic-othello-v1/heldout/exact_labels.jsonl --baseline-config data/eval/current_default.eval --candidates data/eval/classic_othello_v1.eval --out runs/eval-classic-othello-v1/candidate-matrix-heldout --search-depths 5,6,7 --positions evaluation --repetitions 1 --exact-endgame-threshold 0 --move-rank-analysis
python3 tools/scripts/eval_config_validate.py --validation-labels runs/eval-classic-othello-v1/heldout/exact_labels.jsonl --base-config data/eval/current_default.eval --candidate-configs data/eval/classic_othello_v1.eval --out runs/eval-classic-othello-v1/heldout-validation --build-dir build --top 2 --move-rank-analysis
```

Search validation, exact root disabled, iterative search with TT/PVS/aspiration:

```sh
python3 tools/scripts/eval_candidate_matrix.py --build-dir build --baseline-config data/eval/current_default.eval --candidates data/eval/classic_othello_v1.eval --out runs/eval-classic-othello-v1/search-validation-evaluation-r3 --search-depths 5,6,7 --positions evaluation --repetitions 3 --exact-endgame-threshold 0
```

Small swapped match smoke, exact disabled:

```sh
./build/othello_match_runner --black search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/classic_othello_v1.eval --white search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/current_default.eval --games 24 --swap-sides true --seed 20260531 --openings data/openings/eval_regression_openings.txt --output runs/eval-classic-othello-v1/match-smoke/classic-vs-current-depth5.jsonl --quiet
python3 tools/scripts/match_summary.py --input runs/eval-classic-othello-v1/match-smoke/classic-vs-current-depth5.jsonl --by-opening
```

NTest availability check:

```sh
test -f runs/local-engines/engines.txt
```

The NTest check returned non-zero, so no local NTest teacher evidence was
collected.

## CTest Result

Result: 257/257 CTest tests passed.

## Eval-vs-Exact Summary

Both splits used 48 sampled exact labels with `move_scores`.

| Split | Config | Sign Agreements | Wrong Direction | High Confidence Wrong |
| --- | --- | ---: | ---: | ---: |
| train | current_default | 29 | 17 | 0 |
| train | classic_othello_v1 | 31 | 15 | 0 |
| held-out | current_default | 37 | 10 | 1 |
| held-out | classic_othello_v1 | 37 | 10 | 1 |

Held-out validation objective was unchanged:

| Config | Objective | Delta vs Base | Analyzed |
| --- | ---: | ---: | ---: |
| current_default | 16 | 0 | 48 |
| classic_othello_v1 | 16 | 0 | 48 |

## Move-Rank Summary

| Split | Config | Top Exact-Best | Exact-Best Rank Sum | Eval Score Gap Sum | Exact Score Gap Sum |
| --- | --- | ---: | ---: | ---: | ---: |
| train | current_default | 24 | 91 | 1434 | 178 |
| train | classic_othello_v1 | 26 | 85 | 1443 | 174 |
| held-out | current_default | 27 | 95 | 1482 | 177 |
| held-out | classic_othello_v1 | 27 | 94 | 1643 | 165 |

The candidate improved the training top exact-best count and rank sum. On
held-out labels, top exact-best count stayed flat, exact-best rank sum improved
by one, and exact score gap improved, but evaluator score gap worsened.

## Search Validation Summary

Search validation used the committed `evaluation` position suite, depths 5,6,7,
three repetitions, iterative mode, TT on, PVS on, aspiration on, and
`--exact-endgame-threshold 0`.

| Config | Nodes | Node Delta vs Base | Elapsed ms | Elapsed Delta vs Base | Score Kind | Used Exact Endgame | Exact Root Searches |
| --- | ---: | ---: | ---: | ---: | --- | --- | ---: |
| current_default | 613647 | 0.0% | 173.361 | 0.0% | heuristic | false | 0 |
| classic_othello_v1 | 584589 | -4.7% | 182.829 | +5.5% | heuristic | false | 0 |

The candidate changed result and work checksums, which is expected evaluator
behavior evidence rather than a regression by itself. The sample aggregate best
move changed from `d6` to `b2`; the sample score changed from `69` to `129`.
Nodes fell, but elapsed time rose slightly in this smoke run.

## Match Smoke Summary

Depth 5, exact disabled, 24 games, `eval_regression_openings.txt`, side-swapped.
`classic_othello_v1` was player A and `current_default` was player B.

| A Wins | B Wins | Draws | Average Disc Diff from A | Error Games | Exact Roots A/B | Average Time ms A/B |
| ---: | ---: | ---: | ---: | ---: | --- | --- |
| 12 | 11 | 1 | 1.92 | 0 | 0 / 0 | 29.30 / 28.56 |

This is smoke evidence only. It is not Elo and not proof of strength.

## Positive Evidence

- CTest passed after adding the committed `.eval` config.
- Training exact-label sign agreement improved from 29 to 31 and wrong-direction
  count fell from 17 to 15.
- Training move-rank top exact-best count improved from 24 to 26, and rank sum
  improved from 91 to 85.
- Held-out exact score gap improved from 177 to 165, even though sign agreement
  was unchanged.
- Search validation kept exact root disabled and reported `score_kind=heuristic`
  with zero exact-root searches.
- Search validation showed fewer nodes for the candidate on the evaluation
  suite.
- Match smoke was slightly positive for the candidate: 12-11-1 with +1.92
  average disc diff from player A.

## Negative Evidence

- Held-out sign agreement, wrong-direction count, high-confidence wrong count,
  and validation objective did not improve.
- Held-out top exact-best count did not improve.
- Held-out evaluator score gap worsened from 1482 to 1643.
- Search elapsed time rose by 5.5% in the repeated evaluation-suite smoke run.
- The match sample is too small and opening-dependent for any strength claim.
- No local NTest teacher evidence was available.

## Decision

Decision: keep experimental.

`classic_othello_v1` is a coherent and reusable experimental baseline, not a
promotion candidate. The evidence is better than a purely local tweak because it
tests a full classic control philosophy and changes search behavior measurably.
However, held-out sign/objective results are flat, and the timing/move-rank
evidence is mixed. Keep it as an active `.eval` candidate for follow-up teacher
labeling and mistake mining; do not promote it to default.

## Caveats

- no default promotion
- no C++ `EvaluationPreset`
- no raw outputs committed
- no generated labels committed
- no exact solver semantic changes
- no search semantic changes
- smoke is not Elo or strength proof
- exact labels are small, sampled by random playouts, and not representative
  training data
- NTest teacher evidence is not yet available locally

## Next Recommended Action

Implement or document the NTest teacher label workflow as a follow-up PR so this
candidate can be compared against an external teacher, not only exact small-empty
labels. In parallel, mine the held-out move-rank misses from
`runs/eval-classic-othello-v1/heldout-validation/reports/classic_othello_v1.md`
and classify whether the failures are corner-access, X-square, frontier,
edge-shape, or late disc-count mistakes before changing weights again.
