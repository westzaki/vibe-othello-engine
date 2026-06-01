# 2026-06-01 Pairwise Pattern v0

Status: rejected as a baseline candidate.
Not default promotion.

## Summary

This report tested the PR191 regularized pairwise pattern trainer on the
reusable NTest depth-26 teacher dataset from PR190. The generated
`pairwise-v0` candidate is useful negative evidence: the trainer produced a
C++-loadable candidate, and the candidate improved some exact-label metrics
over the deliberately weak `pattern_reboot_v0` baseline, but it did not reach
`pattern_teacher_v0` or `current_default` on teacher agreement, exact heldout
metrics, or match smoke.

This is not a strength claim and not a default-promotion proposal. The generated
candidate and tables remain under `runs/` and are not committed.

## Decision

Trainer works, candidate not strong enough yet.

Do not keep this generated candidate as a durable baseline. The result supports
follow-up diagnostics or objective improvements for pairwise pattern training,
not a candidate/scorecard PR and not default promotion.

## Context

PR190 migrated the reusable NTest depth-26 teacher artifacts to the shared
dataset root and catalog references. PR191 added
`tools/scripts/regularized_pairwise_pattern_train.py` as a trainer foundation:
it trains floating-point internal weights, writes C++ loader-compatible integer
pattern TSV values, and resolves `dataset:` references through the shared
`dataset_paths.py` helpers.

This report tests one `pairwise-v0` output from that trainer. It does not add a
C++ `EvaluationPreset`, does not change `current_default.eval`, and does not
change search or exact solver semantics.

## Inputs

- git SHA: `98618d944962bbff248c7d278fba1f32e5cc15cc`
- build type: `Release`
- candidate output directory: `runs/pattern-training/pairwise-v0`
- trainer base eval config: `data/eval/pattern_reboot_v0.eval`
- compared eval configs:
  - `data/eval/current_default.eval`
  - `data/eval/pattern_teacher_v0.eval`
  - `data/eval/pattern_reboot_v0.eval`
  - `runs/pattern-training/pairwise-v0/candidate.eval`
- teacher dataset refs:
  - `dataset:teacher.ntest_depth26_2027:train`
  - `dataset:teacher.ntest_depth26_2027:validation`
  - `dataset:teacher.ntest_depth26_2027:holdout`
- exact dataset refs:
  - `dataset:teacher.ntest_depth26_2027:exact_teacher2000`
  - `dataset:teacher.ntest_depth26_2027:exact_extra30`
  - `dataset:teacher.ntest_depth26_2027:exact_train`
  - `dataset:teacher.ntest_depth26_2027:exact_heldout`

The teacher split row counts observed by the tools were 1234 train, 384
validation, and 409 holdout rows, for 2027 total teacher rows. No local absolute
dataset-root path is required in this report.

## Candidate Generation

Command:

```sh
python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --teacher-labels dataset:teacher.ntest_depth26_2027:train \
  --exact-labels dataset:teacher.ntest_depth26_2027:exact_teacher2000,dataset:teacher.ntest_depth26_2027:exact_extra30 \
  --eval-config data/eval/pattern_reboot_v0.eval \
  --analyze-position build/othello_analyze_position \
  --out-dir runs/pattern-training/pairwise-v0 \
  --families broad_all \
  --split train \
  --loss logistic \
  --l2 0.001 \
  --epochs 5 \
  --learning-rate 0.1 \
  --max-abs-weight 8 \
  --output-scale 8 \
  --max-abs-output-weight 32767 \
  --seed 20260601
```

Result:

- output directory: `runs/pattern-training/pairwise-v0`
- generated candidate: `runs/pattern-training/pairwise-v0/candidate.eval`
- generated tables:
  - `runs/pattern-training/pairwise-v0/tables/opening.tsv`
  - `runs/pattern-training/pairwise-v0/tables/midgame.tsv`
  - `runs/pattern-training/pairwise-v0/tables/late.tsv`
- generated summary: `runs/pattern-training/pairwise-v0/summary.json`
- generated report: `runs/pattern-training/pairwise-v0/report.md`
- train-split diagnostics: `runs/pattern-training/pairwise-v0/validation.tsv`

Training rows and pairs:

| Metric | Count |
| --- | ---: |
| teacher rows | 1234 |
| accepted teacher rows | 1234 |
| split skipped | 0 |
| unusable teacher rows | 0 |
| teacher-exact disagreements skipped | 0 |
| already agreed | 126 |
| preference pairs | 1108 |
| opening pairs | 115 |
| midgame pairs | 572 |
| late pairs | 421 |

Output scale and clamp:

- internal max absolute weight clamp: `8`
- output scale: `8`
- max absolute output clamp: `32767`
- observed max absolute output weight: `15`

No saturation was observed. Quantization did produce some zero entries, but the
nonzero ratios were not obviously empty.

## Candidate Load Smoke

Command:

```sh
build/othello_analyze_position \
  --board-file tools/scripts/fixtures/pr115_divergence_board.txt \
  --depth 1 \
  --exact-endgame-threshold 0 \
  --eval-config runs/pattern-training/pairwise-v0/candidate.eval \
  --root-candidates > runs/pattern-training/pairwise-v0/load-smoke.txt
```

Result: passed. The generated `candidate.eval` loaded successfully through the
C++ evaluator path.

Key output:

- `eval_config: runs/pattern-training/pairwise-v0/candidate.eval`
- `best_move: b3`
- `score_kind: heuristic`
- `used_exact_endgame: no`
- root `pattern_table: 91`
- root `pattern_table_weight: 4`
- root `pattern_table_score: 364`

## Training Diagnostics

These are train-split diagnostics from `--split train`. They are not held-out
validation proof.

Final training metrics:

| Loss | Accuracy | Avg Margin | Pairs |
| ---: | ---: | ---: | ---: |
| 0.2169999571 | 1.0000 | 2.4648779483 | 1108 |

Entries and nonzero integer TSV values:

| Phase | Family | Entries | Nonzero | Max Abs Output |
| --- | --- | ---: | ---: | ---: |
| opening | corner_3x3 | 68 | 56 | 10 |
| opening | diagonal_8 | 83 | 67 | 10 |
| opening | edge_8 | 29 | 26 | 10 |
| opening | edge_x_10 | 49 | 42 | 10 |
| opening | inner_row_8 | 104 | 76 | 10 |
| midgame | corner_3x3 | 889 | 748 | 15 |
| midgame | diagonal_8 | 471 | 380 | 15 |
| midgame | edge_8 | 668 | 539 | 15 |
| midgame | edge_x_10 | 1222 | 985 | 15 |
| midgame | inner_row_8 | 1340 | 1022 | 15 |
| late | corner_3x3 | 1127 | 983 | 11 |
| late | diagonal_8 | 599 | 508 | 11 |
| late | edge_8 | 947 | 827 | 11 |
| late | edge_x_10 | 1496 | 1325 | 11 |
| late | inner_row_8 | 1192 | 1019 | 11 |

Phase totals:

| Phase | Entries | Nonzero | Max Abs Float Weight | Max Abs Output |
| --- | ---: | ---: | ---: | ---: |
| opening | 333 | 267 | 1.2088300732 | 10 |
| midgame | 4590 | 3674 | 1.8168741265 | 15 |
| late | 5361 | 4662 | 1.3917107806 | 11 |

## Held-out / Exact / Search Evidence

### Teacher Validation

Command:

```sh
python3 tools/scripts/teacher_label_mistake_mining.py \
  --teacher-labels dataset:teacher.ntest_depth26_2027:validation \
  --config current_default=data/eval/current_default.eval \
  --config pattern_teacher_v0=data/eval/pattern_teacher_v0.eval \
  --config pattern_reboot_v0=data/eval/pattern_reboot_v0.eval \
  --config pairwise_v0=runs/pattern-training/pairwise-v0/candidate.eval \
  --out runs/pattern-training/pairwise-v0/teacher-validation \
  --build-dir build \
  --depth 1 \
  --exact-endgame-threshold 0
```

Result summary:

| Config | Rows | Teacher Agreements | Teacher Rank Sum |
| --- | ---: | ---: | ---: |
| current_default | 384 | 164 | 940 |
| pattern_teacher_v0 | 384 | 175 | 899 |
| pattern_reboot_v0 | 384 | 41 | 1340 |
| pairwise_v0 | 384 | 48 | 1885 |

`pairwise_v0` improved teacher agreement over `pattern_reboot_v0` by 7 rows,
but its rank sum regressed and it remained far below `pattern_teacher_v0` and
`current_default`.

