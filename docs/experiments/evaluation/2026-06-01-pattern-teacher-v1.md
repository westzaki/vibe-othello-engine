# 2026-06-01 Pattern Teacher v1

Status: experimental report, not current guidance.

This report records a serious pattern-evaluation improvement iteration after
`pattern_teacher_v0`. It is not a default-promotion proposal, not a C++
`EvaluationPreset`, not an Elo claim, not proof of strength, and it does not
change exact solver or search semantics.
Historical `data/eval/pattern_teacher_v0.eval` paths referenced below have
been removed from active source-controlled eval configs; the command blocks and
comparison paths are retained as provenance only. Current work should use
`current_default.eval`, `ntest_pairwise_full_v2.eval`, or an explicit new
config.

## Metadata

- base git SHA: `f2f92338af14821cef1d484849ce47e7a875846c`
- report generation SHA: `f2f92338af14821cef1d484849ce47e7a875846c` plus this working-tree experiment
- raw output location: `runs/pattern-teacher-v1/`
- reused teacher/exact raw output location: `runs/teacher-aggressive-v3/`
- raw outputs committed: no
- generated teacher labels committed: no
- generated exact labels committed: no
- raw match JSONL committed: no
- local NTest paths committed: no

Compared configs and tables:

| Item | Path | sha256 |
| --- | --- | --- |
| current_default | `data/eval/current_default.eval` | `42d66384d725c55f46261624d78e4723846d63df1d2c1b41200429c4f4fa99cd` |
| classic_othello_v2_teacher_safe | `data/eval/classic_othello_v2_teacher_safe.eval` | `510e9fc66a425a3e16cef959d2405eadf091c4316799d7c13d318dbb8896891a` |
| classic_othello_v3_teacher_aggressive | `data/eval/classic_othello_v3_teacher_aggressive.eval` | `cbaec437d6dfb2efc1b6af7afb977a030bd4c7aab54386d65f4739bf0495b7be` |
| pattern_teacher_v0 | `data/eval/pattern_teacher_v0.eval` | `020fc737bbf1a5e264559e857c2f4995fcc23def8b56d28113dc3b00a5290743` |
| pattern_teacher_v1 | `data/eval/pattern_teacher_v1.eval` | `be43f547fc675320419c478f1c4cc5c02885e79cf60db8339780791e72d9c068` |
| pattern_teacher_v1_rank | `data/eval/pattern_teacher_v1_rank.eval` | `028c1f6dca4e3e5e5da57bd135a28e2910f89bcd07d24f567a67f4eff81775b3` |
| pattern_teacher_v1_phase | `data/eval/pattern_teacher_v1_phase.eval` | `249ef1b64fee7c036058687fb2fc3593c2cc486a0ccbc2383eb0ad229532df0f` |
| v1 table | `data/eval/patterns/pattern_teacher_v1.tsv` | `ba71a99727907821eb025eb9ea359bf9606d51bb8e902f82a120158b3dba6cc7` |
| v1 rank table | `data/eval/patterns/pattern_teacher_v1_rank.tsv` | `243ba1184946dd70589347adf9c0de6a02e1ef8605f4fbb29ea986f1fe443f70` |
| v1 phase table | `data/eval/patterns/pattern_teacher_v1_phase.tsv` | `9dfa6ea7907990bfba2866c6adb99f4591151f01c61870975036affb08132115` |

## Hypothesis

`pattern_teacher_v0` proved the config-gated sparse pattern-table path, but it
trained and evaluated on the same 1006 validated NTest teacher rows. The v1
hypothesis was that a train/validation/holdout split plus residual, rank-aware,
and small-empty pattern updates would generalize better than v0 and scalar v3.

## Representation

The committed v1 candidates still use the existing sparse table-capable
families only:

| Candidate | Training idea | Table weights | Corner entries | Edge entries |
| --- | --- | --- | ---: | ---: |
| `pattern_teacher_v1` | train-split residuals against v3 | 6 / 6 / 6 | 80 | 76 |
| `pattern_teacher_v1_rank` | train-split rank-aware updates against v3 | 4 / 4 / 4 | 80 | 80 |
| `pattern_teacher_v1_phase` | train-split small-empty residuals, `empty_count <= 12` | 8 / 8 / 8 | 8 | 12 |

