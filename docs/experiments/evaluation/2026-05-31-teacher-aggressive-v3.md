# 2026-05-31 Teacher Aggressive v3

Status: experimental report, not current guidance.

This report records an aggressive scalar `.eval` iteration using validated
NTest depth-26 teacher labels. It is not a default-promotion proposal, not a
C++ `EvaluationPreset`, not an Elo claim, and not proof of strength.

## Metadata

- base git SHA: `bebfe9d95e621d2878bc699e37fa34dc881f1b5e`
- report head SHA before metadata refresh: `5ca2516c2f231e73fa34100254e36ce8dbcf6d84`
- PR176 source commit before merge commit: `aaa4767ff05dbef110163788a40aca9cd8049f50`
- raw output location: `runs/teacher-aggressive-v3/`
- raw outputs committed: no
- generated teacher labels committed: no
- generated exact labels committed: no
- raw match JSONL committed: no
- local NTest paths committed: no

Compared configs:

| Config | Path | sha256 |
| --- | --- | --- |
| current_default | `data/eval/current_default.eval` | `42d66384d725c55f46261624d78e4723846d63df1d2c1b41200429c4f4fa99cd` |
| classic_othello_v1 | `data/eval/classic_othello_v1.eval` | `f43a1de59fc298d4576b75cf807c7cb040cd8ac16349ad01196bda4348af973c` |
| classic_othello_v2_teacher_safe | `data/eval/classic_othello_v2_teacher_safe.eval` | `510e9fc66a425a3e16cef959d2405eadf091c4316799d7c13d318dbb8896891a` |
| classic_othello_v3_teacher_aggressive | `data/eval/classic_othello_v3_teacher_aggressive.eval` | `cbaec437d6dfb2efc1b6af7afb977a030bd4c7aab54386d65f4739bf0495b7be` |
| classic_othello_v3_teacher_rank | `data/eval/classic_othello_v3_teacher_rank.eval` | `f1f116b0baa212bab7322b028275f3329b88262233451fa127d989c95b5aaf0f` |
| classic_othello_v3_late_exact | `data/eval/classic_othello_v3_late_exact.eval` | `f08b7dfbc6d62c2afaf8c95334d276ab2a7a68e868fa24652bdbadc3402b1bdd` |

## Hypothesis

`classic_othello_v2_teacher_safe` recovered v1 teacher regressions but was
deliberately conservative. This v3 experiment asks whether a stronger scalar
move can improve teacher agreement, teacher move rank, and exact-label move
quality without promoting defaults or adding presets.

The candidates intentionally explore larger scalar changes:

- `classic_othello_v3_teacher_aggressive`: maximize validated teacher top-1
  agreement.
- `classic_othello_v3_teacher_rank`: prioritize teacher move rank while keeping
  exact held-out damage controlled.
- `classic_othello_v3_late_exact`: keep v2 opening/midgame weights but replace
  late weights with an exact-best top-group probe for empties 8/10/12.

## Teacher Label Commands

Position generation:

```sh
./build/othello_position_sampler \
  --output runs/teacher-aggressive-v3/teacher/random1000_positions.txt \
  --count 1000 \
  --target-empties 8,10,12,16,20,24,30,36,42,50 \
  --seed 2026053106
```

Random 1000 NTest labels:

```sh
python3 tools/scripts/external_teacher_label_workflow.py \
  --positions runs/teacher-aggressive-v3/teacher/random1000_positions.txt \
  --out runs/teacher-aggressive-v3/teacher/ntest-depth26-random1000 \
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

Diagnostic NTest labels:

```sh
python3 tools/scripts/external_teacher_label_workflow.py \
  --positions data/positions/evaluation/diagnostic_suite.txt \
  --out runs/teacher-aggressive-v3/teacher/ntest-depth26-diagnostic \
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

NTest settings: depth 26, timeout 60000 ms, NBoard protocol, legal validator
`build/othello_validate_move`. NTest raw coordinates were normalized through the
workflow's `engine_move` to `move` conversion before validation.

