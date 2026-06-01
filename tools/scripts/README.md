# Experimental Python Scripts

These scripts are lightweight orchestration and reporting helpers for local
experiments. The C++ tools remain the source of truth for engine execution and
JSONL generation, while Python is the canonical layer for dataset plumbing,
pattern-table experiments, match summaries, and reproducible evidence reports.

The scripts use only the Python standard library. They do not import the C++
engine directly.

## Status Definitions

- `current`: recommended for the current pattern-first workflow or shared PR
  evidence workflow.
- `legacy`: still useful for scalar/config diagnostics, compatibility checks,
  historical comparison, or focused debugging, but not the default path for new
  pattern-first work.
- `deprecated`: no longer recommended; removed here when a current workflow
  covers the use case and repository references are historical only.

Historical experiment reports may still mention removed scripts so old evidence
keeps its provenance. Do not copy those historical commands as current
instructions unless this README or a current workflow doc still recommends the
script.

## Inventory

| Script | Status | Purpose | Keep reason |
| --- | --- | --- | --- |
| `base_head_match_matrix.py` | current | Run external-process base/head match matrices. | Recommended for strength-changing code comparisons where in-process players would share one linked implementation. |
| `common.py` | current | Shared parsing, command, JSONL, slug, and report helpers. | Imported by many current and legacy scripts. |
| `dataset_paths.py` | current | Resolve `dataset:...` references and shared dataset roots. | Required by reusable teacher/exact dataset workflows. |
| `eval_candidate_matrix.py` | current | Gather comparable `.eval` candidate smoke evidence from labels and search bench. | Current replacement for ad hoc candidate validation wrappers; also registered in CTest as a dry-run smoke. |
| `eval_config_tuner.py` | legacy | Perturb fully expanded `.eval` scalar weights against exact-label JSONL. | Kept for debugging existing configs and because current diagnostics reuse its parser/metric helpers; not the default pattern-learning path. |
| `eval_experiment_matrix.py` | legacy | Run staged preset/config search and match matrices. | Kept for preset compatibility diagnostics and because `evidence.py --profile eval` can still call it. Prefer `.eval` candidates and `eval_candidate_matrix.py` for new pattern work. |
| `evidence.py` | current | Collect reproducible build/test/benchmark/match evidence for PRs. | Shared evidence workflow for reviewers; wraps C++ tools without making strength claims. |
| `exact_label_workflow.py` | current | Sample positions, dump exact labels, and optionally run eval-vs-exact analysis. | Current exact-label smoke helper for evaluation investigations. |
| `external_teacher_label_workflow.py` | current | Generate teacher-label JSONL from external engines. | Current teacher-label entrypoint and a dependency of `teacher_dataset_build.py`. |
| `extract_divergence_positions.py` | legacy | Extract first divergence boards from swap-side match JSONL. | Kept for focused regression diagnosis and historical baseline reproduction. |
| `forced_move_nboard_wrapper.py` | legacy | Force one diagnostic NBoard move when a target board is reached. | Kept for tactical/debug investigations; not part of engine behavior or normal experiments. |
| `match_summary.py` | current | Summarize C++ match-runner JSONL. | Shared by current evidence, match, and base/head workflows. |
| `pattern_teacher_v0_train.py` | current | Train sparse learned pattern tables and provide shared pattern extraction helpers. | Current pattern-table foundation and provenance path for `pattern_teacher_v0.tsv`; also used by phase/pairwise trainers and CTest. |
| `phase_pattern_table_train.py` | current | Train separate opening/midgame/late sparse pattern tables and local candidate configs. | Recommended pattern-first trainer after reusable dataset setup. |
| `regularized_pairwise_pattern_train.py` | current | Train phase-specific tables from teacher-vs-engine pairwise preferences. | Current pattern-first trainer foundation for broader regularized experiments. |
| `run_external_engine_once.py` | current | Probe one external-engine request through the canonical adapter CLI. | Current process/timeout/protocol smoke path for adapters. |
| `run_match_experiment.py` | current | Thin subprocess wrapper around `othello_match_runner` plus optional summary. | Current simple match-smoke wrapper used by recent pattern reports. |
| `teacher_dataset_build.py` | current | Build reusable position shards, manifests, splits, teacher labels, and exact overlap labels under a dataset root. | Recommended durable dataset-builder entrypoint for pattern-first work. |
| `teacher_label_mistake_mining.py` | current | Mine evaluator move-choice mistakes against validated teacher labels. | Current pattern diagnostics for teacher-vs-engine disagreement and vocabulary gaps. |