Output path: `runs/pattern-training/pairwise-v0/teacher-validation`.

### Teacher Holdout

Command:

```sh
python3 tools/scripts/teacher_label_mistake_mining.py \
  --teacher-labels dataset:teacher.ntest_depth26_2027:holdout \
  --config current_default=data/eval/current_default.eval \
  --config pattern_teacher_v0=data/eval/pattern_teacher_v0.eval \
  --config pattern_reboot_v0=data/eval/pattern_reboot_v0.eval \
  --config pairwise_v0=runs/pattern-training/pairwise-v0/candidate.eval \
  --out runs/pattern-training/pairwise-v0/teacher-holdout \
  --build-dir build \
  --depth 1 \
  --exact-endgame-threshold 0
```

Result summary:

| Config | Rows | Teacher Agreements | Teacher Rank Sum |
| --- | ---: | ---: | ---: |
| current_default | 409 | 184 | 994 |
| pattern_teacher_v0 | 409 | 188 | 950 |
| pattern_reboot_v0 | 409 | 40 | 1560 |
| pairwise_v0 | 409 | 53 | 2089 |

`pairwise_v0` again improved agreement over `pattern_reboot_v0`, but it was
not competitive with `pattern_teacher_v0` or `current_default`, and the rank
sum was worse than `pattern_reboot_v0`.

Output path: `runs/pattern-training/pairwise-v0/teacher-holdout`.

### Exact Train

Command used `dataset:teacher.ntest_depth26_2027:exact_train`, resolved by the
shared dataset helper, and wrote to
`runs/pattern-training/pairwise-v0/exact-train-matrix`.

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --labels "$(python3 -c 'from pathlib import Path; import sys; sys.path.insert(0, str(Path("tools/scripts").resolve())); from dataset_paths import resolve_dataset_reference; print(resolve_dataset_reference("dataset:teacher.ntest_depth26_2027:exact_train"))')" \
  --baseline-config data/eval/pattern_reboot_v0.eval \
  --candidates data/eval/pattern_teacher_v0.eval data/eval/current_default.eval runs/pattern-training/pairwise-v0/candidate.eval \
  --out runs/pattern-training/pairwise-v0/exact-train-matrix \
  --search-depths 5 \
  --positions evaluation \
  --repetitions 1 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis
```

Result summary:

| Config | Exact Rows | Sign Agree | Wrong Direction | High Conf Wrong | Exact-Best Top | Exact-Best Rank Sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| pattern_reboot_v0 | 96 | 24 | 48 | 0 | 36 | 265 |
| pattern_teacher_v0 | 96 | 81 | 11 | 0 | 64 | 154 |
| current_default | 96 | 67 | 24 | 2 | 54 | 190 |
| pairwise_v0 | 96 | 58 | 23 | 0 | 51 | 230 |

`pairwise_v0` improved over `pattern_reboot_v0` but did not reach
`pattern_teacher_v0` or `current_default`.

### Exact Heldout

Command used `dataset:teacher.ntest_depth26_2027:exact_heldout`, resolved by
the shared dataset helper, and wrote to
`runs/pattern-training/pairwise-v0/exact-heldout-matrix`.

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --labels "$(python3 -c 'from pathlib import Path; import sys; sys.path.insert(0, str(Path("tools/scripts").resolve())); from dataset_paths import resolve_dataset_reference; print(resolve_dataset_reference("dataset:teacher.ntest_depth26_2027:exact_heldout"))')" \
  --baseline-config data/eval/pattern_reboot_v0.eval \
  --candidates data/eval/pattern_teacher_v0.eval data/eval/current_default.eval runs/pattern-training/pairwise-v0/candidate.eval \
  --out runs/pattern-training/pairwise-v0/exact-heldout-matrix \
  --search-depths 5 \
  --positions evaluation \
  --repetitions 1 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis
```

Result summary:

| Config | Exact Rows | Sign Agree | Wrong Direction | High Conf Wrong | Exact-Best Top | Exact-Best Rank Sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| pattern_reboot_v0 | 96 | 22 | 55 | 0 | 39 | 267 |
| pattern_teacher_v0 | 96 | 79 | 17 | 0 | 50 | 195 |
| current_default | 96 | 79 | 17 | 1 | 47 | 207 |
| pairwise_v0 | 96 | 62 | 29 | 0 | 40 | 239 |