`pattern_teacher_v1_phase` is only an approximation of a phase experiment. The
current loader supports one table path per config plus phase weights, not
separate phase-specific table sections. The small-empty table was therefore
tested with several phase-weight choices; all-phase weight 8 had the best
validation teacher-rank result among those variants, but it did not generalize
on holdout.

## Teacher Data

This iteration reused the validated NTest depth-26 data from the v3 run rather
than committing or regenerating raw labels. Additional 2000-3000 label expansion
was deferred because the main scientific change here was separating the existing
1006 legal rows into train/validation/holdout and testing generalization.

Teacher workflow command shape, with local paths intentionally parameterized:

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

Diagnostic labels used the same workflow over
`data/positions/evaluation/diagnostic_suite.txt`.

Teacher acceptance filter:

- `status == "ok"`
- `legal_move_valid == true`
- `move_token_valid == true`
- `move` is not null

Teacher counts:

| Source | Requested | OK | Failed | Usable | Legal Valid | Illegal Teacher Moves | Validator Failures | Invalid Tokens |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| random1000 | 1000 | 999 | 1 | 999 | 999 | 0 | 0 | 1 |
| diagnostic_suite | 8 | 7 | 1 | 7 | 7 | 0 | 0 | 1 |
| total | 1008 | 1006 | 2 | 1006 | 1006 | 0 | 0 | 2 |

Deterministic split:

- split method: stable SHA-256 hash of `split_seed`, normalized board text, and teacher move
- split ratios: 60 / 20 / 20
- split seed: `20260601`

| Split | Rows | Empties Distribution |
| --- | ---: | --- |
| train | 591 | 8:55, 10:63, 12:56, 16:65, 20:57, 21:1, 23:1, 24:52, 30:59, 36:66, 42:59, 50:57 |
| validation | 216 | 8:20, 10:18, 12:23, 16:17, 20:19, 24:24, 30:23, 36:20, 42:23, 50:28, 55:1 |
| holdout | 199 | 8:25, 10:20, 12:21, 16:17, 20:24, 24:24, 25:1, 30:18, 36:15, 42:18, 50:15, 53:1 |

## Exact Data

Exact labels were reused from the v3 run:

```sh
./build/othello_exact_label_dump \
  --input runs/teacher-aggressive-v3/exact/train_positions.txt \
  --output runs/teacher-aggressive-v3/exact/train_labels.jsonl \
  --max-empties 12 \
  --include-move-scores

./build/othello_exact_label_dump \
  --input runs/teacher-aggressive-v3/exact/heldout_positions.txt \
  --output runs/teacher-aggressive-v3/exact/heldout_labels.jsonl \
  --max-empties 12 \
  --include-move-scores

./build/othello_exact_label_dump \
  --input runs/teacher-aggressive-v3/teacher/random1000_positions.txt \
  --output runs/teacher-aggressive-v3/exact/teacher_random1000_max12_labels.jsonl \
  --max-empties 12 \
  --include-move-scores
```

| Dataset | Labeled | Notes |
| --- | ---: | --- |
| exact train | 96 | includes root move scores |
| exact held-out | 96 | includes root move scores |
| teacher random1000 overlap | 300 | `max_empties <= 12` |
| diagnostic overlap | 1 | `max_empties <= 12` |

Teacher-vs-exact overlap remained 301 positions. The validated NTest teacher
move was in the exact-best group for all 301 overlap rows.

## Training

Main training command for `pattern_teacher_v1`:

```sh
python3 tools/scripts/pattern_teacher_v0_train.py \
  --teacher-labels runs/teacher-aggressive-v3/teacher/ntest-depth26-random1000/labels.jsonl,runs/teacher-aggressive-v3/teacher/ntest-depth26-diagnostic/labels.jsonl \
  --exact-labels runs/teacher-aggressive-v3/exact/teacher_random1000_max12_labels.jsonl,runs/teacher-aggressive-v3/exact/diagnostic_max12_labels.jsonl \
  --eval-config data/eval/classic_othello_v3_teacher_aggressive.eval \
  --analyze-position build/othello_analyze_position \
  --out runs/pattern-teacher-v1/training/pattern_teacher_v1_general.tsv \
  --table-name pattern_teacher_v1_general \
  --corner-pairs 40 \
  --edge-pairs 40 \
  --min-abs-diff 2 \
  --scale 3 \
  --max-abs-weight 5 \
  --split train \
  --split-ratios 60,20,20 \
  --split-seed 20260601 \
  --write-splits runs/pattern-teacher-v1/splits
```