Only rows with all of these fields were used for mining:

- `status == "ok"`
- `legal_move_valid == true`
- `move_token_valid == true`
- `move` is not null

Teacher counts:

| Source | Requested | OK | Failed | Usable | `legal_move_valid=true` | Illegal Teacher Moves | Validator Failures | Invalid Tokens | Elapsed ms | Avg elapsed ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| diagnostic_suite | 8 | 7 | 1 | 7 | 7 | 0 | 0 | 1 | 15506.494 | 1938.312 |
| random1000 | 1000 | 999 | 1 | 999 | 999 | 0 | 0 | 1 | 1927010.308 | 1927.010 |
| total | 1008 | 1006 | 2 | 1006 | 1006 | 0 | 0 | 2 | 1942516.802 | 1928.942 |

Accepted teacher empties distribution:

| Source | Distribution |
| --- | --- |
| random1000 | 8:100, 10:100, 12:100, 16:99, 20:100, 24:100, 30:100, 36:100, 42:100, 50:100 |
| diagnostic_suite | 10:1, 21:1, 23:1, 25:1, 36:1, 53:1, 55:1 |

## Exact Label Commands

Train and held-out exact labels:

```sh
./build/othello_position_sampler \
  --output runs/teacher-aggressive-v3/exact/train_positions.txt \
  --count 96 \
  --target-empties 8,10,12 \
  --seed 2026053107

./build/othello_exact_label_dump \
  --input runs/teacher-aggressive-v3/exact/train_positions.txt \
  --output runs/teacher-aggressive-v3/exact/train_labels.jsonl \
  --max-empties 12 \
  --include-move-scores

./build/othello_position_sampler \
  --output runs/teacher-aggressive-v3/exact/heldout_positions.txt \
  --count 96 \
  --target-empties 8,10,12 \
  --seed 2026053108

./build/othello_exact_label_dump \
  --input runs/teacher-aggressive-v3/exact/heldout_positions.txt \
  --output runs/teacher-aggressive-v3/exact/heldout_labels.jsonl \
  --max-empties 12 \
  --include-move-scores
```

Exact overlap labels:

```sh
./build/othello_exact_label_dump \
  --input runs/teacher-aggressive-v3/teacher/random1000_positions.txt \
  --output runs/teacher-aggressive-v3/exact/teacher_random1000_max12_labels.jsonl \
  --max-empties 12 \
  --include-move-scores

./build/othello_exact_label_dump \
  --input data/positions/evaluation/diagnostic_suite.txt \
  --output runs/teacher-aggressive-v3/exact/diagnostic_max12_labels.jsonl \
  --max-empties 12 \
  --include-move-scores
```

Exact counts:

| Dataset | Input Positions | Labeled | Skipped Too Many Empties |
| --- | ---: | ---: | ---: |
| train | 96 | 96 | 0 |
| held-out | 96 | 96 | 0 |
| teacher random1000 overlap | 1000 | 300 | 700 |
| diagnostic overlap | 8 | 1 | 7 |

Teacher-vs-exact overlap was 301 positions. The validated NTest teacher move was
in the exact-best group for all 301.

## Candidate Design

Candidate weights came from local analysis over validated teacher rows. The
analysis extracted root legal-move feature vectors through
`othello_analyze_position --depth 1 --root-candidates`; it did not implement
Othello rules in Python. The resulting candidates were then re-measured through
the normal C++ tools.

Why the v3 weights differ:

- `teacher_aggressive` pushes teacher top-1 agreement hardest. It drops opening
  mobility and potential mobility, raises opening corner-pattern/stability
  signals, raises midgame frontier/local/stability, and makes late play much
  more corner/frontier/stability driven with edge pattern suppressed.
- `teacher_rank` is less top-1 focused. It uses high opening corner and
  X-square safety, zeroes edge pattern, pushes midgame corner/frontier, and uses
  lower late disc/mobility than v2 to reduce teacher rank even when top-1 does
  not flip.
