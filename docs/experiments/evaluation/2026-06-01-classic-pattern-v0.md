# 2026-06-01 Classic Pattern v0

Status: experimental report, not current guidance.

This report records a broad classic sparse-pattern baseline after
`pattern_teacher_v0` and `pattern_teacher_v1`. It is not a default-promotion
proposal, not a C++ `EvaluationPreset`, not an Elo claim, not proof of strength,
and it does not change exact solver or search semantics.

## Metadata

- base git SHA: `e772c2f93e96d0972e2ccd571fbd745a4626dc8b`
- report generation SHA: `e772c2f93e96d0972e2ccd571fbd745a4626dc8b` plus this working-tree experiment
- raw output location: `runs/classic-pattern-v0/`
- raw outputs committed: no
- generated teacher labels committed: no
- generated exact labels committed: no
- raw match JSONL committed: no
- local NTest paths committed: no

Compared configs and tables:

| Item | Path | sha256 |
| --- | --- | --- |
| current_default | `data/eval/current_default.eval` | `42d66384d725c55f46261624d78e4723846d63df1d2c1b41200429c4f4fa99cd` |
| classic_othello_v3_teacher_aggressive | `data/eval/classic_othello_v3_teacher_aggressive.eval` | `cbaec437d6dfb2efc1b6af7afb977a030bd4c7aab54386d65f4739bf0495b7be` |
| pattern_teacher_v0 | `data/eval/pattern_teacher_v0.eval` | `020fc737bbf1a5e264559e857c2f4995fcc23def8b56d28113dc3b00a5290743` |
| pattern_teacher_v1 | `data/eval/pattern_teacher_v1.eval` | `dd8e614e1dc14a316e07600136ddf51713e31d025f610dbffe1ad7c98263dbb8` |
| classic_pattern_v0 | `data/eval/classic_pattern_v0.eval` | `2dd735a6f79ee8238f2be4cbd373898d4166fe6cbad6d0fd35d798d3cf78945a` |
| classic_pattern_v0 table | `data/eval/patterns/classic_pattern_v0.tsv` | `99f234ef9831a59fe80e20dbb3737c188946b5b204f46aeac87fc9ba3ebc1022` |

## Hypothesis

The earlier pattern experiments proved that small sparse tables can be loaded
through `.eval` configs and can improve some NTest and exact-label metrics. The
hypothesis here was that a broader classic Othello pattern vocabulary would
recover scalar-blind positions better than the existing corner 2x3 and edge 8
tables.

This experiment deliberately keeps the change bounded:

- no default promotion
- no C++ preset
- no external-engine logic in engine core
- no raw teacher or exact labels committed
- one sparse table path loaded through `.eval`

## Representation

`classic_pattern_v0` adds one experimental `.eval` config and one sparse TSV
table:

```text
pattern_table=patterns/classic_pattern_v0.tsv
opening.pattern_table=6
midgame.pattern_table=6
late.pattern_table=6
```

The engine gained deterministic index and table-sum support for additional
families. Table loading still happens through the tooling `.eval` loader, and
the core search/evaluation path only receives in-memory arrays.

Committed table shape:

| Family | Full Index Space | Nonzero Entries | Notes |
| --- | ---: | ---: | --- |
| `corner_3x3` | 19683 | 78 | 3x3 corner regions around all corners |
| `edge_8` | 6561 | 96 | Existing side-relative full-edge index |
| `edge_x_10` | 59049 | 40 | Edge plus adjacent X squares |
| `diagonal_8` | 6561 | 48 | Main diagonals |
| `inner_row_8` | 6561 | 80 | Second ranks and files |

The committed broad table was trained from the final train split only. It was
kept as a reviewable experimental baseline even though the validation and
holdout metrics do not justify promotion.

## Teacher Data

The run started with a 300-position bootstrap so implementation and validation
could begin quickly while the larger NTest job was still running. The final
reported data uses the expanded depth-26 NTest labels.

NTest workflow command shape, with local paths intentionally parameterized:

```sh
python3 tools/scripts/external_teacher_label_workflow.py \
  --positions runs/classic-pattern-v0/teacher/shards/positions_2000_shard0.txt \
  --out runs/classic-pattern-v0/teacher/ntest-depth26-2000-shard0 \
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

The same command shape was used for shards 1 through 3 and the `extra30` fill
batch. Raw workflow outputs remain under `runs/classic-pattern-v0/`.

Teacher acceptance filter:

- `status == "ok"`
- `legal_move_valid == true`
- `move_token_valid == true`
- `move` is not null

Teacher counts:

| Source | Requested | OK | Failed | Usable | Illegal Teacher Moves | Invalid Tokens |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| shard0 | 500 | 500 | 0 | 500 | 0 | 0 |
| shard1 | 500 | 499 | 1 | 499 | 0 | 1 |
| shard2 | 500 | 499 | 1 | 499 | 0 | 1 |
| shard3 | 500 | 499 | 1 | 499 | 0 | 1 |
| extra30 | 30 | 30 | 0 | 30 | 0 | 0 |
| total | 2030 | 2027 | 3 | 2027 | 0 | 3 |

Accepted teacher empties distribution:

| Dataset | Distribution |
| --- | --- |
| final accepted teacher rows | 8:204, 10:204, 12:202, 16:202, 20:202, 24:203, 30:201, 36:203, 42:203, 50:203 |

Deterministic split:

- split method: stable SHA-256 hash of `split_seed`, normalized board text, and teacher move
- split ratios: 60 / 20 / 20
- split seed: `20260601`

| Split | Rows | Empties Distribution |
| --- | ---: | --- |
| train | 1234 | 8:118, 10:131, 12:120, 16:121, 20:116, 24:136, 30:120, 36:124, 42:125, 50:123 |
| validation | 384 | 8:39, 10:33, 12:40, 16:45, 20:47, 24:31, 30:36, 36:39, 42:35, 50:39 |
| holdout | 409 | 8:47, 10:40, 12:42, 16:36, 20:39, 24:36, 30:45, 36:40, 42:43, 50:41 |

## Exact Data

Exact labels were generated with root move scores for the fixed exact train and
held-out sets, plus the low-empty overlap from the NTest teacher positions.

```sh
./build/othello_exact_label_dump \
  --input runs/classic-pattern-v0/exact/train_positions.txt \
  --output runs/classic-pattern-v0/exact/train_labels.jsonl \
  --max-empties 12 \
  --include-move-scores

./build/othello_exact_label_dump \
  --input runs/classic-pattern-v0/exact/heldout_positions.txt \
  --output runs/classic-pattern-v0/exact/heldout_labels.jsonl \
  --max-empties 12 \
  --include-move-scores

./build/othello_exact_label_dump \
  --input runs/classic-pattern-v0/teacher/positions_2000.txt \
  --output runs/classic-pattern-v0/exact/teacher2000_max12_labels.jsonl \
  --max-empties 12 \
  --include-move-scores
```

| Dataset | Labeled | Notes |
| --- | ---: | --- |
| exact train | 96 | includes root move scores |
| exact held-out | 96 | includes root move scores |
| teacher2000 overlap dump | 602 | `max_empties <= 12` |
| extra30 overlap dump | 10 | `max_empties <= 12` |

Across accepted teacher rows, the mining reports observed 610 exact-overlap
rows: 369 train, 112 validation, and 129 holdout.

## Training

Final broad-table training command:

```sh
python3 tools/scripts/pattern_teacher_v0_train.py \
  --teacher-labels runs/classic-pattern-v0/teacher/ntest-depth26-merged-2027/labels.jsonl \
  --exact-labels runs/classic-pattern-v0/exact/teacher2000_max12_labels.jsonl,runs/classic-pattern-v0/exact/teacher_extra30_max12_labels.jsonl \
  --eval-config data/eval/classic_othello_v3_teacher_aggressive.eval \
  --analyze-position build/othello_analyze_position \
  --out runs/classic-pattern-v0/training-2027/classic_pattern_v0_broad_all.tsv \
  --table-name classic_pattern_v0_broad_all \
  --families broad_all \
  --corner-3x3-pairs 56 \
  --edge-pairs 48 \
  --edge-x-10-pairs 56 \
  --diagonal-pairs 40 \
  --inner-row-pairs 40 \
  --min-abs-diff 3 \
  --scale 4 \
  --max-abs-weight 5 \
  --split train \
  --split-ratios 60,20,20 \
  --split-seed 20260601 \
  --write-splits runs/classic-pattern-v0/splits-2027