The trainer now supports deterministic split writing, split selection,
rank-aware updates, small-empty filtering, and deterministic table rendering.
No heavy ML dependency was added.

Candidate generation summaries:

| Candidate | Baseline | Mode | Filters | Accepted Rows | Already Agreed | Updates |
| --- | --- | --- | --- | ---: | ---: | ---: |
| v1 | v3 | residual | train split | 591 | 287 | 304 |
| v1_rank | v3 | rank-aware | train split | 591 | 287 | 304 positions, 750 rank pairs |
| v1_phase | v3 | residual | train split, `empty_max=12` | 591 | 98 | 76 |

## Teacher Metrics

Train split:

| Config | Teacher Agreement | Teacher Rank Sum | Exact Overlap Agreement | Exact-Best Rank Sum |
| --- | ---: | ---: | ---: | ---: |
| current_default | 254 / 591 | 1447 | 99 / 174 | 311 |
| v2_teacher_safe | 261 / 591 | 1428 | 100 / 174 | 311 |
| v3_teacher_aggressive | 287 / 591 | 1341 | 109 / 174 | 302 |
| pattern_teacher_v0 | 295 / 591 | 1343 | 116 / 174 | 291 |
| pattern_teacher_v1 | 289 / 591 | 1372 | 111 / 174 | 299 |
| pattern_teacher_v1_rank | 289 / 591 | 1291 | 110 / 174 | 299 |
| pattern_teacher_v1_phase | 289 / 591 | 1340 | 110 / 174 | 301 |

Validation split:

| Config | Teacher Agreement | Teacher Rank Sum | Exact Overlap Agreement | Exact-Best Rank Sum |
| --- | ---: | ---: | ---: | ---: |
| current_default | 90 / 216 | 506 | 29 / 61 | 126 |
| v2_teacher_safe | 90 / 216 | 505 | 31 / 61 | 122 |
| v3_teacher_aggressive | 100 / 216 | 460 | 38 / 61 | 108 |
| pattern_teacher_v0 | 96 / 216 | 481 | 38 / 61 | 106 |
| pattern_teacher_v1 | 96 / 216 | 477 | 37 / 61 | 108 |
| pattern_teacher_v1_rank | 99 / 216 | 463 | 36 / 61 | 110 |
| pattern_teacher_v1_phase | 99 / 216 | 457 | 38 / 61 | 109 |

Holdout split:

| Config | Teacher Agreement | Teacher Rank Sum | Exact Overlap Agreement | Exact-Best Rank Sum |
| --- | ---: | ---: | ---: | ---: |
| current_default | 73 / 199 | 567 | 27 / 66 | 150 |
| v2_teacher_safe | 78 / 199 | 559 | 26 / 66 | 154 |
| v3_teacher_aggressive | 77 / 199 | 502 | 35 / 66 | 126 |
| pattern_teacher_v0 | 87 / 199 | 477 | 35 / 66 | 121 |
| pattern_teacher_v1 | 80 / 199 | 501 | 36 / 66 | 125 |
| pattern_teacher_v1_rank | 79 / 199 | 484 | 35 / 66 | 122 |
| pattern_teacher_v1_phase | 77 / 199 | 503 | 35 / 66 | 126 |

Recovered regressions vs v0:

| Split | v1 recovered / new regressions | v1_rank recovered / new regressions | v1_phase recovered / new regressions |
| --- | ---: | ---: | ---: |
| train | 12 / 18 | 20 / 26 | 22 / 28 |
| validation | 7 / 7 | 11 / 8 | 12 / 9 |
| holdout | 2 / 9 | 2 / 10 | 2 / 12 |

Holdout top buckets:

