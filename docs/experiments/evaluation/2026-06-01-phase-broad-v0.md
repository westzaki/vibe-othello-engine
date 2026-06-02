# 2026-06-01 Phase Broad v0

Status: experimental report, negative result, not current guidance.

This report records the first phase-aware broad pattern training run using the
new `phase_pattern_table_train.py` workflow. It is not a default-promotion
proposal, not a C++ `EvaluationPreset`, not an Elo claim, not proof of strength,
and it does not change exact solver or search semantics.
Historical `pattern_reboot_v0` and `pattern_teacher_v0` paths referenced below
have been removed from active source-controlled eval configs; the command
blocks are retained as provenance only. Current work should use
`current_default.eval`, `ntest_pairwise_full_v2.eval`, or an explicit new
config.

## Decision

`phase_broad_v0` is rejected as a stronger evaluator candidate.

The phase-aware broad trainer works: it generated separate opening, midgame, and
late sparse TSVs plus a phase-specific local `candidate.eval` under `runs/`.
The candidate also loaded through the C++ `.eval` path at the time. However,
the learned tables did not beat `pattern_teacher_v0` on validation, holdout,
exact heldout, or match smoke. This is useful workflow evidence, not a
candidate to promote.

Do not promote this candidate, do not add it to `data/eval`, and do not use it
for strength claims. `pattern_teacher_v0` is now a pruned historical baseline,
not a current comparison preset.

## Metadata

- experiment git SHA: `2e01a9942ce86d2ccc024b93f7ce661f422b3ffe`
- generated candidate: `runs/pattern-training/phase-broad-v0/candidate.eval`
- raw output location: `runs/pattern-training/phase-broad-v0/`
- source teacher data location: `runs/classic-pattern-v0/`
- raw outputs committed: no
- generated pattern TSVs committed: no
- generated candidate `.eval` committed: no
- generated teacher labels committed: no
- generated exact labels committed: no
- raw match JSONL committed: no
- benchmark logs committed: no
- local NTest / external-engine paths committed: no
- default promotion: no
- C++ preset: no

Compared configs and generated artifacts:

| Item | Path | sha256 |
| --- | --- | --- |
| current_default | `data/eval/current_default.eval` | `42d66384d725c55f46261624d78e4723846d63df1d2c1b41200429c4f4fa99cd` |
| pattern_teacher_v0 | `data/eval/pattern_teacher_v0.eval` | `2a058edb7fcea3b0ab0021d88a64c4e522574c93d0ba07f304bb93dca89bba60` |
| pattern_reboot_v0 | `data/eval/pattern_reboot_v0.eval` | `c73fd652b46394901972cfff4fcb0587b0e09a59e7416ae9099aebd98d12e55b` |
| phase_broad_v0 candidate | `runs/pattern-training/phase-broad-v0/candidate.eval` | `66cdc4fef838777d6ba48e247d97b92cea43b32b991110d7a81e9d4107b74162` |
| opening table | `runs/pattern-training/phase-broad-v0/tables/opening.tsv` | `23af472694251c011a4eccbfc42dfe59f96084c8a9342b406bd8feaa679cf06c` |
| midgame table | `runs/pattern-training/phase-broad-v0/tables/midgame.tsv` | `e9c741e4a9245696c92d3b1655f9cec499e43fea659e94d35529a79df016e9f5` |
| late table | `runs/pattern-training/phase-broad-v0/tables/late.tsv` | `49634feaf0ebfb9e2f4cb54da2b57b08fa8590babacfb5b5c7cc516561b000b3` |

## Dataset Availability

The shared dataset-root path was still not configured in this worktree:

- `VIBE_OTHELLO_DATASET_ROOT` was not set
- `config/datasets.local.toml` was absent

After checking the local ignored `runs/` tree, the previous 2027 NTest teacher
data was available in this same worktree under `runs/classic-pattern-v0/`.
This report uses those files because they are the intended 2000+ row teacher
artifacts, but they are still worktree-local raw artifacts. They should be
migrated to a shared dataset root before relying on them from a fresh worktree.

Teacher and exact input hashes:

| Dataset | Path | Rows | sha256 |
| --- | --- | ---: | --- |
| teacher train | `runs/classic-pattern-v0/splits-2027/teacher_train.jsonl` | 1234 | `88c3b16016f267152204cb023f15c8b93ec1a5406761480429492bbd9fe6a286` |
| teacher validation | `runs/classic-pattern-v0/splits-2027/teacher_validation.jsonl` | 384 | `8929879f49033b83a41827507c1e900a3d6465da194122342aecdd4499b2907a` |
| teacher holdout | `runs/classic-pattern-v0/splits-2027/teacher_holdout.jsonl` | 409 | `c154615a2cb4cb06effbc0bcec7d9eaef47b2039a9180da05db107b694234099` |
| teacher2000 exact overlap | `runs/classic-pattern-v0/exact/teacher2000_max12_labels.jsonl` | 602 | `df443d5480ef1f038e080474c9d19c3b8d551eed7831d1d8b6363840538e17b6` |
| extra30 exact overlap | `runs/classic-pattern-v0/exact/teacher_extra30_max12_labels.jsonl` | 10 | `6f0b43ab987bd01dad261676844ec666fadf9f4f5479e659f5e1d8d453187d94` |
| exact train | `runs/classic-pattern-v0/exact/train_labels.jsonl` | 96 | `d0f6933f1dab0282c5c2d163ab6c226d0abdda1d7174d53c4235ff2b73468a5c` |
| exact heldout | `runs/classic-pattern-v0/exact/heldout_labels.jsonl` | 96 | `850145595ea9fb0d2200e507cd0a2a4e4144e088b6027f4e1e4ba35c41d2fd80` |

## Training

```sh
python3 tools/scripts/phase_pattern_table_train.py \
  --teacher-labels runs/classic-pattern-v0/splits-2027/teacher_train.jsonl \
  --exact-labels runs/classic-pattern-v0/exact/teacher2000_max12_labels.jsonl,runs/classic-pattern-v0/exact/teacher_extra30_max12_labels.jsonl \
  --eval-config data/eval/pattern_reboot_v0.eval \
  --analyze-position build/othello_analyze_position \
  --out-dir runs/pattern-training/phase-broad-v0 \
  --table-name phase_broad_v0 \
  --families broad_all \
  --update-mode rank \
  --split all \
  --depth 1
```

Training settings:

- base config: `data/eval/pattern_reboot_v0.eval`
- phase cutoffs: opening <= 20 occupied, midgame <= 44 occupied, else late
- families: `corner_3x3`, `edge_8`, `edge_x_10`, `diagonal_8`, `inner_row_8`
- update mode: `rank`
- split: `all` because the input file was already the train split
- exact-best filtering: enabled for positions present in the teacher exact overlap
- scalar handcrafted feature weights: zero, inherited from `pattern_reboot_v0`

Train counts:

| Metric | Count |
| --- | ---: |
| accepted teacher rows | 1234 |
| training rows | 1234 |
| updates | 1108 |
| no-update rows | 126 |

Phase summary:

| Phase | Teacher Rows | Updates | Skipped | Sentinel | corner_3x3 | edge_8 | edge_x_10 | diagonal_8 | inner_row_8 |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| opening | 123 | 115 | 8 | false | 28 | 6 | 14 | 24 | 34 |
| midgame | 621 | 572 | 49 | false | 128 | 128 | 128 | 128 | 128 |
| late | 490 | 421 | 69 | false | 128 | 128 | 128 | 128 | 128 |

The generated `candidate.eval` uses phase-specific table keys:

```text
pattern_table.opening=tables/opening.tsv
pattern_table.midgame=tables/midgame.tsv
pattern_table.late=tables/late.tsv
```

## Teacher Validation

Teacher validation used `teacher_label_mistake_mining.py` at depth 1 with
exact root disabled. It compared `current_default`, `pattern_teacher_v0`,
`pattern_reboot_v0`, and the generated `phase_broad_v0` candidate.

Command shape:

```sh
python3 tools/scripts/teacher_label_mistake_mining.py \
  --teacher-labels runs/classic-pattern-v0/splits-2027/teacher_<split>.jsonl \
  --exact-labels runs/classic-pattern-v0/exact/teacher2000_max12_labels.jsonl runs/classic-pattern-v0/exact/teacher_extra30_max12_labels.jsonl \
  --config current_default=data/eval/current_default.eval \
  --config pattern_teacher_v0=data/eval/pattern_teacher_v0.eval \
  --config pattern_reboot_v0=data/eval/pattern_reboot_v0.eval \
  --config phase_broad_v0=runs/pattern-training/phase-broad-v0/candidate.eval \
  --out runs/pattern-training/phase-broad-v0/teacher-mining/<split> \
  --build-dir build \
  --depth 1 \
  --exact-endgame-threshold 0
```

Teacher top-1 agreement:

| Split | Config | Teacher Agreement | Teacher Rank Sum |
| --- | --- | ---: | ---: |
| train | current_default | 506 / 1234 | 3175 |
| train | pattern_teacher_v0 | 530 / 1234 | 3034 |
| train | pattern_reboot_v0 | 126 / 1234 | 4503 |
| train | phase_broad_v0 | 353 / 1234 | 3678 |
| validation | current_default | 164 / 384 | 940 |
| validation | pattern_teacher_v0 | 175 / 384 | 899 |
| validation | pattern_reboot_v0 | 41 / 384 | 1340 |
| validation | phase_broad_v0 | 79 / 384 | 1357 |
| holdout | current_default | 184 / 409 | 994 |
| holdout | pattern_teacher_v0 | 188 / 409 | 950 |
| holdout | pattern_reboot_v0 | 40 / 409 | 1560 |
| holdout | phase_broad_v0 | 96 / 409 | 1406 |

Teacher/exact-overlap agreement:

| Split | Config | Exact Rows | Exact-Best Agreement | Exact-Best Rank Sum |
| --- | --- | ---: | ---: | ---: |
| train | current_default | 369 | 178 | 768 |
| train | pattern_teacher_v0 | 369 | 217 | 677 |
| train | pattern_reboot_v0 | 369 | 64 | 1042 |
| train | phase_broad_v0 | 369 | 162 | 780 |
| validation | current_default | 112 | 57 | 223 |
| validation | pattern_teacher_v0 | 112 | 59 | 206 |
| validation | pattern_reboot_v0 | 112 | 19 | 337 |
| validation | phase_broad_v0 | 112 | 47 | 244 |
| holdout | current_default | 129 | 72 | 245 |
| holdout | pattern_teacher_v0 | 129 | 91 | 204 |
| holdout | pattern_reboot_v0 | 129 | 25 | 405 |
| holdout | phase_broad_v0 | 129 | 54 | 312 |

`phase_broad_v0` recovers a large amount of `pattern_reboot_v0`'s lost teacher
agreement, but it is still far behind `pattern_teacher_v0` on validation and
holdout.

## Exact Validation

Exact validation used the fixed train and heldout exact-label fixtures from the
classic-pattern experiment.

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --labels runs/classic-pattern-v0/exact/<train-or-heldout>_labels.jsonl \
  --baseline-config data/eval/current_default.eval \
  --candidates data/eval/pattern_teacher_v0.eval data/eval/pattern_reboot_v0.eval runs/pattern-training/phase-broad-v0/candidate.eval \
  --out runs/pattern-training/phase-broad-v0/matrix/exact-<split> \
  --search-depths 5,6,7 \
  --positions evaluation \
  --repetitions 3 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis \
  --allow-failures
```

Exact train:

| Config | Sign Agreements | Wrong Direction | High-Confidence Wrong | Exact-Best Top Group | Exact-Best Rank Sum |
| --- | ---: | ---: | ---: | ---: | ---: |
| current_default | 67 / 96 | 24 | 2 | 54 / 96 | 190 |
| pattern_teacher_v0 | 81 / 96 | 11 | 0 | 64 / 96 | 154 |
| pattern_reboot_v0 | 24 / 96 | 48 | 0 | 36 / 96 | 265 |
| phase_broad_v0 | 57 / 96 | 27 | 0 | 49 / 96 | 230 |

Exact heldout:

| Config | Sign Agreements | Wrong Direction | High-Confidence Wrong | Exact-Best Top Group | Exact-Best Rank Sum |
| --- | ---: | ---: | ---: | ---: | ---: |
| current_default | 79 / 96 | 17 | 1 | 47 / 96 | 207 |
| pattern_teacher_v0 | 79 / 96 | 17 | 0 | 50 / 96 | 195 |
| pattern_reboot_v0 | 22 / 96 | 55 | 0 | 39 / 96 | 267 |
| phase_broad_v0 | 58 / 96 | 26 | 0 | 36 / 96 | 263 |

The exact heldout result is negative: `phase_broad_v0` improves over
`pattern_reboot_v0` on sign agreement but loses badly to both `current_default`
and `pattern_teacher_v0`, and its exact-best rank sum is close to reboot.

## Search Validation

The exact matrix also ran fixed-position search smoke with iterative mode, TT
on, PVS on, aspiration on, and exact root disabled. Search rows are identical
between exact-train and exact-heldout matrices because the search suite is the
same fixed `evaluation` position set; the table below uses the heldout matrix.

| Config | Search Status | Nodes | Elapsed ms | Exact Root Searches | Best | Score | PV |
| --- | --- | ---: | ---: | ---: | --- | ---: | --- |
| current_default | passed | 613647 | 185.49 | 0 | d6 | 69 | d6 a5 b6 b5 a4 |
| pattern_teacher_v0 | passed | 565020 | 183.70 | 0 | f7 | 154 | f7 e7 c2 g8 f5 |
| pattern_reboot_v0 | passed | 668181 | 212.94 | 0 | b2 | 4 | b2 b3 a3 a1 f7 |
| phase_broad_v0 | passed | 534285 | 163.99 | 0 | d2 | 36 | d2 b2 c2 f3 a1 |

The generated candidate loaded successfully and changed search checksums versus
the baselines. Checksum differences are expected behavior-change evidence, not
a strength claim.

## Match Smoke

Small match smoke was run only as a sanity check. These are not Elo estimates.
Exact was disabled.

```sh
python3 tools/scripts/eval_config_search_validate.py \
  --base-config data/eval/current_default.eval \
  --candidate-configs runs/pattern-training/phase-broad-v0/candidate.eval \
  --build-dir build \
  --out runs/pattern-training/phase-broad-v0/search-match/current-depth5 \
  --run-search-bench \
  --run-match-smoke \
  --depths 5 \
  --positions smoke \
  --games 12 \
  --seed 20260601 \
  --openings data/openings/eval_regression_openings.txt \
  --exact-endgame-threshold 0 \
  --allow-failures
