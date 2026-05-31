# 2026-05-31 Teacher Mistake Mining v2

Status: experimental report, not current guidance.

This report records a scalar `.eval` experiment that used validated NTest teacher
labels plus exact labels with root move scores. It is not a default-promotion
proposal, not a C++ `EvaluationPreset`, not an Elo claim, and not proof of
strength.

## Metadata

- base git SHA: `df86050fec8fa8d72a2ed48a8ff50bc6757365fe`
- head git SHA at report creation: `6f933d0891507045e0100e3fb8d104d8aa6efffb`
- raw output location: `runs/teacher-mistake-mining-v2/`
- raw outputs committed: no
- generated teacher labels committed: no
- generated exact labels committed: no
- local NTest paths committed: no

Compared configs:

| Config | Path | sha256 |
| --- | --- | --- |
| current_default | `data/eval/current_default.eval` | `42d66384d725c55f46261624d78e4723846d63df1d2c1b41200429c4f4fa99cd` |
| classic_othello_v1 | `data/eval/classic_othello_v1.eval` | `f43a1de59fc298d4576b75cf807c7cb040cd8ac16349ad01196bda4348af973c` |
| classic_othello_v2_teacher_safe | `data/eval/classic_othello_v2_teacher_safe.eval` | `510e9fc66a425a3e16cef959d2405eadf091c4316799d7c13d318dbb8896891a` |

## Hypothesis

`classic_othello_v1` was a useful classic-principles baseline, but validated
teacher mining showed that its stronger mobility/frontier/corner-shape emphasis
caused four teacher regressions versus `current_default` and no offsetting
teacher-agreement wins. The v2 hypothesis is therefore conservative:

- preserve current-default teacher move choices where v1 regressed;
- keep a small amount of v1's corner-safety and shape signal;
- do not further raise frontier or mobility;
- keep edge pattern conservative;
- do not raise late disc difference.

## Candidate

`classic_othello_v2_teacher_safe` is based on `current_default` with small
teacher-safe pieces of `classic_othello_v1`.

| Feature | current opening/mid/late | v1 opening/mid/late | v2 opening/mid/late | Rationale |
| --- | --- | --- | --- | --- |
| `mobility` | 8 / 10 / 6 | 12 / 13 / 7 | 9 / 10 / 6 | v1 overrode teacher choices; v2 keeps only a small opening bump. |
| `potential_mobility` | 4 / 5 / 2 | 8 / 6 / 3 | 6 / 5 / 2 | Adds opening flexibility without changing late behavior. |
| `corner_occupancy` | 35 / 40 / 45 | 36 / 48 / 55 | 38 / 46 / 50 | Keeps corner ownership stronger than current, below v1 late. |
| `corner_access` | 30 / 35 / 20 | 45 / 38 / 24 | 36 / 35 / 22 | Avoids v1's late open-corner regression while preserving safety. |
| `x_square_danger` | 25 / 30 / 20 | 42 / 36 / 24 | 34 / 32 / 22 | Adds safety, but does not copy v1's aggressive penalty. |
| `frontier` | 5 / 6 / 3 | 7 / 9 / 5 | 5 / 6 / 3 | Teacher mining did not support higher frontier weight. |
| `corner_local_2x3` | 0 / 0 / 0 | 4 / 5 / 3 | 2 / 2 / 1 | Keeps a small local shape signal only. |
| `corner_2x3_pattern` | 4 / 6 / 4 | 8 / 9 / 6 | 6 / 7 / 5 | Mild shape increase, below v1. |
| `edge_stability_lite` | 2 / 4 / 8 | 1 / 4 / 9 | 2 / 4 / 9 | Mostly current behavior. |
| `edge_8_pattern` | 2 / 4 / 6 | 1 / 3 / 4 | 1 / 3 / 4 | Keeps v1's conservative edge shape. |

Phase cutoffs stay at `opening_max_occupied=20` and `midgame_max_occupied=44`.

## Teacher Labels