| Config | Unknown/manual | Late Disc-Count Greed | Corner Access |
| --- | ---: | ---: | ---: |
| v3_teacher_aggressive | 79 | 42 | 1 |
| pattern_teacher_v0 | 72 | 39 | 1 |
| pattern_teacher_v1 | 79 | 39 | 1 |
| pattern_teacher_v1_rank | 77 | 42 | 1 |
| pattern_teacher_v1_phase | 79 | 42 | 1 |

## Exact Held-Out

Held-out exact matrix with `--move-rank-analysis`:

| Config | Sign Agreements | Wrong Direction | High-Confidence Wrong | Exact-Best Top Group | Exact-Best Rank Sum | Exact Score Gap Sum | Validation Objective |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| current_default | 77 / 96 | 15 | 1 | 45 / 96 | 196 | 451 | 46 |
| v2_teacher_safe | 77 / 96 | 15 | 1 | 46 / 96 | 195 | 423 | 46 |
| v3_teacher_aggressive | 81 / 96 | 11 | 3 | 52 / 96 | 176 | 322 | 56 |
| pattern_teacher_v0 | 81 / 96 | 11 | 1 | 54 / 96 | 173 | 286 | 58 |
| pattern_teacher_v1 | 81 / 96 | 11 | 0 | 53 / 96 | 174 | 316 | 59 |
| pattern_teacher_v1_rank | 81 / 96 | 11 | 1 | 53 / 96 | 174 | 298 | 58 |
| pattern_teacher_v1_phase | 81 / 96 | 11 | 3 | 52 / 96 | 176 | 330 | 56 |

`pattern_teacher_v1` has the best validation objective only because it removed
the remaining high-confidence exact wrong case. It regressed v0 on exact-best
top group and exact score gap, so this is not clean evidence that v1 is better.

## Search Validation

Command profile:

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --labels runs/teacher-aggressive-v3/exact/heldout_labels.jsonl \
  --baseline-config data/eval/current_default.eval \
  --candidates ... \
  --out runs/pattern-teacher-v1/matrix/heldout \
  --search-depths 5,6,7 \
  --positions evaluation \
  --repetitions 3 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis
```

The script ran iterative search with TT, PVS, aspiration, and exact roots
disabled for evaluator comparison.

| Config | Search Status | Nodes | Elapsed ms | Exact Root Searches | Result Checksum Changed vs Current |
| --- | --- | ---: | ---: | ---: | --- |
| current_default | passed | 613647 | 160.21 | 0 | n/a |
| v2_teacher_safe | passed | 553383 | 148.74 | 0 | yes |
| v3_teacher_aggressive | passed | 633834 | 165.34 | 0 | yes |
| pattern_teacher_v0 | passed | 565020 | 181.55 | 0 | yes |
| pattern_teacher_v1 | passed | 641889 | 173.22 | 0 | yes |
| pattern_teacher_v1_rank | passed | 625755 | 169.42 | 0 | yes |
| pattern_teacher_v1_phase | passed | 688452 | 185.45 | 0 | yes |

## Match Smoke

Best v1 candidate for match smoke was `pattern_teacher_v1` because it had the
best exact held-out validation objective and zero high-confidence exact wrongs.
Matches used exact disabled, side swapping, and
`data/openings/eval_regression_openings.txt` with 100 games per matchup.

| Matchup | Depth | Result for v1 | Avg Disc Diff |
| --- | ---: | ---: | ---: |
| v1 vs current_default | 5 | 44-50-6 | -4.46 |
| v1 vs v2_teacher_safe | 5 | 31-64-5 | -6.24 |
| v1 vs v3_teacher_aggressive | 5 | 38-62-0 | -7.60 |
| v1 vs pattern_teacher_v0 | 5 | 47-46-7 | +1.36 |
| v1 vs current_default | 6 | 47-50-3 | -0.68 |
| v1 vs v2_teacher_safe | 6 | 47-50-3 | -3.30 |
| v1 vs v3_teacher_aggressive | 6 | 45-54-1 | -0.58 |
| v1 vs pattern_teacher_v0 | 6 | 45-53-2 | +0.44 |

No broader 200-game smoke was run because the 100-game key matchups were mixed
to negative and did not justify treating v1 as promising strength evidence.

## Representative Positions

v1 helps versus v0 on holdout:

| Position | Empty | Teacher | v0 | v1 | Note |
| --- | ---: | --- | --- | --- | --- |
| teacher-309 | 50 | c3 | c1 | c3 | early sparse pattern correction |
| teacher-390 | 8 | f8 | h6 | f8 | late exact-overlap correction |

v1 regresses versus v0 on holdout:

| Position | Empty | Teacher | v0 | v1 | Bucket |
| --- | ---: | --- | --- | --- | --- |
| teacher-74 | 20 | f1 | f1 | a8 | unknown/manual |
| teacher-217 | 36 | c2 | c2 | h3 | unknown/manual |
| teacher-285 | 24 | c6 | c6 | c4 | unknown/manual |
| teacher-515 | 24 | h1 | h1 | a8 | unknown/manual |
| teacher-517 | 36 | a5 | a5 | a8 | unknown/manual |

The regression examples are mostly still `unknown_or_needs_manual_review`,
which is a warning that the current corner_2x3 + edge_8 sparse vocabulary is too
blunt for several midgame teacher preferences.

## Candidate Decisions

`pattern_teacher_v1` was kept as the primary v1 candidate because it improved
held-out exact validation objective and removed the remaining high-confidence
exact wrong case. It was not selected as a stronger evaluator because teacher
holdout fell behind v0 by 7 top-1 agreements and match smoke was not clearly
positive.

`pattern_teacher_v1_rank` was useful diagnostically: it improved train rank sum
strongly and validation rank sum versus v0, but holdout teacher agreement and
exact overlap were weaker than v0 and it did not justify selection.

`pattern_teacher_v1_phase` showed the best validation rank among the phase
variants, but the current single-table config model made it a weak proxy for
true phase-specific pattern evaluation. Holdout metrics did not improve.

Rejected local variants:

- higher-weight general tables improved neither validation nor holdout
- residual-v0 tables improved validation top-1 to 101 / 216, but collapsed on
  holdout at 78-79 / 199
- late-only phase weights did not beat all-phase weight 8 on validation rank

## Positive Evidence

- Train/validation/holdout split is now deterministic and test-covered.
- v1 removed held-out exact high-confidence wrong count from 1 to 0.
- v1_exact objective beat v0 by the script objective, 59 vs 58.
- v1_rank reduced train teacher rank sum from v0's 1343 to 1291.
- v1_phase improved validation teacher rank sum to 457 vs v3's 460 and v0's 481.

## Negative Evidence

- Teacher holdout did not improve over v0: v0 87 / 199, v1 80 / 199.
- v1 recovered only 2 v0 holdout misses and introduced 9 new holdout regressions.
- Exact-best top group regressed versus v0: 54 / 96 to 53 / 96.
- Exact score gap regressed versus v0: 286 to 316.
- Match smoke was mixed to negative, especially depth 5 against v2 and v3.
- Unknown/manual holdout misses were not reduced versus v0.

## Risks

- v0 still has same-sample teacher overfitting risk, so beating it on holdout is
  harder to interpret until a larger independent teacher set is generated.
- The current table vocabulary is limited to corner_2x3 and edge_8; many
  midgame regressions are interior or broader mobility/parity shapes.
- `EvaluationConfig` stores table arrays by value. This remains acceptable for
  small sparse-loaded tables but should be revisited if table families grow.
- The loader has one table path per config. Real phase-specific pattern tables
  need either separate phase sections or separate table slots.

## Final Decision

Reject `pattern_teacher_v1` as a stronger evaluator candidate and keep
`pattern_teacher_v0` as the better historical comparison baseline in this
recorded run.

Continue pattern architecture, but do not keep squeezing this exact small-table
path as-is. The next useful step is one of:

- add true phase-specific pattern table support
- add a new high-leverage pattern family, likely corner 3x3 or edge plus adjacent
  interior row
- generate a larger independent validated teacher set before fitting more table
  entries

This PR is still useful because it proves that the v0 improvement does not
cleanly generalize under a deterministic split and because it adds the
reproducible split/rank training support needed for the next pattern iteration.