## Removed Deprecated Scripts

| Script | Status | Former purpose | Why it was safe to delete |
| --- | --- | --- | --- |
| `eval_config_validate.py` | deprecated | Validate generated scalar `.eval` candidates on held-out exact-label JSONL. | Replaced for current work by `eval_candidate_matrix.py --labels ...` and PR evidence reports; remaining repository mentions are historical experiment commands. |
| `eval_config_search_validate.py` | deprecated | Chain held-out scalar/config summaries into search-bench and match-smoke validation. | Replaced by `eval_candidate_matrix.py`, `run_match_experiment.py`, `base_head_match_matrix.py`, and `evidence.py`; remaining repository mentions are historical experiment commands. |
| `run_experiment_matrix.py` | deprecated | Run JSON-defined match-runner matrices from `tools/scripts/examples/search_ablation_smoke.json`. | Replaced by explicit `run_match_experiment.py`, `base_head_match_matrix.py`, and `evidence.py` workflows; it had no current docs outside this README and its dedicated test/example. |

The deleted scripts' dedicated tests were removed with them. The committed
historical experiment reports remain as evidence snapshots; raw/local reruns
should be rebuilt with the current workflows above.

## Pattern-First Evaluation Workflow

For new evaluation research, start with the current role definitions in
`data/eval/README.md` before choosing a script. `current_default.eval` is the
engine default and product-facing compatibility baseline, not the research
baseline. `pattern_teacher_v0.eval` is the retained experimental pattern
baseline, and `pattern_reboot_v0.eval`, when present, is the clean pattern-only
research baseline even though it is expected to be weak initially.

The preferred next path is pattern learning foundation work, not another round
of tiny scalar residual tuning. Use reusable teacher and exact artifacts through
`dataset:...` references and a dataset root as described in
`docs/datasets/README.md`; do not assume old worktree-local `runs/` output is
available.

Recommended order:

1. Build or locate teacher and exact datasets through `dataset:...` references.
2. Train or generate pattern tables under `runs/` or an external dataset root.
3. Load candidate tables through `.eval` configs.
4. Validate on teacher holdout, exact heldout, search validation, and match
   smoke.
5. Record a durable experiment report.
6. Keep raw labels, exact labels, match JSONL, benchmark logs, and local paths
   out of git.

Do not use `eval_config_tuner.py` as the default next step for pattern
learning. It remains useful for debugging existing `.eval` configs and scalar
weight interactions, but it is not the main strength path after the
pattern-first restart. Do not add one tiny pattern family at a time unless the
PR is explicitly part of a broader vocabulary and ablation plan. Do not create
source-controlled `.eval` files for rejected candidates. Do not treat smoke
evidence as Elo, proof of strength, or default-promotion evidence.

The next implementation direction should be `PatternTableBundle` / table
ownership, dense runtime tables or a future binary `.ptab` execution format,
phase-specific tables, deterministic dataset/split/manifest plumbing, stronger
trainer objectives and regularization, and a broad known-good Othello pattern
vocabulary with ablation.

Build reusable teacher/exact dataset artifacts under the shared dataset root:

```sh
python3 tools/scripts/teacher_dataset_build.py \
  --dataset-id fake-smoke \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --positions data/positions/evaluation/diagnostic_suite.txt \
  --shard-size 4 \
  --teacher-engine-name fake \
  --legal-validator build/othello_validate_move \
  --allow-failures \
  --teacher-engine-cmd -- python3 tools/scripts/external_engines/fake_engine.py --move d3
```

The builder writes position shards, deterministic split IDs, manifests,
dataset cards, and QC summaries. Teacher labels and exact overlap labels are
optional phases. The script is orchestration only: it uses existing C++ tools
for rule validation and exact labels, and it does not require external engines
in CI.

Train phase-specific broad pattern tables from reusable teacher labels:

```sh
python3 tools/scripts/phase_pattern_table_train.py \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --teacher-labels dataset:teacher/ntest-depth26-2027/labels/ntest26/train.jsonl \
  --exact-labels dataset:teacher/ntest-depth26-2027/exact-overlap/teacher2000_max12_labels.jsonl \
  --eval-config data/eval/pattern_reboot_v0.eval \
  --analyze-position build/othello_analyze_position \
  --out-dir runs/pattern-training/phase-broad-v0 \
  --table-name phase_broad_v0 \
  --families broad_all \
  --update-mode rank \
  --split train \
  --depth 1
```

This is the next pattern-first learning path after the dataset builder and
phase-specific table loading support. It writes `tables/opening.tsv`,
`tables/midgame.tsv`, `tables/late.tsv`, `candidate.eval`, `report.md`,
`summary.json`, and `phase_summary.tsv` under the requested `runs/` directory.
Generated TSVs and candidate `.eval` files are temporary experiment artifacts;
do not commit them or treat their smoke validation as strength proof.

Train regularized pairwise pattern tables when the goal is to improve the
pattern objective directly rather than tune scalar residual weights:

```sh
python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --teacher-labels dataset:teacher.ntest_depth26_2027:train \
  --exact-labels dataset:teacher.ntest_depth26_2027:exact_teacher2000,dataset:teacher.ntest_depth26_2027:exact_extra30 \
  --eval-config data/eval/pattern_reboot_v0.eval \
  --analyze-position build/othello_analyze_position \
  --out-dir runs/pattern-training/pairwise-v1 \
  --families broad_all \
  --split train \
  --loss logistic \
  --pair-mode exact-aware \
  --pair-weighting exact-boost \
  --exact-best-weight 2.0 \
  --teacher-weight 1.0 \
  --max-pairs-per-position 8 \
  --l2 0.01 \
  --epochs 5 \
  --learning-rate 0.05 \
  --max-abs-weight 8 \
  --output-scale 8 \
  --max-abs-output-weight 32767 \
  --seed 20260601
```

Pair modes:

- `best-vs-engine`: preserves the original trainer behavior. The teacher move
  is preferred over the current engine-selected move only.
- `best-vs-all`: compares the teacher move against all legal/root candidates.
  Use this when the single engine move is too narrow and the trainer needs more
  move-ordering signal per position.
- `rank-weighted`: compares the teacher move against lower-ranked root
  candidates and can weight those pairs by root rank or score margin. Use this
  when root candidate scores are available and tiny score ties should matter
  less than clear rank gaps.
- `exact-aware`: when exact labels exist, exact-best moves are preferred over
  non-exact-best legal/root candidates. Rows without exact labels fall back to
  teacher preferences. This is the preferred v1 objective after the pairwise-v0
  report because it reduces dependence on a single teacher-vs-engine pair.

Pair weighting:

- `uniform` keeps every generated pair at weight `1.0`.
- `rank-margin` modestly increases weight for larger root-rank gaps.
- `score-margin` modestly increases weight for larger root-score gaps.
- `exact-boost` uses `--exact-best-weight` for exact-best pairs and
  `--teacher-weight` for teacher fallback pairs.