Teacher workflow commands used NTest depth 26, NBoard protocol, timeout 60000 ms,
and `build/othello_validate_move`. The actual local NTest binary and workdir
paths are intentionally not recorded in this committed report.

Diagnostic suite:

```sh
python3 tools/scripts/external_teacher_label_workflow.py \
  --positions data/positions/evaluation/diagnostic_suite.txt \
  --out runs/teacher-mistake-mining-v2/teacher/ntest-depth26-diagnostic \
  --adapter ntest \
  --protocol nboard \
  --depth 26 \
  --timeout-ms 60000 \
  --workdir "${NTEST_BUILD}" \
  --engine-name ntest26-local \
  --legal-validator build/othello_validate_move \
  --engine-cmd -- "${NTEST_BIN}" x
```

Random self-play sample:

```sh
./build/othello_position_sampler \
  --output runs/teacher-mistake-mining-v2/teacher/random100_positions.txt \
  --count 100 \
  --target-empties 12,20,30,40,50 \
  --seed 2026053103

python3 tools/scripts/external_teacher_label_workflow.py \
  --positions runs/teacher-mistake-mining-v2/teacher/random100_positions.txt \
  --out runs/teacher-mistake-mining-v2/teacher/ntest-depth26-random100 \
  --adapter ntest \
  --protocol nboard \
  --depth 26 \
  --timeout-ms 60000 \
  --workdir "${NTEST_BUILD}" \
  --engine-name ntest26-local \
  --legal-validator build/othello_validate_move \
  --allow-failures \
  --engine-cmd -- "${NTEST_BIN}" x
```

NTest depth 26 was stable, but the 100-position run used about 195 seconds of
engine time. A 1,000-position run at the same setting was estimated at more than
30 minutes, so it was not run in this PR.

| Source | Requested | OK | Failed | `legal_move_valid=true` | Illegal Teacher Moves | Validator Failures |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| diagnostic_suite | 8 | 7 | 1 | 7 | 0 | 0 |
| random100 | 100 | 100 | 0 | 100 | 0 | 0 |
| total | 108 | 107 | 1 | 107 | 0 | 0 |

The one diagnostic failure was an adapter-level invalid move token: NTest exited
successfully but produced no recognizable move for `eval-mobility-pressure`.
That row was excluded from mining. All usable teacher rows had C++ rule-core
legal validation.

## Exact Labels

Exact labels were generated with root move scores for train and held-out splits:

```sh
./build/othello_position_sampler \
  --output runs/teacher-mistake-mining-v2/exact/train_positions.txt \
  --count 48 \
  --target-empties 8,10,12 \
  --seed 2026053104

./build/othello_exact_label_dump \
  --input runs/teacher-mistake-mining-v2/exact/train_positions.txt \
  --output runs/teacher-mistake-mining-v2/exact/train_labels.jsonl \
  --max-empties 12 \
  --include-move-scores

./build/othello_position_sampler \
  --output runs/teacher-mistake-mining-v2/exact/heldout_positions.txt \
  --count 48 \
  --target-empties 8,10,12 \
  --seed 2026053105

./build/othello_exact_label_dump \
  --input runs/teacher-mistake-mining-v2/exact/heldout_positions.txt \
  --output runs/teacher-mistake-mining-v2/exact/heldout_labels.jsonl \
  --max-empties 12 \
  --include-move-scores
```

Additional exact overlap labels were generated for teacher/exact comparison:

```sh
./build/othello_exact_label_dump \
  --input runs/teacher-mistake-mining-v2/teacher/random100_positions.txt \
  --output runs/teacher-mistake-mining-v2/exact/teacher_random100_max12_labels.jsonl \
  --max-empties 12 \
  --include-move-scores

./build/othello_exact_label_dump \
  --input data/positions/evaluation/diagnostic_suite.txt \
  --output runs/teacher-mistake-mining-v2/exact/diagnostic_max12_labels.jsonl \
  --max-empties 12 \
  --include-move-scores
```

