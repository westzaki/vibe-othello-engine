# 2026-06-01 Pattern Reboot v0

Status: experimental report, not current guidance.

This report records a clean pattern-only evaluator reboot baseline. It is not a
default-promotion proposal, not a C++ `EvaluationPreset`, not an Elo claim, not
proof of strength, and it does not change exact solver or search semantics.
The `data/eval/pattern_reboot_v0.eval`,
`data/eval/pattern_teacher_v0.eval`, and
`data/eval/patterns/pattern_teacher_v0.tsv` artifacts referenced below have
been removed from active source-controlled eval configs; the commands and paths
are retained as historical provenance only. Current work should use
`current_default.eval`, `ntest_pairwise_full_v2.eval`, or an explicit new
config.

## Decision

The recorded `pattern_reboot_v0` run was a clean pattern-only research
baseline. Do not use it as a current loadable preset or strength candidate.

The recorded config was intentionally weak and interpretable: it reused the
`pattern_teacher_v0` sparse table, set all scalar handcrafted feature weights to
zero, and left only `pattern_table` nonzero. The value was clarity for the next
pattern-learning line, not immediate playing strength.

## Metadata

- base git SHA: `be10b15755901ca98a0841b786f14b82b12b111b`
- raw output location: `runs/pattern-reboot-v0/`
- raw outputs committed: no
- generated teacher labels committed: no
- generated exact labels committed: no
- raw match JSONL committed: no
- local NTest paths committed: no

Compared configs and tables:

| Item | Path | sha256 |
| --- | --- | --- |
| current_default | `data/eval/current_default.eval` | `42d66384d725c55f46261624d78e4723846d63df1d2c1b41200429c4f4fa99cd` |
| pattern_teacher_v0 | `data/eval/pattern_teacher_v0.eval` | `2a058edb7fcea3b0ab0021d88a64c4e522574c93d0ba07f304bb93dca89bba60` |
| pattern_reboot_v0 | `data/eval/pattern_reboot_v0.eval` | `c73fd652b46394901972cfff4fcb0587b0e09a59e7416ae9099aebd98d12e55b` |
| classic_othello_v3_teacher_aggressive | `data/eval/classic_othello_v3_teacher_aggressive.eval` | `fb070b2ee2f0cc874d33e7f7b771a80676c82d7ae648c8a5e0ecbe4e77738777` |
| pattern_teacher_v0 table | `data/eval/patterns/pattern_teacher_v0.tsv` | `1c479d0bfea5e46955e0e48a4d2e3fb3b005b1a2e731bab3e43a16e5875bff44` |

## Why This Exists

Recent scalar-residual and sparse-pattern experiments became hard to interpret:
the table signal was mixed with many handcrafted scalar weights. That made it
unclear whether a future improvement came from pattern learning or from another
scalar residual adjustment.

`pattern_reboot_v0` resets the research surface:

- all scalar handcrafted feature weights are zero
- only the sparse loaded pattern table contributes to non-terminal evaluation
- default evaluator behavior is unchanged
- `pattern_teacher_v0` was the historical experimental pattern baseline for
  strength-oriented comparison in this report
- temporary strength regression is acceptable

The next research question should be pattern-table ownership, data, training
objective, regularization, and validation foundation rather than squeezing more
scalar residual weights.

## Config

`pattern_reboot_v0` uses:

```text
pattern_table=patterns/pattern_teacher_v0.tsv
opening.pattern_table=4
midgame.pattern_table=4
late.pattern_table=4
```

The phase cutoffs match `current_default` and `pattern_teacher_v0`:

```text
opening_max_occupied=20
midgame_max_occupied=44
```

All other feature weights are zero in all phases:

- `disc_difference`
- `mobility`
- `potential_mobility`
- `corner_occupancy`
- `corner_access`
- `x_square_danger`
- `frontier`
- `corner_local_2x3`
- `corner_2x3_pattern`
- `edge_stability_lite`
- `edge_8_pattern`

No table-weight sweep was committed. The all-phase weight `4` was selected as a
simple conservative reboot weight: lower than `pattern_teacher_v0`'s residual
table weight `10`, but large enough for the table to affect fixed-position
search.

## Teacher Dataset

No shared teacher dataset root was available in this worktree:

- `VIBE_OTHELLO_DATASET_ROOT` was not set
- `config/datasets.local.toml` was absent

No teacher or NTest run was generated for this PR. Teacher/mistake-mining
evidence is intentionally omitted rather than fabricated.

## Exact and Search Smoke

Exact labels were generated only from the committed evaluation diagnostic suite
and kept under `runs/`:

```sh
./build/othello_exact_label_dump \
  --input data/positions/evaluation/diagnostic_suite.txt \
  --output runs/pattern-reboot-v0/exact/evaluation_diagnostic_labels.jsonl \
  --max-empties 14 \
  --include-move-scores
```

Result:

| Input Positions | Labeled | Skipped Too Many Empties | Labels sha256 |
| ---: | ---: | ---: | --- |
| 8 | 1 | 7 | `4077e238bb3dce8d86f07333cd2020d155fd0218246eccab24e844dd553e70df` |

Candidate matrix command:

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --labels runs/pattern-reboot-v0/exact/evaluation_diagnostic_labels.jsonl \
  --baseline-config data/eval/current_default.eval \
  --candidates data/eval/pattern_teacher_v0.eval data/eval/pattern_reboot_v0.eval data/eval/classic_othello_v3_teacher_aggressive.eval \
  --out runs/pattern-reboot-v0/matrix/evaluation-diagnostic \
  --search-depths 5,6,7 \
  --positions evaluation \
  --repetitions 3 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis
```

Search used iterative mode, TT on, PVS on, aspiration on, and exact root
disabled.

Eval-vs-exact on the one labeled diagnostic position:

| Config | Sign Agreements | Wrong Direction | High-Confidence Wrong | Exact-Best Top Group | Exact-Best Rank Sum |
| --- | ---: | ---: | ---: | ---: | ---: |
| current_default | 1 / 1 | 0 | 0 | 1 / 1 | 1 |
| pattern_teacher_v0 | 1 / 1 | 0 | 0 | 1 / 1 | 1 |
| pattern_reboot_v0 | 0 / 1 | 1 | 0 | 1 / 1 | 1 |
| classic_othello_v3_teacher_aggressive | 1 / 1 | 0 | 0 | 1 / 1 | 1 |

The wrong-direction result for `pattern_reboot_v0` is expected negative
evidence. The top score group still contained an exact-best move because the
pattern-only table produced a broad tie on that late position.

Search smoke:

| Config | Nodes | Elapsed ms | Exact Root Searches | Best | Score | PV |
| --- | ---: | ---: | ---: | --- | ---: | --- |
| current_default | 613647 | 244.08 | 0 | d6 | 69 | d6 a5 b6 b5 a4 |
| pattern_teacher_v0 | 565020 | 283.16 | 0 | f7 | 154 | f7 e7 c2 g8 f5 |
| pattern_reboot_v0 | 668181 | 209.34 | 0 | b2 | 4 | b2 b3 a3 a1 f7 |
| classic_othello_v3_teacher_aggressive | 633834 | 181.01 | 0 | c2 | 135 | c2 d3 f6 b3 a4 |

Result and work checksums changed for every candidate versus
`current_default`, as expected for evaluator changes. This is behavior-change
evidence, not a regression label.

## Match Smoke

Exact was disabled for all match smoke runs. Games were side-swapped and used
`data/openings/eval_regression_openings.txt`. These are not Elo estimates and
not proof of strength.

```sh
python3 tools/scripts/run_match_experiment.py \
  --runner ./build/othello_match_runner \
  --summary-script tools/scripts/match_summary.py \
  --black search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/pattern_reboot_v0.eval \
  --white search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/current_default.eval \
  --games 12 \
  --swap-sides true \
  --seed 20260601 \
  --openings data/openings/eval_regression_openings.txt \
  --output runs/pattern-reboot-v0/match-smoke/reboot-vs-current-depth5.jsonl \
  --summary \
  --by-opening
```

```sh
python3 tools/scripts/run_match_experiment.py \
  --runner ./build/othello_match_runner \
  --summary-script tools/scripts/match_summary.py \
  --black search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/pattern_reboot_v0.eval \
  --white search:depth=5,tt=on,pvs=on,exact=off,eval_config=data/eval/pattern_teacher_v0.eval \
  --games 12 \
  --swap-sides true \
  --seed 20260602 \
  --openings data/openings/eval_regression_openings.txt \
  --output runs/pattern-reboot-v0/match-smoke/reboot-vs-pattern-teacher-depth5.jsonl \
  --summary \
  --by-opening
```

| Matchup | Depth | Games | Reboot Wins | Opponent Wins | Draws | Avg Diff Reboot | Exact Roots |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| reboot vs current_default | 5 | 12 | 0 | 12 | 0 | -51.17 | 0 / 0 |
| reboot vs pattern_teacher_v0 | 5 | 12 | 0 | 12 | 0 | -45.50 | 0 / 0 |

This is deliberately blunt negative evidence: a pattern-only table with no
learned full-board foundation is currently much weaker than both compared
baselines in this tiny smoke.

## Validation

```sh
python3 -m py_compile tools/scripts/*.py tools/scripts/tests/*.py
python3 -m unittest discover tools/scripts/tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

Results:

| Check | Result |
| --- | --- |
| Python compile | passed |
| Python unittest | passed, 217 tests |
| CMake configure | passed |
| CMake build | passed |
| CTest | passed, 266 / 266 tests |
| git diff --check | passed |

## Positive Evidence

- The recorded config parsed and loaded the sparse pattern table at the time.
- Tests assert `pattern_reboot_v0` has all scalar weights zero and only
  `pattern_table` nonzero.
- `current_default.eval` remains synchronized with `default_evaluation_config()`.
- Search validation ran with exact root disabled and no semantic search/exact
  changes.
- The config creates a clean pattern-only starting point for future learning.

## Negative Evidence

- `pattern_reboot_v0` failed the one available diagnostic exact sign check.
- The pattern-only score tied many root moves on the labeled late position.
- 12-game match smokes were 0-12 versus both `current_default` and
  `pattern_teacher_v0`.
- No shared teacher dataset was available, so teacher/mistake-mining evidence is
  absent.
- This is a baseline for research clarity, not a viable strength candidate.

## Next Direction

Do not try to rescue `pattern_reboot_v0` by adding scalar residual weights.
Useful follow-up work is:

- `PatternTableBundle` / table ownership / avoiding large table-by-value copying
- phase-specific pattern tables
- deterministic dataset/split/manifest builder
- stronger regularization and better trainer objective
- dense runtime table or future binary `.ptab` execution format while keeping
  TSV as source/review format

`pattern_teacher_v0` was the better historical pattern baseline for
strength-oriented comparisons in this report.