Use `--max-pairs-per-position` to cap pair explosion and keep the objective
from simply overfitting harder. The trainer reports weighted train loss, train
pair accuracy, generated pair counts, pairs per position, exact-aware pairs,
phase/family entries, quantization stats, and saturation stats. High train
accuracy is a diagnostic only; it is not evidence of playing strength. Candidate
TSVs and `.eval` files remain generated artifacts under `runs/`, and follow-up
evidence should be recorded in a separate experiment report such as
`docs(eval): report pairwise pattern v1`.

## Current Evidence Workflows

Collect a reproducible PR evidence report:

```sh
python3 tools/scripts/evidence.py \
  --profile smoke \
  --build-dir build \
  --out runs/evidence/smoke-example
```

Evidence reports collect metadata, exact commands, raw command logs,
correctness checks, and benchmark or match outputs under `runs/evidence/...`.
They are review aids, not strength claims, Elo estimates, or default-promotion
recommendations. Raw logs under `runs/` should not be committed; durable
summaries belong under `docs/perf/baselines/`.

Sample positions, dump exact labels, and optionally compare one evaluator
against those labels:

```sh
python3 tools/scripts/exact_label_workflow.py \
  --build-dir build \
  --out runs/exact-label-workflow/smoke \
  --count 20 \
  --target-empties 8,10,12 \
  --seed 20260531 \
  --max-empties 14 \
  --eval-preset default \
  --analyze
```

This workflow calls the C++ sampler, exact-label dumper, and optional
eval-vs-exact analyzer. Random playout samples are reproducible smoke inputs,
not a representative training distribution. The workflow report records exact
commands, output paths, counts, caveats, and the absence of any strength claim
or default-promotion recommendation.

Run a simple evaluator candidate matrix when candidate `.eval` files already
exist and the goal is comparable smoke evidence rather than tuning:

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --labels runs/exact-label-workflow/heldout/labels.jsonl \
  --candidates runs/eval-config-tuner/smoke/configs/candidate_0001.eval \
  --out runs/eval-candidates/smoke
```

The matrix includes the default `data/eval/current_default.eval` baseline when
present, runs `othello_eval_vs_exact` only when labels are provided, always runs
the iterative TT/PVS/aspiration search-bench smoke profile, and writes
`report.md`, `summary.tsv`, per-candidate logs, and search-bench aggregate JSONL
under the requested output directory. It does not tune weights, make a strength
claim, or promote a default. Broader matches or base/head comparison are still
required before any strength claim.

Run the C++ match runner and summarize the result:

```sh
python3 tools/scripts/run_match_experiment.py \
  --runner ./build/othello_match_runner \
  --summary-script tools/scripts/match_summary.py \
  --black search:depth=4,tt=on,pvs=on,exact=off \
  --white search:depth=4,tt=off,pvs=off,exact=off \
  --games 4 \
  --swap-sides true \
  --seed 1 \
  --openings data/openings/smoke_openings.txt \
  --output build/matches/search4_options_from_python.jsonl \
  --summary \
  --by-opening
```

Run an external-process base/head matrix for a strength-changing PR:

```sh
python3 tools/scripts/base_head_match_matrix.py \
  --base-build /tmp/vibe-othello-base/build \
  --head-build build \
  --base-repo /tmp/vibe-othello-base \
  --head-repo . \
  --openings data/openings/smoke_openings.txt \
  --depths 4,8 \
  --games 12 \
  --seed 20260524 \
  --out runs/base-head/my-change-smoke
```

Use this for base/head code comparisons where in-process `search:` players would
share the same linked evaluator or search implementation. Raw matrix output
belongs under `runs/`; summarize meaningful snapshots in `docs/perf/baselines/`.

Summarize an existing match runner JSONL file:

```sh
python3 tools/scripts/match_summary.py \
  --input build/matches/search4_options.jsonl \
  --by-opening