| Dataset | Input Positions | Labeled | Skipped Too Many Empties |
| --- | ---: | ---: | ---: |
| train | 48 | 48 | 0 |
| held-out | 48 | 48 | 0 |
| teacher random100 overlap | 100 | 21 | 79 |
| diagnostic overlap | 8 | 1 | 7 |

Teacher-vs-exact overlap was 22 positions. NTest's validated move was exact-best
in all 22 overlap positions.

## Mistake Mining

Mining command:

```sh
python3 tools/scripts/teacher_label_mistake_mining.py \
  --teacher-labels \
    runs/teacher-mistake-mining-v2/teacher/ntest-depth26-diagnostic/labels.jsonl \
    runs/teacher-mistake-mining-v2/teacher/ntest-depth26-random100/labels.jsonl \
  --exact-labels \
    runs/teacher-mistake-mining-v2/exact/diagnostic_max12_labels.jsonl \
    runs/teacher-mistake-mining-v2/exact/teacher_random100_max12_labels.jsonl \
  --config current=data/eval/current_default.eval \
  --config classic_v1=data/eval/classic_othello_v1.eval \
  --config v2_teacher_safe=data/eval/classic_othello_v2_teacher_safe.eval \
  --out runs/teacher-mistake-mining-v2/mining/v2-teacher-safe \
  --build-dir build \
  --depth 1 \
  --exact-endgame-threshold 0
```

The mining helper shells out to `othello_analyze_position`; it does not implement
Othello legal move generation in Python. Buckets are conservative diagnostics.

## Agreement Matrix

Teacher agreement used 107 validated teacher rows.

| Config | Teacher Agreement | Teacher Move Rank Sum | Exact Overlap Agreement | Exact Overlap Rank Sum |
| --- | ---: | ---: | ---: | ---: |
| current_default | 44 / 107 | 286 | 9 / 22 | 50 |
| classic_othello_v1 | 40 / 107 | 279 | 9 / 22 | 45 |
| classic_othello_v2_teacher_safe | 44 / 107 | 280 | 9 / 22 | 48 |
| NTest teacher | n/a | n/a | 22 / 22 | n/a |

Exact-label sign and move-rank matrix:

| Split | Config | Sign Agreements | Wrong Direction | High Confidence Wrong | Top Exact-Best | Exact-Best Rank Sum | Exact Score Gap Sum |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| train | current_default | 36 / 48 | 11 | 1 | 26 / 48 | 91 | 230 |
| train | classic_othello_v1 | 37 / 48 | 10 | 1 | 28 / 48 | 88 | 216 |
| train | classic_othello_v2_teacher_safe | 38 / 48 | 9 | 1 | 29 / 48 | 88 | 194 |
| held-out | current_default | 38 / 48 | 9 | 1 | 24 / 48 | 97 | 157 |
| held-out | classic_othello_v1 | 38 / 48 | 9 | 1 | 25 / 48 | 95 | 173 |
| held-out | classic_othello_v2_teacher_safe | 39 / 48 | 9 | 1 | 26 / 48 | 94 | 141 |

Held-out validation objective:

| Config | Objective | Delta vs Base | Analyzed |
| --- | ---: | ---: | ---: |
| current_default | 19 | 0 | 48 |
| classic_othello_v1 | 19 | 0 | 48 |
| classic_othello_v2_teacher_safe | 20 | +1 | 48 |

## Search Validation

Command:

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --baseline-config data/eval/current_default.eval \
  --candidates data/eval/classic_othello_v1.eval data/eval/classic_othello_v2_teacher_safe.eval \
  --out runs/teacher-mistake-mining-v2/search-validation \
  --search-depths 5,6,7 \
  --positions evaluation \
  --repetitions 3 \
  --exact-endgame-threshold 0
