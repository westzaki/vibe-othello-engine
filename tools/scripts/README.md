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
- `canonical`: the default current entry point for new work in its area.
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
| `evidence.py` | current | Collect reproducible build/test/benchmark/match evidence for PRs. | Shared evidence workflow for reviewers; wraps C++ tools without making strength claims. |
| `exact_label_workflow.py` | current | Sample positions, dump exact labels, and optionally run eval-vs-exact analysis. | Current exact-label smoke helper for evaluation investigations. |
| `external_teacher_label_workflow.py` | current | Generate teacher-label JSONL from external engines. | Current teacher-label entrypoint and a dependency of `teacher_dataset_build.py`. |
| `match_summary.py` | current | Summarize C++ match-runner JSONL. | Shared by current evidence, match, and base/head workflows. |
| `ntest_teacher_smoke.py` | current | Run a local NTest teacher-label smoke and estimate 300K run feasibility. | Operational preflight before overnight NTest teacher dataset generation; does not make strength claims. |
| `regularized_pairwise_pattern_train.py` | current / canonical | Train phase-specific PatternOnly tables from exact-aware listwise teacher/exact labels. | Canonical current pattern trainer for new experiments; owns the shared analyzer cache/dedup/parallel workflow. |
| `teacher_dataset_build.py` | current | Build reusable position shards, manifests, splits, teacher labels, and exact overlap labels under a dataset root. | Recommended durable dataset-builder entrypoint for pattern-first work. |
| `teacher_label_mistake_mining.py` | current | Mine evaluator move-choice mistakes against validated teacher labels. | Current pattern diagnostics for teacher-vs-engine disagreement and vocabulary gaps. |

## Pattern-First Evaluation Workflow

For new evaluation research, start with the current role definitions in
`data/eval/README.md` before choosing a script and base evaluator.
`current_default.eval` is the engine default and product-facing compatibility
baseline. Historical pattern-teacher configs and tables were pruned from
`data/eval`; old reports may still mention them as provenance, but they are not
active trainer entry points, loadable presets, or defaults for new work.

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

Scalar perturbation tuning is no longer a current workflow after the
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

For new pattern experiments, use `regularized_pairwise_pattern_train.py` by
default. It is the canonical current trainer because it supports broad
phase-specific PatternOnly tables, regularization, exact-aware listwise target
construction, deterministic validation summaries, and the shared analyzer
cache/dedup/parallel workflow. Start from this trainer unless the task is
explicitly reproducing an older artifact or isolating a phase-table migration
detail.

The canonical PatternOnly trainer requires `--eval-config`; choose the base
evaluator intentionally for analyzer candidate discovery and diagnostics.
Current-engine improvement workflows should usually pass
`--eval-config data/eval/current_default.eval`. Base scalar scores are not part
of the trainer objective or generated candidate `.eval`.

Pattern trainers that invoke `othello_analyze_position` share the same optional
root-analysis cache flags: `--analysis-cache-dir`, `--analysis-cache-mode`, and
`--analysis-jobs`. Cache entries include the board, depth, eval config hash,
and analyzer binary hash so repeated or duplicate rows can reuse deterministic
root analysis without changing the table output format. Future analyzer
protocol work can avoid subprocess startup overhead further by adding
`othello_analyze_position --jsonl-in --jsonl-out`.

Train PatternOnly listwise pattern tables when the goal is to improve the
pattern objective directly rather than tune scalar residual weights:

```sh
python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --teacher-labels dataset:teacher.ntest_depth26_2027:train \
  --exact-labels dataset:teacher.ntest_depth26_2027:exact_teacher2000,dataset:teacher.ntest_depth26_2027:exact_extra30 \
  --eval-config data/eval/current_default.eval \
  --analyze-position build/othello_analyze_position \
  --out-dir runs/pattern-training/listwise-v1 \
  --families broad_all \
  --split train \
  --exact-score-temperature 4 \
  --exact-score-target-floor 0.0001 \
  --exact-score-near-best-window 8 \
  --l2 0.01 \
  --epochs 5 \
  --learning-rate 0.05 \
  --max-abs-weight 8 \
  --output-scale 8 \
  --max-abs-output-weight 32767 \
  --seed 20260601
```

When exact labels contain complete `move_scores`, the trainer uses a soft
exact-score target distribution. When only exact-best moves are available, it
uses a uniform target over those moves. Rows without exact labels fall back to
the teacher move. The trainer reports weighted listwise loss, selected-move
diagnostics, exact-score soft-target diagnostics, phase/family entries,
quantization stats, and saturation stats. High training accuracy is a
diagnostic only; it is not evidence of playing strength. Candidate TSVs and
`.eval` files remain generated artifacts under `runs/`, and follow-up evidence
should be recorded in a separate experiment report.

Historical reports and retained pattern-table metadata may still mention older
trainer entry points as timestamped provenance. Those scripts are removed from
active tooling; new work should use the canonical PatternOnly listwise trainer
instead of copying old command blocks.

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
  --candidates runs/pattern-training/phase-broad-v0/candidate.eval \
  --out runs/eval-candidates/smoke
```

The matrix includes the default `data/eval/current_default.eval` baseline when
present, runs `othello_eval_vs_exact` only when labels are provided, always runs
the iterative TT/PVS/aspiration search-bench smoke profile, and writes
`report.md`, `summary.tsv`, per-candidate logs, and search-bench aggregate JSONL
under the requested output directory. It does not tune weights, make a strength
claim, or promote a default. Broader matches or base/head comparison are still
required before any strength claim.

Run the C++ match runner directly and summarize the generated JSONL:

```sh
./build/othello_match_runner \
  --black search:depth=4,tt=on,pvs=on,exact=off \
  --white search:depth=4,tt=off,pvs=off,exact=off \
  --games 4 \
  --swap-sides true \
  --seed 1 \
  --openings data/openings/smoke_openings.txt \
  --output build/matches/search4_options.jsonl

python3 tools/scripts/match_summary.py \
  --input build/matches/search4_options.jsonl \
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

## Divergence Diagnostics

Extract first divergence positions from an existing swap-side base/head JSONL
with the current C++ replay workflow:

```sh
./build/othello_replay_game \
  --match-jsonl runs/base-head/my-change/depth-8/match.jsonl \
  --format jsonl
```

Use the emitted `board_text` values with `othello_analyze_position
--root-candidates` when a matrix regresses but the starting root positions do not
explain the difference.

Inspect root move alternatives for a focused board with the current analyzer:

```sh
./build/othello_analyze_position \
  --board-file data/positions/evaluation/corner_access_a1.txt \
  --depth 8 \
  --mode iterative \
  --tt on \
  --pvs on \
  --aspiration on \
  --exact-endgame-threshold 0 \
  --root-candidates
```

Use `othello_match_runner` or `base_head_match_matrix.py` for match-level
comparisons. Do not reintroduce protocol wrappers for one-off forced moves unless
there is a new current workflow that cannot be covered by the C++ tools.

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

External engine binaries, including NTest or Edax, are not stored in this
repository. Use the dataset and teacher-label workflows above for current
external-engine runs, and keep adapter implementations under `external_engines/`.

## Tests

Python script tests run in CI. Run the same checks locally with:

```sh
python3 -m unittest discover tools/scripts/tests
```