```

## Legacy Diagnostics

Run a small diagnostic `.eval` config tuning experiment from exact-label JSONL:

```sh
python3 tools/scripts/eval_config_tuner.py \
  --labels runs/exact-label-workflow/smoke/labels.jsonl \
  --base-config data/eval/current_default.eval \
  --build-dir build \
  --out runs/eval-config-tuner/smoke \
  --rounds 1 \
  --step 1 \
  --max-candidates 64 \
  --seed 20260531
```

Use `exact_label_workflow.py` first when a labels file does not already exist.
The tuner reads a fully expanded `.eval` config, writes perturbed candidate
configs under `runs/eval-config-tuner/.../configs/`, runs
`othello_eval_vs_exact` for each candidate, and ranks candidates by a diagnostic
objective based on sign agreement, wrong-direction count, and high-confidence
wrong-direction count. Candidate configs are local experiment artifacts, not
active configs.

Run a staged evaluator-preset or explicit `.eval` matrix only when maintaining
legacy preset/config plumbing:

```sh
python3 tools/scripts/eval_experiment_matrix.py \
  --presets default,mobility_plus_smoke \
  --depths 4 \
  --games 6 \
  --openings data/openings/smoke_openings.txt \
  --seed 20260530 \
  --build-dir build \
  --out runs/eval/eval-preset-smoke \
  --smoke-run
```

This compares each non-default preset against `default` through the normal C++
tools. It is legacy preset/config plumbing, not the main path for
pattern-first learning and not strength evidence by itself. New experiments
should prefer `.eval` configs and current candidate/evidence workflows.

Extract first divergence positions from an existing swap-side base/head JSONL:

```sh
python3 tools/scripts/extract_divergence_positions.py \
  --input runs/base-head/my-change/depth-8/match.jsonl
```

Use those boards with `othello_analyze_position --root-candidates` when a matrix
regresses but the starting root positions do not explain the difference.

Force one diagnostic move when a target board is reached by an NBoard engine:

```sh
python3 tools/scripts/forced_move_nboard_wrapper.py \
  --target-board-file tools/scripts/fixtures/pr115_divergence_board.txt \
  --force-move a1 \
  -- ./build/othello_nboard_engine
```

This wrapper is diagnostic-only. It proxies the NBoard line protocol to a child
engine, tracks the current board by replaying `set game` commands, and returns
the forced move only when the tracked board exactly matches the target and the
move is legal. Otherwise it forwards commands unchanged.

## External Engine Helpers

Generate local external-engine teacher labels as JSONL:

```sh
python3 tools/scripts/external_teacher_label_workflow.py \
  --positions data/positions/evaluation/diagnostic_suite.txt \
  --out runs/teacher-labels/fake-smoke \
  --adapter one-shot \
  --engine-name fake \
  --engine-cmd -- python3 tools/scripts/external_engines/fake_engine.py --move d3
```

The workflow reads existing 9-line board position files, calls the selected
external-engine adapter once per position, and writes `labels.jsonl`,
`workflow.md`, and raw per-position logs under the requested output directory.
Teacher labels are reference-engine evidence, not exact truth, Elo, or a
default-promotion recommendation. Keep generated labels and raw logs under
`runs/`; do not commit them, and never commit external engine binaries or local
engine paths.

Probe the canonical external engine adapter CLI with the fake engine:

```sh
printf 'board text\n' | python3 tools/scripts/run_external_engine_once.py \
  --stdin-board \
  --adapter one-shot \
  --engine-cmd -- python3 tools/scripts/external_engines/fake_engine.py --move d3
```

External engine binaries, including NTest or Edax, are not stored in this
repository. The current adapter is only a process/timeout/error-handling
scaffold; engine-specific protocols belong under `external_engines/`.
Adapter options must appear before the `--engine-cmd --` boundary; everything
after that boundary is passed to the engine command.

Do not add a new per-engine probe CLI for each external engine. Keep
`run_external_engine_once.py` as the canonical one-move probe and add adapter
implementations under `external_engines/`.

## Tests

Python script tests run in CI. Run the same checks locally with:

```sh
python3 -m unittest discover tools/scripts/tests
```