Heldout exact evidence repeats the same pattern: better than
`pattern_reboot_v0`, weaker than `pattern_teacher_v0` and `current_default`.

### Exact Teacher2000 And Extra30

The exact-overlap refs supplied to the trainer were also checked with the same
matrix tool. These are sanity checks, not held-out evidence for the primary
train split.

`dataset:teacher.ntest_depth26_2027:exact_teacher2000`:

| Config | Exact Rows | Sign Agree | Wrong Direction | High Conf Wrong | Exact-Best Top | Exact-Best Rank Sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| pattern_reboot_v0 | 602 | 165 | 304 | 0 | 230 | 1749 |
| pattern_teacher_v0 | 602 | 494 | 82 | 6 | 368 | 1072 |
| current_default | 602 | 448 | 128 | 7 | 306 | 1220 |
| pairwise_v0 | 602 | 336 | 202 | 0 | 366 | 1140 |

`dataset:teacher.ntest_depth26_2027:exact_extra30`:

| Config | Exact Rows | Sign Agree | Wrong Direction | High Conf Wrong | Exact-Best Top | Exact-Best Rank Sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| pattern_reboot_v0 | 10 | 3 | 4 | 0 | 1 | 37 |
| pattern_teacher_v0 | 10 | 8 | 1 | 0 | 4 | 17 |
| current_default | 10 | 7 | 2 | 0 | 5 | 18 |
| pairwise_v0 | 10 | 6 | 3 | 0 | 6 | 19 |

The candidate is mixed on these overlap checks: stronger than reboot, sometimes
close on exact-best top count, but still behind the retained stronger baselines
on the broader metrics.

Output paths:

- `runs/pattern-training/pairwise-v0/exact-teacher2000-matrix`
- `runs/pattern-training/pairwise-v0/exact-extra30-matrix`

### Search Smoke

Search smoke was collected through the `eval_candidate_matrix.py` search-bench
step. Depth was 5, exact endgame was disabled at the root, and generated output
stayed under `runs/pattern-training/pairwise-v0`.

Evaluation positions:

| Config | Depth | Nodes | Best Move | Score |
| --- | ---: | ---: | --- | ---: |
| pattern_reboot_v0 | 5 | 17237 | b2 | 4 |
| pattern_teacher_v0 | 5 | 16050 | f7 | 154 |
| current_default | 5 | 16522 | d6 | 69 |
| pairwise_v0 | 5 | 28566 | b2 | -172 |

Smoke positions:

| Config | Depth | Nodes | Best Move | Score |
| --- | ---: | ---: | --- | ---: |
| pattern_reboot_v0 | 5 | 1426 | d3 | 0 |
| pattern_teacher_v0 | 5 | 1351 | d3 | 0 |
| current_default | 5 | 1544 | d3 | -15 |
| pairwise_v0 | 5 | 2390 | e6 | -424 |

The candidate changed search behavior and increased node counts substantially
against `pattern_reboot_v0` in these fixed smokes. This is not a regression
label by itself, but it is another reason not to promote the candidate.

### Match Smoke

Match smoke used the existing C++ match runner through
`tools/scripts/run_match_experiment.py`. Exact was disabled, games were
side-swapped, depth was 5, and openings were
`data/openings/smoke_openings.txt`. These are small smoke checks only, not Elo.

Commands wrote to:

- `runs/pattern-training/pairwise-v0/match-smoke/pairwise-vs-reboot-depth5.jsonl`
- `runs/pattern-training/pairwise-v0/match-smoke/pairwise-vs-teacher-depth5.jsonl`
- `runs/pattern-training/pairwise-v0/match-smoke/pairwise-vs-current-depth5.jsonl`

Result summary:

| Matchup | Depth | Games | Pairwise Wins | Opponent Wins | Draws | Avg Diff Pairwise |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| pairwise_v0 vs pattern_reboot_v0 | 5 | 12 | 10 | 2 | 0 | 12.33 |
| pairwise_v0 vs pattern_teacher_v0 | 5 | 12 | 0 | 12 | 0 | -50.33 |
| pairwise_v0 vs current_default | 5 | 12 | 0 | 12 | 0 | -49.33 |

This match smoke reinforces the decision: `pairwise_v0` is better than the
deliberately weak reboot baseline in this tiny smoke, but it is not competitive
with the retained baselines.

## Interpretation