- `late_exact` preserves v2 opening and midgame weights. Only the late phase is
  replaced: disc, mobility, potential mobility, frontier, edge stability, and
  edge pattern are suppressed; corner occupancy, corner access, X-square safety,
  and local corner shape carry the late scalar score.

## Teacher Agreement

Teacher agreement used 1006 validated teacher rows.

| Config | Teacher Agreement | Teacher Move Rank Sum | Exact Overlap Agreement | Exact Overlap Rank Sum |
| --- | ---: | ---: | ---: | ---: |
| current_default | 417 / 1006 | 2520 | 155 / 301 | 587 |
| classic_othello_v1 | 427 / 1006 | 2487 | 157 / 301 | 583 |
| classic_othello_v2_teacher_safe | 429 / 1006 | 2492 | 157 / 301 | 587 |
| classic_othello_v3_teacher_aggressive | 464 / 1006 | 2303 | 182 / 301 | 536 |
| classic_othello_v3_teacher_rank | 448 / 1006 | 2236 | 171 / 301 | 545 |
| classic_othello_v3_late_exact | 375 / 1006 | 2329 | 141 / 301 | 518 |
| NTest teacher | n/a | n/a | 301 / 301 | n/a |

Top mismatch buckets:

| Config | Unknown/manual | Late disc-count greed | X-square trap | Corner access | Edge greed |
| --- | ---: | ---: | ---: | ---: | ---: |
| current_default | 364 | 222 | 2 | 0 | 1 |
| classic_othello_v1 | 356 | 219 | 2 | 1 | 1 |
| classic_othello_v2_teacher_safe | 356 | 218 | 2 | 0 | 1 |
| classic_othello_v3_teacher_aggressive | 356 | 183 | 2 | 1 | 0 |
| classic_othello_v3_teacher_rank | 361 | 193 | 2 | 2 | 0 |
| classic_othello_v3_late_exact | 381 | 247 | 2 | 0 | 1 |

The majority of remaining mistakes are still
`unknown_or_needs_manual_review`, which is evidence that this scalar feature set
is running out of explanatory power.

## Exact Metrics

Eval-vs-exact and move-rank commands:

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --labels runs/teacher-aggressive-v3/exact/train_labels.jsonl \
  --baseline-config data/eval/current_default.eval \
  --candidates data/eval/classic_othello_v1.eval data/eval/classic_othello_v2_teacher_safe.eval data/eval/classic_othello_v3_teacher_aggressive.eval data/eval/classic_othello_v3_teacher_rank.eval data/eval/classic_othello_v3_late_exact.eval \
  --out runs/teacher-aggressive-v3/matrix/final-train \
  --search-depths 5,6,7 \
  --positions evaluation \
  --repetitions 1 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis

python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --labels runs/teacher-aggressive-v3/exact/heldout_labels.jsonl \
  --baseline-config data/eval/current_default.eval \
  --candidates data/eval/classic_othello_v1.eval data/eval/classic_othello_v2_teacher_safe.eval data/eval/classic_othello_v3_teacher_aggressive.eval data/eval/classic_othello_v3_teacher_rank.eval data/eval/classic_othello_v3_late_exact.eval \
  --out runs/teacher-aggressive-v3/matrix/final-heldout \
  --search-depths 5,6,7 \
  --positions evaluation \
  --repetitions 1 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis
```

Train exact labels:

| Config | Sign Agreements | Wrong Direction | High Confidence Wrong | Top Exact-Best | Exact-Best Rank Sum | Exact Score Gap Sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| current_default | 71 / 96 | 22 | 6 | 51 / 96 | 197 | 436 |
| classic_othello_v1 | 71 / 96 | 22 | 6 | 50 / 96 | 195 | 464 |
| classic_othello_v2_teacher_safe | 72 / 96 | 21 | 6 | 49 / 96 | 197 | 470 |
| classic_othello_v3_teacher_aggressive | 78 / 96 | 15 | 0 | 60 / 96 | 169 | 272 |
| classic_othello_v3_teacher_rank | 74 / 96 | 19 | 0 | 56 / 96 | 182 | 320 |
| classic_othello_v3_late_exact | 73 / 96 | 18 | 0 | 62 / 96 | 179 | 274 |

Held-out exact labels:

| Config | Sign Agreements | Wrong Direction | High Confidence Wrong | Top Exact-Best | Exact-Best Rank Sum | Exact Score Gap Sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| current_default | 77 / 96 | 15 | 1 | 45 / 96 | 196 | 451 |
| classic_othello_v1 | 78 / 96 | 14 | 2 | 44 / 96 | 195 | 451 |
| classic_othello_v2_teacher_safe | 77 / 96 | 15 | 1 | 46 / 96 | 195 | 423 |
| classic_othello_v3_teacher_aggressive | 81 / 96 | 11 | 3 | 52 / 96 | 176 | 322 |
| classic_othello_v3_teacher_rank | 78 / 96 | 12 | 0 | 56 / 96 | 175 | 342 |
| classic_othello_v3_late_exact | 72 / 96 | 18 | 0 | 61 / 96 | 176 | 305 |

Held-out validation objective:

| Config | Objective | Delta vs Base | Analyzed |
| --- | ---: | ---: | ---: |
| current_default | 46 | 0 | 96 |
| classic_othello_v1 | 48 | +2 | 96 |
| classic_othello_v2_teacher_safe | 46 | 0 | 96 |
| classic_othello_v3_teacher_aggressive | 56 | +10 | 96 |
| classic_othello_v3_teacher_rank | 54 | +8 | 96 |
| classic_othello_v3_late_exact | 36 | -10 | 96 |

## Search Validation

Command:

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --baseline-config data/eval/current_default.eval \
  --candidates data/eval/classic_othello_v1.eval data/eval/classic_othello_v2_teacher_safe.eval data/eval/classic_othello_v3_teacher_aggressive.eval data/eval/classic_othello_v3_teacher_rank.eval data/eval/classic_othello_v3_late_exact.eval \
  --out runs/teacher-aggressive-v3/search-validation-final \
  --search-depths 5,6,7 \
  --positions evaluation \
  --repetitions 3 \
  --exact-endgame-threshold 0
```

This used iterative search, TT on, PVS on, aspiration on, and exact root
disabled.

| Config | Nodes | Node Delta vs Base | Elapsed ms | Elapsed Delta vs Base | Score Kind | Used Exact Endgame | Exact Root Searches | Best Move | Score |
| --- | ---: | ---: | ---: | ---: | --- | --- | ---: | --- | ---: |
| current_default | 613647 | 0.0% | 174.712 | 0.0% | heuristic | false | 0 | d6 | 69 |
| classic_othello_v1 | 584589 | -4.7% | 152.947 | -12.5% | heuristic | false | 0 | b2 | 129 |
| classic_othello_v2_teacher_safe | 553383 | -9.8% | 159.195 | -8.9% | heuristic | false | 0 | b2 | 101 |
| classic_othello_v3_teacher_aggressive | 633834 | +3.3% | 156.260 | -10.6% | heuristic | false | 0 | c2 | 135 |
| classic_othello_v3_teacher_rank | 486801 | -20.7% | 123.974 | -29.0% | heuristic | false | 0 | c2 | 89 |
| classic_othello_v3_late_exact | 494868 | -19.4% | 133.515 | -23.6% | heuristic | false | 0 | b2 | 101 |

Result and work checksums changed, as expected for evaluator behavior changes.

## Match Smoke

All match smoke used exact disabled, side swapping, TT on, PVS on, and
`data/openings/eval_regression_openings.txt`. These are smoke checks only, not
Elo and not strength proof.

Depth 5, 48 games:

| Player A | Player B | A Wins | B Wins | Draws | Avg Disc Diff A | Error Games | Exact Roots A/B |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| v3_teacher_aggressive | current_default | 22 | 25 | 1 | -4.08 | 0 | 0 / 0 |
| v3_teacher_aggressive | v2_teacher_safe | 19 | 26 | 3 | -5.38 | 0 | 0 / 0 |
| v3_teacher_rank | current_default | 14 | 32 | 2 | -14.62 | 0 | 0 / 0 |
| v3_teacher_rank | v2_teacher_safe | 15 | 32 | 1 | -10.04 | 0 | 0 / 0 |
| v3_late_exact | current_default | 14 | 34 | 0 | -12.33 | 0 | 0 / 0 |
| v3_late_exact | v2_teacher_safe | 15 | 32 | 1 | -9.46 | 0 | 0 / 0 |

Because `v3_teacher_aggressive` had the best teacher and exact signals, it also
received a broader depth 5/6 smoke:

| Depth | Player A | Player B | Games | A Wins | B Wins | Draws | Avg Disc Diff A | Unique Openings | Exact Roots A/B |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 5 | v3_teacher_aggressive | current_default | 100 | 51 | 47 | 2 | -2.20 | 37 | 0 / 0 |
| 5 | v3_teacher_aggressive | v2_teacher_safe | 100 | 38 | 56 | 6 | -5.94 | 37 | 0 / 0 |
| 6 | v3_teacher_aggressive | current_default | 100 | 62 | 38 | 0 | +6.04 | 37 | 0 / 0 |
| 6 | v3_teacher_aggressive | v2_teacher_safe | 100 | 60 | 38 | 2 | +6.36 | 37 | 0 / 0 |

The depth-6 smoke is positive for `v3_teacher_aggressive`, while depth-5 is
mixed. This is still smoke evidence only.

## Regressions

Recovered and new teacher regressions versus `v2_teacher_safe`:

| Candidate | Recovered Top-1 | New Top-1 Regressions | Rank Improved Count | Rank Improved Sum | Rank Worsened Count | Rank Worsened Sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| v3_teacher_aggressive | 124 | 89 | 304 | 525 | 183 | 336 |
| v3_teacher_rank | 122 | 103 | 329 | 589 | 171 | 333 |
| v3_late_exact | 44 | 98 | 178 | 348 | 74 | 185 |

Representative positions are identified by local run row ID; board text remains
in `runs/teacher-aggressive-v3/mining/final/rows.tsv` and is not committed as
raw generated data.

| Position | Candidate | Kind | Empty | Teacher | v2 Move | v3 Move | Teacher Rank v2 -> v3 | Exact Best | Bucket |
| --- | --- | --- | ---: | --- | --- | --- | --- | --- | --- |
| teacher-143 | v3_teacher_aggressive | recovered | 16 | f8 | a1 | f8 | 6 -> 1 | - | unknown/manual |
| teacher-294 | v3_teacher_aggressive | recovered | 20 | c7 | f7 | c7 | 6 -> 1 | - | unknown/manual |
| teacher-616 | v3_teacher_rank | recovered | 30 | c3 | h4 | c3 | 7 -> 1 | - | unknown/manual |
| teacher-478 | v3_teacher_rank | recovered | 42 | a8 | d8 | a8 | 6 -> 1 | - | unknown/manual |
| teacher-341 | v3_late_exact | recovered | 10 | c1 | f8 | c1 | 5 -> 1 | c1 | unknown/manual |
| teacher-759 | v3_teacher_aggressive | new regression | 50 | c6 | c6 | d2 | 1 -> 10 | - | unknown/manual |
| teacher-121 | v3_teacher_aggressive | new regression | 10 | c8 | c8 | e2 | 1 -> 5 | c8 | late_disc_count_greed |
| teacher-626 | v3_teacher_rank | new regression | 30 | a6 | a6 | f5 | 1 -> 11 | - | unknown/manual |
| teacher-61 | v3_late_exact | new regression | 10 | a1 | a1 | h4 | 1 -> 5 | a1 | late_disc_count_greed |
| teacher-940 | v3_late_exact | new regression | 8 | h8 | h8 | c1 | 1 -> 5 | c1 h8 | late_disc_count_greed |