```

```sh
python3 tools/scripts/eval_config_search_validate.py \
  --base-config data/eval/pattern_teacher_v0.eval \
  --candidate-configs runs/pattern-training/phase-broad-v0/candidate.eval \
  --build-dir build \
  --out runs/pattern-training/phase-broad-v0/search-match/pattern-teacher-depth5 \
  --run-search-bench \
  --run-match-smoke \
  --depths 5 \
  --positions smoke \
  --games 12 \
  --seed 20260602 \
  --openings data/openings/eval_regression_openings.txt \
  --exact-endgame-threshold 0 \
  --allow-failures
```

| Matchup | Depth | Games | Candidate Wins | Opponent Wins | Draws | Avg Diff Candidate | Exact Roots |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| phase_broad_v0 vs current_default | 5 | 12 | 0 | 12 | 0 | -45.17 | 0 / 0 |
| phase_broad_v0 vs pattern_teacher_v0 | 5 | 12 | 0 | 11 | 1 | -40.50 | 0 / 0 |

Match smoke strongly supports rejection. The game count is small, but the signal
is not ambiguous enough to justify keeping this candidate as promising.

## Positive Evidence

- The phase-aware trainer emitted opening, midgame, and late broad TSVs under
  `runs/`.
- The generated `.eval` used `pattern_table.opening`, `pattern_table.midgame`,
  and `pattern_table.late`.
- The generated candidate loaded through the existing C++ `.eval` path.
- `phase_broad_v0` is a real improvement over the intentionally weak
  `pattern_reboot_v0` on train, validation, holdout, and fixed exact smoke.
- Search bench and match runner smoke completed without requiring external
  engines or source-controlled generated artifacts.

## Negative Evidence

- `phase_broad_v0` does not beat `pattern_teacher_v0` on validation teacher
  agreement, holdout teacher agreement, exact overlap, fixed exact train,
  fixed exact heldout, or match smoke.
- `phase_broad_v0` also loses to `current_default` on validation, holdout, fixed
  exact heldout, and match smoke.
- The learned midgame and late tables hit the configured per-family entry caps,
  so this sparse-count objective is probably saturating rather than learning a
  robust phase model.
- The raw teacher data is still under worktree-local `runs/`, not a shared
  dataset root, so future worktrees need the dataset migration step.

## Final Decision

Reject `phase_broad_v0` as a stronger evaluator candidate.

The result is useful as workflow validation: phase-aware broad pattern learning
can produce loadable local candidates, and the measurement path now catches a
bad candidate cleanly. The next learning step should focus on a better trainer
objective, stronger regularization, phase-specific data/manifest foundations,
and moving reusable teacher artifacts under `VIBE_OTHELLO_DATASET_ROOT`, not
promoting this table or simply adding more pattern families.

## Repository Validation

```sh
python3 -m py_compile tools/scripts/*.py tools/scripts/tests/*.py
python3 -m unittest discover tools/scripts/tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

Results: all passed on this branch.