```

Training summary:

| Accepted Train Rows | Already Agreed | Residual Updates | Family Count |
| ---: | ---: | ---: | ---: |
| 1234 | 541 | 693 | 5 |

Candidate sweep:

| Candidate | Training Idea |
| --- | --- |
| `broad_all` | all broad families, committed as `classic_pattern_v0` |
| `corner_only` | only `corner_3x3` |
| `edge_context_only` | only `edge_x_10` |
| `broad_regularized` | all broad families with more conservative weights |

## Teacher Metrics

Train split:

| Config | Teacher Agreement | Exact Overlap Agreement | Teacher Rank Sum | Exact-Best Rank Sum |
| --- | ---: | ---: | ---: | ---: |
| current_default | 506 / 1234 | 178 / 369 | 3175 | 768 |
| v3_teacher_aggressive | 541 / 1234 | 210 / 369 | 2947 | 700 |
| pattern_teacher_v0 | 530 / 1234 | 217 / 369 | 3034 | 677 |
| pattern_teacher_v1 | 529 / 1234 | 214 / 369 | 3038 | 686 |
| broad_all | 550 / 1234 | 221 / 369 | 3036 | 675 |
| corner_only | 543 / 1234 | 213 / 369 | 2991 | 693 |
| edge_context_only | 544 / 1234 | 210 / 369 | 2943 | 699 |
| broad_regularized | 545 / 1234 | 217 / 369 | 3046 | 686 |

Validation split:

| Config | Teacher Agreement | Exact Overlap Agreement | Teacher Rank Sum | Exact-Best Rank Sum |
| --- | ---: | ---: | ---: | ---: |
| current_default | 164 / 384 | 57 / 112 | 940 | 223 |
| v3_teacher_aggressive | 179 / 384 | 64 / 112 | 861 | 199 |
| pattern_teacher_v0 | 175 / 384 | 59 / 112 | 899 | 206 |
| pattern_teacher_v1 | 179 / 384 | 64 / 112 | 895 | 197 |
| broad_all | 167 / 384 | 59 / 112 | 906 | 206 |
| corner_only | 175 / 384 | 63 / 112 | 875 | 200 |
| edge_context_only | 180 / 384 | 63 / 112 | 869 | 200 |
| broad_regularized | 174 / 384 | 62 / 112 | 899 | 201 |

Holdout split:

| Config | Teacher Agreement | Exact Overlap Agreement | Teacher Rank Sum | Exact-Best Rank Sum |
| --- | ---: | ---: | ---: | ---: |
| current_default | 184 / 409 | 72 / 129 | 994 | 245 |
| v3_teacher_aggressive | 183 / 409 | 88 / 129 | 933 | 214 |
| pattern_teacher_v0 | 188 / 409 | 91 / 129 | 950 | 204 |
| pattern_teacher_v1 | 183 / 409 | 91 / 129 | 955 | 211 |
| broad_all | 182 / 409 | 90 / 129 | 1012 | 212 |
| corner_only | 179 / 409 | 89 / 129 | 954 | 212 |
| edge_context_only | 183 / 409 | 88 / 129 | 935 | 214 |
| broad_regularized | 186 / 409 | 90 / 129 | 991 | 211 |

Compared with `pattern_teacher_v0`, `broad_all` recovered 32 train misses and
introduced 23 train regressions. On validation it recovered 15 and regressed
18; on holdout it recovered 13 and regressed 19. This is the main reason the
broad table is not promoted.

Top bucket counts for `broad_all`:

| Split | Unknown/Manual | Late Disc-Count Greed |
| --- | ---: | ---: |
| train | 454 | 230 |
| validation | 133 | 84 |
| holdout | 155 | 72 |

The validation unknown/manual bucket is worse than v3, `pattern_teacher_v0`,
`pattern_teacher_v1`, and `edge_context_only`; the holdout bucket is also worse
than v3 and `pattern_teacher_v0`.

## Exact Metrics

Train exact labels:

| Config | Sign Agreements | Wrong Direction | High-Confidence Wrong | Exact-Best Top Group | Exact-Best Rank Sum | Exact Score Gap Sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| current_default | 67 / 96 | 24 | 2 | 54 / 96 | 190 | 380 |
| v3_teacher_aggressive | 77 / 96 | 15 | 0 | 64 / 96 | 153 | 264 |
| pattern_teacher_v0 | 81 / 96 | 11 | 0 | 64 / 96 | 154 | 244 |
| pattern_teacher_v1 | 78 / 96 | 13 | 0 | 65 / 96 | 155 | 244 |
| classic_pattern_v0 | 78 / 96 | 14 | 0 | 63 / 96 | 156 | 284 |

Held-out exact labels:

| Config | Sign Agreements | Wrong Direction | High-Confidence Wrong | Exact-Best Top Group | Exact-Best Rank Sum | Exact Score Gap Sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| current_default | 79 / 96 | 17 | 1 | 47 / 96 | 207 | 378 |
| v3_teacher_aggressive | 78 / 96 | 18 | 0 | 53 / 96 | 199 | 301 |
| pattern_teacher_v0 | 79 / 96 | 17 | 0 | 50 / 96 | 195 | 333 |
| pattern_teacher_v1 | 78 / 96 | 18 | 0 | 52 / 96 | 195 | 323 |
| classic_pattern_v0 | 79 / 96 | 17 | 0 | 54 / 96 | 199 | 301 |

`classic_pattern_v0` slightly improves held-out exact-best top-group count
against v3, but it ties v3 rank sum and gap, loses to `pattern_teacher_v0` and
`pattern_teacher_v1` on held-out exact-best rank sum, and loses on train exact
metrics.

## Search Validation

The final held-out matrix also ran the search benchmark over iterative depth
5/6/7 aggregates with the same search semantics as the compared configs.

Held-out search summary:

| Config | Nodes | Elapsed ms | Best Move | Score | Principal Variation |
| --- | ---: | ---: | --- | ---: | --- |
| current_default | 613647 | 171.3447 | d6 | 69 | d6 a5 b6 b5 a4 |
| v3_teacher_aggressive | 633834 | 156.7514 | c2 | 135 | c2 d3 f6 b3 a4 |
| pattern_teacher_v0 | 565020 | 197.9739 | f7 | 154 | f7 e7 c2 g8 f5 |
| pattern_teacher_v1 | 641889 | 202.1330 | c2 | 159 | c2 b3 a3 d3 f7 |
| classic_pattern_v0 | 709425 | 230.2277 | c2 | 147 | c2 d3 f6 g6 d2 |

The broader table changed the search checksum and selected PV, as expected for
an evaluation change. It also increased held-out search nodes and elapsed time
relative to the baseline and all compared pattern configs in this run.

## Conclusion

`classic_pattern_v0` is useful as a broad sparse-pattern plumbing and review
baseline, but it should not be promoted as a strength improvement. The expanded
2027-row teacher set showed train improvement, but validation and holdout did
not generalize. The held-out exact set has one encouraging top-group metric, but
the broader exact and search picture is mixed to negative.

Recommended next step: keep the broad family support and table loader tests,
but treat this table as an experimental baseline. A future iteration should
prefer narrower family selection, stronger regularization, or a phase-aware
split before any default promotion is considered.