```

This used iterative search, TT on, PVS on, aspiration on, and exact root disabled.

| Config | Nodes | Node Delta vs Base | Elapsed ms | Elapsed Delta vs Base | Score Kind | Used Exact Endgame | Best Move | Score |
| --- | ---: | ---: | ---: | ---: | --- | --- | --- | ---: |
| current_default | 613647 | 0.0% | 173.534 | 0.0% | heuristic | false | d6 | 69 |
| classic_othello_v1 | 584589 | -4.7% | 152.711 | -12.0% | heuristic | false | b2 | 129 |
| classic_othello_v2_teacher_safe | 553383 | -9.8% | 159.229 | -8.2% | heuristic | false | b2 | 101 |

Result and work checksums changed for both candidates, as expected for evaluator
behavior changes. Exact root searches stayed at zero.

## Match Smoke

Depth 5, TT on, PVS on, exact disabled, 24 games, side-swapped,
`data/openings/eval_regression_openings.txt`.

```sh
./build/othello_match_runner \
  --black search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/classic_othello_v2_teacher_safe.eval \
  --white search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/current_default.eval \
  --games 24 \
  --swap-sides true \
  --seed 20260531 \
  --openings data/openings/eval_regression_openings.txt \
  --output runs/teacher-mistake-mining-v2/match-smoke/v2-vs-current-depth5.jsonl \
  --quiet

./build/othello_match_runner \
  --black search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/classic_othello_v2_teacher_safe.eval \
  --white search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/classic_othello_v1.eval \
  --games 24 \
  --swap-sides true \
  --seed 20260531 \
  --openings data/openings/eval_regression_openings.txt \
  --output runs/teacher-mistake-mining-v2/match-smoke/v2-vs-v1-depth5.jsonl \
  --quiet