## Positive Evidence

- The required 1000-row NTest run completed with legal validation: 999 usable
  random rows, 0 illegal teacher moves, 0 validator failures.
- Teacher-vs-exact overlap was clean: NTest's validated move was exact-best in
  all 301 overlap positions.
- `v3_teacher_aggressive` improved teacher agreement over current/v1/v2:
  464/1006 vs 417/427/429.
- `v3_teacher_rank` improved teacher rank sum the most among top teacher
  candidates: 2236 vs v2's 2492.
- Both teacher-focused v3 candidates improved held-out exact-best rank sum and
  exact score gap versus current and v2.
- `v3_late_exact` achieved the best held-out exact-best top group, 61/96, but
  only as a late exact probe.
- Broader depth-6 smoke for `v3_teacher_aggressive` was positive against both
  current and v2 with exact disabled.

## Negative Evidence

- The 48-game depth-5 match smoke was negative for every v3 candidate against
  both current and v2.
- `v3_teacher_aggressive` worsened held-out high-confidence wrong count to 3,
  even though sign agreement improved.
- `v3_teacher_rank` had good rank and no high-confidence wrongs, but match smoke
  was strongly negative at depth 5.
- `v3_late_exact` improved late exact top-group metrics but badly worsened
  teacher agreement and held-out validation objective.
- Many recovered and regressed positions are still unknown/manual buckets,
  suggesting the scalar feature vocabulary is too blunt.

## Overfitting Risks

- The candidate search used the same 1006 validated teacher rows that appear in
  the teacher agreement table. Exact held-out and match smoke are therefore more
  important than the raw teacher top-1 gain.
- Scalar weights can exploit root depth-1 feature correlations that do not
  survive deeper search.
- NTest is teacher evidence, not exact truth. The clean exact overlap only
  covers empties <= 12 positions from the sample.
- `v3_teacher_aggressive` changes many weights at once, so it is not a
  clean one-factor causal experiment.

## Runtime Limitations

- The NTest random1000 run used about 1,927,010 ms of engine elapsed time, about
  1.927 seconds per requested row.
- Exact labels were limited to 96 train and 96 held-out positions at empties
  8/10/12.
- Broader 100-game match smoke was run only for `v3_teacher_aggressive`, because
  the 48-game smoke made `v3_teacher_rank` and `v3_late_exact` poor match-smoke
  candidates.
- Match smoke is deterministic and useful for regression detection, but it is
  not an Elo estimate.

## Decision

Decision: keep v3 experimental, do not promote.

`classic_othello_v3_teacher_aggressive` is the most interesting follow-up
candidate because it improves teacher agreement, teacher/exact overlap, exact
held-out sign agreement, and depth-6 match smoke. It is still not a promotion
candidate because depth-5 smoke is mixed and high-confidence exact mistakes
increased.

`classic_othello_v3_teacher_rank` is useful as a rank-quality reference, but the
match smoke is too negative to prefer it for deeper strength work.

`classic_othello_v3_late_exact` should be treated as a negative/control probe:
late exact top-group improved, but teacher agreement and match smoke are poor.

The scalar `.eval` path still has value for controlled experiments, but this run
strengthens the case for a pivot toward pattern evaluation and a teacher-label
architecture. The biggest remaining bucket is unknown/manual, and the scalar
candidates trade off teacher agreement, exact held-out quality, and match smoke
too sharply.

## Caveats

- no default promotion
- no C++ `EvaluationPreset`
- no exact solver semantic changes
- no search semantic changes
- no raw outputs committed
- no generated labels committed
- no NTest binary or local NTest path committed
- no Elo claim
- no strength proof