The candidate learned the train-split pairwise objective cleanly, but that did
not translate into a useful evaluator candidate. The strongest positive signal
is that exact train and exact heldout metrics improved over
`pattern_reboot_v0`. The strongest negative signals are:

- teacher validation and holdout rank sums regressed versus `pattern_reboot_v0`
- exact heldout remains well below `pattern_teacher_v0` and `current_default`
- depth-5 search smoke changes best moves and increases node counts
- match smoke loses 0-12 against both `pattern_teacher_v0` and
  `current_default`

Main risks and limitations:

- overfitting to the 1234 train rows is likely because train diagnostics reached
  100% preference accuracy
- exact-disagreement filtering did not skip rows in this run, so overlap and
  filtering behavior should be inspected before interpreting exact-conditioned
  training too strongly
- output-scale sensitivity was not swept; this run used only `output-scale=8`
  because the generated tables were not empty or saturated
- teacher agreement, exact move ranking, search behavior, and match smoke can
  disagree; no single metric here is a strength proof
- the 12-game match smokes are not Elo estimates

## Conclusion

- promote to default? no
- commit generated candidate? no
- keep this generated candidate for follow-up? no
- keep the trainer foundation? yes
- next recommended PR: diagnostics/objective improvement for pairwise pattern
  training, especially regularization, calibration, and exact/teacher objective
  alignment before another candidate report

## Commands Run

Repository and trainer help:

```sh
python3 tools/scripts/regularized_pairwise_pattern_train.py --help
```

Build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Candidate generation:

```sh
python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --teacher-labels dataset:teacher.ntest_depth26_2027:train \
  --exact-labels dataset:teacher.ntest_depth26_2027:exact_teacher2000,dataset:teacher.ntest_depth26_2027:exact_extra30 \
  --eval-config data/eval/pattern_reboot_v0.eval \
  --analyze-position build/othello_analyze_position \
  --out-dir runs/pattern-training/pairwise-v0 \
  --families broad_all \
  --split train \
  --loss logistic \
  --l2 0.001 \
  --epochs 5 \
  --learning-rate 0.1 \
  --max-abs-weight 8 \
  --output-scale 8 \
  --max-abs-output-weight 32767 \
  --seed 20260601
```

Candidate load smoke:

```sh
build/othello_analyze_position \
  --board-file tools/scripts/fixtures/pr115_divergence_board.txt \
  --depth 1 \
  --exact-endgame-threshold 0 \
  --eval-config runs/pattern-training/pairwise-v0/candidate.eval \
  --root-candidates > runs/pattern-training/pairwise-v0/load-smoke.txt
```

Teacher validation and holdout:

```sh
python3 tools/scripts/teacher_label_mistake_mining.py \
  --teacher-labels dataset:teacher.ntest_depth26_2027:validation \
  --config current_default=data/eval/current_default.eval \
  --config pattern_teacher_v0=data/eval/pattern_teacher_v0.eval \
  --config pattern_reboot_v0=data/eval/pattern_reboot_v0.eval \
  --config pairwise_v0=runs/pattern-training/pairwise-v0/candidate.eval \
  --out runs/pattern-training/pairwise-v0/teacher-validation \
  --build-dir build \
  --depth 1 \
  --exact-endgame-threshold 0

python3 tools/scripts/teacher_label_mistake_mining.py \
  --teacher-labels dataset:teacher.ntest_depth26_2027:holdout \
  --config current_default=data/eval/current_default.eval \
  --config pattern_teacher_v0=data/eval/pattern_teacher_v0.eval \
  --config pattern_reboot_v0=data/eval/pattern_reboot_v0.eval \
  --config pairwise_v0=runs/pattern-training/pairwise-v0/candidate.eval \
  --out runs/pattern-training/pairwise-v0/teacher-holdout \
  --build-dir build \
  --depth 1 \
  --exact-endgame-threshold 0
```

Exact matrices:

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --labels "$(python3 -c 'from pathlib import Path; import sys; sys.path.insert(0, str(Path("tools/scripts").resolve())); from dataset_paths import resolve_dataset_reference; print(resolve_dataset_reference("dataset:teacher.ntest_depth26_2027:exact_train"))')" \
  --baseline-config data/eval/pattern_reboot_v0.eval \
  --candidates data/eval/pattern_teacher_v0.eval data/eval/current_default.eval runs/pattern-training/pairwise-v0/candidate.eval \
  --out runs/pattern-training/pairwise-v0/exact-train-matrix \
  --search-depths 5 \
  --positions evaluation \
  --repetitions 1 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis

python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --labels "$(python3 -c 'from pathlib import Path; import sys; sys.path.insert(0, str(Path("tools/scripts").resolve())); from dataset_paths import resolve_dataset_reference; print(resolve_dataset_reference("dataset:teacher.ntest_depth26_2027:exact_heldout"))')" \
  --baseline-config data/eval/pattern_reboot_v0.eval \
  --candidates data/eval/pattern_teacher_v0.eval data/eval/current_default.eval runs/pattern-training/pairwise-v0/candidate.eval \
  --out runs/pattern-training/pairwise-v0/exact-heldout-matrix \
  --search-depths 5 \
  --positions evaluation \
  --repetitions 1 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis

python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --labels "$(python3 -c 'from pathlib import Path; import sys; sys.path.insert(0, str(Path("tools/scripts").resolve())); from dataset_paths import resolve_dataset_reference; print(resolve_dataset_reference("dataset:teacher.ntest_depth26_2027:exact_teacher2000"))')" \
  --baseline-config data/eval/pattern_reboot_v0.eval \
  --candidates data/eval/pattern_teacher_v0.eval data/eval/current_default.eval runs/pattern-training/pairwise-v0/candidate.eval \
  --out runs/pattern-training/pairwise-v0/exact-teacher2000-matrix \
  --search-depths 5 \
  --positions smoke \
  --repetitions 1 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis

python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --labels "$(python3 -c 'from pathlib import Path; import sys; sys.path.insert(0, str(Path("tools/scripts").resolve())); from dataset_paths import resolve_dataset_reference; print(resolve_dataset_reference("dataset:teacher.ntest_depth26_2027:exact_extra30"))')" \
  --baseline-config data/eval/pattern_reboot_v0.eval \
  --candidates data/eval/pattern_teacher_v0.eval data/eval/current_default.eval runs/pattern-training/pairwise-v0/candidate.eval \
  --out runs/pattern-training/pairwise-v0/exact-extra30-matrix \
  --search-depths 5 \
  --positions smoke \
  --repetitions 1 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis
```

Match smoke:

```sh
python3 tools/scripts/run_match_experiment.py \
  --runner ./build/othello_match_runner \
  --summary-script tools/scripts/match_summary.py \
  --black search:depth=5,tt=on,pvs=on,exact=off,eval_config=runs/pattern-training/pairwise-v0/candidate.eval \
  --white search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/pattern_reboot_v0.eval \
  --games 12 \
  --swap-sides true \
  --seed 20260601 \
  --openings data/openings/smoke_openings.txt \
  --output runs/pattern-training/pairwise-v0/match-smoke/pairwise-vs-reboot-depth5.jsonl \
  --summary \
  --by-opening

python3 tools/scripts/run_match_experiment.py \
  --runner ./build/othello_match_runner \
  --summary-script tools/scripts/match_summary.py \
  --black search:depth=5,tt=on,pvs=on,exact=off,eval_config=runs/pattern-training/pairwise-v0/candidate.eval \
  --white search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/pattern_teacher_v0.eval \
  --games 12 \
  --swap-sides true \
  --seed 20260602 \
  --openings data/openings/smoke_openings.txt \
  --output runs/pattern-training/pairwise-v0/match-smoke/pairwise-vs-teacher-depth5.jsonl \
  --summary \
  --by-opening

python3 tools/scripts/run_match_experiment.py \
  --runner ./build/othello_match_runner \
  --summary-script tools/scripts/match_summary.py \
  --black search:depth=5,tt=on,pvs=on,exact=off,eval_config=runs/pattern-training/pairwise-v0/candidate.eval \
  --white search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/current_default.eval \
  --games 12 \
  --swap-sides true \
  --seed 20260603 \
  --openings data/openings/smoke_openings.txt \
  --output runs/pattern-training/pairwise-v0/match-smoke/pairwise-vs-current-depth5.jsonl \
  --summary \
  --by-opening
```

## Validation

Repository validation:

```sh
python3 -m py_compile tools/scripts/*.py tools/scripts/tests/*.py
python3 -m unittest discover tools/scripts/tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

Result: passed locally.