```

| Player A | Player B | A Wins | B Wins | Draws | Avg Disc Diff from A | Error Games | Exact Roots A/B |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| v2_teacher_safe | current_default | 13 | 11 | 0 | +2.33 | 0 | 0 / 0 |
| v2_teacher_safe | classic_othello_v1 | 15 | 9 | 0 | +1.08 | 0 | 0 / 0 |

This is smoke evidence only, not Elo and not proof of strength.

## Top Mistake Categories

| Config | `unknown_or_needs_manual_review` | `late_disc_count_greed` | `x_square_trap` | `edge_greed` | `corner_access_miss` |
| --- | ---: | ---: | ---: | ---: | ---: |
| current_default | 46 | 14 | 2 | 1 | 0 |
| classic_othello_v1 | 49 | 14 | 2 | 1 | 1 |
| classic_othello_v2_teacher_safe | 46 | 14 | 2 | 1 | 0 |

Many rows remain `unknown_or_needs_manual_review` because the current scalar
feature set does not expose enough structure to classify every teacher
disagreement safely. That is a useful negative result: several remaining
teacher misses may need pattern or teacher-label architecture rather than more
scalar tuning.

## Representative Positions

### eval-late-open-corner

- tags: `late_pre_endgame,corner_access,corner_available,edge_heavy,x_square_danger,x_square_risk,high_mobility,score_lopsided`
- teacher raw `engine_move`: `h8`; normalized move: `h1`
- current: `h1`, classic_v1: `c5`, v2: `h1`
- bucket: `corner_access_miss` for v1

```text
...BBB.W
...BB.W.
BBBBBB.B
.B.BWBB.
..BWWWBW
.BBBBBBB
.B.W.BBW
WBBB....
side=W
```

### eval-x-square-danger

- tags: `x_square_danger,x_square_risk,midgame,high_mobility,score_lopsided`
- teacher raw `engine_move`: `h7`; normalized move: `h2`
- current/classic_v1/v2 all selected `e8`
- bucket: `x_square_trap`

```text
.....W..
...WWW..
...WWW..
..WWWWBB
..WWWWBW
..WWWWWW
...WB...
...W....
side=B
```

### eval-edge-pattern-lopsided

- tags: `edge_pattern,edge_heavy,score_lopsided,midgame,high_mobility`
- teacher raw `engine_move`: `g5`; normalized move: `g4`
- current/classic_v1/v2 all selected `a6`; teacher rank was 2
- bucket: `edge_greed`

```text
...B.W.B
.B.B.WW.
.B.BBB.W
BBWBBB..
BBWBBB..
BBWBBB..
BWWB....
BBBBB...
side=W
```

### teacher-0

- teacher raw `engine_move`: `g6`; normalized move: `g3`
- exact-best moves: `h2 g3 a7`
- current/classic_v1/v2 all selected `h4`, exact gap 2
- bucket: `late_disc_count_greed`

```text
WWW.WWW.
.WB.BW.W
.BBBBBWW
WWWWBBWW
BBBBWBB.
.BBBWB.B
.BBBBWW.
B.BBBBWW
side=B
```

### teacher-5

- teacher raw `engine_move`: `c2`; normalized move: `c7`
- exact-best move: `c7`
- current/classic_v1/v2 all selected `h3`, exact gap 6
- bucket: `late_disc_count_greed`

```text
W....W..
WW..BBBB
WBWBBBBB
WBWWWBBB
.BWWWW.B
BBWWBWW.
.BBWWWWW
BBBBBBBB
side=B
```

### teacher-12

- teacher raw `engine_move`: `f5`; normalized move: `f4`
- current: `f4`, classic_v1: `d7`, v2: `f4`
- v2 recovered a v1 teacher regression

```text
...WWWWW
..W.WWW.
...WBBBB
...BWWWB
..WBW..B
.W.BWBW.
..W.B.B.
.....BBB
side=B
```

### teacher-57

- teacher raw `engine_move`: `a6`; normalized move: `a3`
- current: `a3`, classic_v1: `c5`, v2: `a3`
- v2 recovered a v1 teacher regression

```text
........
...B....
WWBBB...
.B.BB...
WWWWB...
.WWBB...
W.B.B...
.BBB....
side=B
```

## Positive Evidence

- All 107 usable NTest teacher rows were validated legal by C++ rule core.
- v2 preserved current-default teacher agreement while improving teacher move
  rank sum from 286 to 280.
- v2 recovered all four teacher regressions introduced by `classic_othello_v1`.
- In 22 teacher/exact overlap positions, NTest's move was exact-best every time.
- Held-out exact validation improved from objective 19 to 20.
- Held-out top exact-best improved from 24/48 current, 25/48 v1, to 26/48 v2.
- Held-out exact score gap sum improved from 157 current and 173 v1 to 141 v2.
- Search validation kept exact root disabled and reduced nodes on the evaluation
  diagnostic suite.
- Match smoke was positive against both current_default and classic_othello_v1.

## Negative Evidence

- Teacher agreement did not improve over current_default; v2 matched it.
- Exact-overlap agreement stayed at 9/22 for all configs.
- The teacher sample is only 107 usable rows at depth 26, not 1,000 or 10,000.
- NTest depth 26 cost made a 1,000-row local run unreasonable for this PR.
- Category classification is conservative and leaves many rows in manual review.
- Several exact-best late misses are unchanged by scalar v2, including
  `teacher-0` and `teacher-5`.
- Match smoke is small and opening-dependent.

## Risks

- The candidate may be overfit to shallow root evaluation and the local
  `eval_regression_openings.txt` smoke.
- The scalar feature set may be too coarse for the remaining teacher misses.
- Teacher labels are external-engine evidence, not exact truth. The 22-position
  exact overlap was clean but small.
- The current mining script ranks root moves via `othello_analyze_position`
  depth 1. Deeper search can choose different moves.

## Decision

Decision: keep `classic_othello_v2_teacher_safe` experimental and choose it for a
deeper follow-up match/teacher run.

Do not promote default. Do not add a C++ `EvaluationPreset`. The v2 candidate is
better supported than v1 for follow-up because it keeps current teacher choices,
recovers v1 regressions, and improves held-out exact move-rank evidence. It is
not strong enough for a default change.

Next discriminating experiment:

- run a 1,000-position NTest label job only if runtime can be reduced or queued;
- run a broader depth 5/6 side-swapped match with more openings;
- manually inspect unchanged exact-best late misses before raising late scalar
  weights;
- if scalar changes keep landing in `unknown_or_needs_manual_review`, stop scalar
  tuning and move toward pattern or teacher-label architecture.
