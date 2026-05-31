# Experimental Python Scripts

These scripts are lightweight orchestration and reporting helpers for local
experiments. The C++ tools remain the source of truth for engine execution and
JSONL generation, while Python is the canonical layer for match summary and
reporting workflows.

The scripts use only the Python standard library. They do not import the C++
engine directly.

## Examples

Collect a reproducible PR evidence report:

```sh
python3 tools/scripts/evidence.py \
  --profile smoke \
  --build-dir build \
  --out runs/evidence/smoke-example
```

Evidence reports collect metadata, exact commands, raw command logs, correctness
checks, and benchmark or match outputs under `runs/evidence/...`. They are
review aids, not strength claims, Elo estimates, or default-promotion
recommendations. Raw logs under `runs/` should not be committed; durable
summaries belong under `docs/perf/baselines/`.

Profiles:

- `smoke`: configure/build, CTest, and a smoke search benchmark with
  `--exact-endgame-threshold 0`
- `pr`: `smoke` plus optional rule-core and self-play smoke evidence when the
  relevant executable and opening suite are available
- `eval`: evaluator/config evidence with preset or `.eval` candidates and an
  optional `eval_experiment_matrix.py` run
- `full`: conservative wrapper around existing cheap checks; broader suites are
  reported as skipped when they do not yet exist

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

The tuner objective is not Elo, not calibrated disc-margin error, and not a
default-promotion gate. It depends on the input label distribution; random
playout labels may not be representative. Follow-up validation should use
held-out exact labels, search bench, match runner or base/head comparison, and
external sanity checks when appropriate before creating any named candidate or
promotion PR.

Pass `--move-rank-analysis` when labels include `move_scores` and root
move-quality evidence should be copied into the tuner report and `summary.tsv`.
The objective remains the sign-agreement formula; missing `move_scores` are a
diagnostic caveat, not a workflow failure.

Validate generated `.eval` candidates on held-out exact-label JSONL:

```sh
python3 tools/scripts/eval_config_validate.py \
  --validation-labels runs/exact-label-workflow/heldout/labels.jsonl \
  --base-config data/eval/current_default.eval \
  --candidate-dir runs/eval-config-tuner/smoke/configs \
  --train-summary runs/eval-config-tuner/smoke/summary.tsv \
  --build-dir build \
  --out runs/eval-config-validation/smoke \
  --top 10
```

Generate validation labels separately from the labels used for tuning, for
example by running `exact_label_workflow.py` with a different seed or input
position source. The validator does not generate configs or tune weights; it
runs `othello_eval_vs_exact` for the base config and candidate configs, computes
the same diagnostic objective as the tuner, and writes
`validation_report.md`, per-config analyzer reports, logs, and `summary.tsv`
under `runs/eval-config-validation/...`.

Held-out validation is still diagnostic evidence only. It is not Elo, not a
strength claim, and not a default-promotion gate. The objective remains
label-distribution dependent, and validation labels may still be biased. After a
candidate looks promising on held-out exact labels, use search bench, match
runner or base/head comparison, and external sanity checks when appropriate
before making any promotion claim.

Use `--move-rank-analysis` with held-out labels that include `move_scores` to
record root move-quality counters beside the held-out sign-agreement objective.
The validator still ranks by the existing diagnostic objective.

Run search-bench and match-smoke validation for top `.eval` candidates:

```sh
python3 tools/scripts/eval_config_search_validate.py \
  --base-config data/eval/current_default.eval \
  --candidate-dir runs/eval-config-tuner/smoke/configs \
  --heldout-summary runs/eval-config-validation/smoke/summary.tsv \
  --build-dir build \
  --out runs/eval-config-search-validation/smoke \
  --top 3 \
  --run-search-bench \
  --run-match-smoke
```

Use the curated evaluation diagnostic suite by adding `--positions evaluation`
to the same command when the search-bench step should cover the committed
evaluation positions instead of the default `smoke` set.

Use this after tuning and held-out exact-label validation to gather smoke
evidence from `othello_search_bench` and small `othello_match_runner`
comparisons. Search validation uses `--exact-endgame-threshold 0` by default so
the search-bench step stays focused on midgame/evaluator behavior unless the
threshold is explicitly overridden. Match smoke is intentionally small and is
not an Elo estimate or strength claim.

The search/match validation report records exact commands, raw logs, checksum
differences when parsed, optional held-out objective context, and caveats under
`runs/eval-config-search-validation/...`. Candidate promotion still requires
broader search bench, match runner or base/head validation, and external sanity
when appropriate. Do not commit generated configs or raw workflow outputs.

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
under the requested output directory. The summary records config fingerprints,
result/work checksums, nodes, elapsed time, score kind, exact-root usage, and
candidate-vs-baseline deltas when comparable. It does not tune weights, make a
strength claim, or promote a default.

When `--move-rank-analysis` is passed and labels include `move_scores`, the
matrix also records eval-vs-exact root move-quality metrics in the report and
`summary.tsv`. These metrics are orchestration smoke evidence for the next
decision; broader matches or base/head comparison are still required before any
strength claim.

For evaluator config evidence:

```sh
python3 tools/scripts/evidence.py \
  --profile eval \
  --build-dir build \
  --out runs/evidence/eval-config-example \
  --eval-configs data/eval/default_edge_pattern_8_soft.eval \
  --reference-config data/eval/current_default.eval \
  --small-depths 5 \
  --small-games 4
```

Summarize an existing match runner JSONL file:

```sh
python3 tools/scripts/match_summary.py \
  --input build/matches/search4_options.jsonl \
  --by-opening
```

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

Run a small experiment matrix from JSON:

```sh
python3 tools/scripts/run_experiment_matrix.py \
  --config tools/scripts/examples/search_ablation_smoke.json \
  --dry-run

python3 tools/scripts/run_experiment_matrix.py \
  --config tools/scripts/examples/search_ablation_smoke.json
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

Run a local evaluator-preset wiring smoke:

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
tools. It is for reproducibility plumbing, not strength claims.

Run a staged evaluator-preset experiment against an explicit reference preset:

```sh
python3 tools/scripts/eval_experiment_matrix.py \
  --presets classic_corner_lite_v1,classic_edge_lite_v1,classic_features_lite_v1,classic_features_lite_aggressive,frontier_classic_features_lite_v1 \
  --reference-preset frontier_open2_mid2_late_plus1 \
  --small-depths 5,6 \
  --extended-depths 7,8 \
  --small-games 48 \
  --extended-games 96 \
  --promote-top 2 \
  --openings data/openings/eval_regression_openings.txt \
  --seed 20260530 \
  --build-dir build \
  --out runs/eval/evaluator-orchestrator-v2 \
  --positions suite \
  --by-position
```

The staged runner treats each candidate preset as player A and
`--reference-preset` as player B. Stage A runs search screening for all listed
presets, Stage B runs small candidate-vs-reference matches, and Stage C runs
extended matches only for promoted candidates. The legacy `--depths` and
`--games` options remain supported as aliases for `--small-depths` and
`--small-games`; use the explicit staged names for new experiments.

Raw command output, match JSONL, summaries, and generated reports belong under
`runs/`, not in committed docs. The generated report is a triage artifact: it is
not a strength claim, Elo estimate, or default-promotion recommendation.

Optionally add an NTest sanity hook when a local external-engine config is
available:

```sh
python3 tools/scripts/eval_experiment_matrix.py \
  --presets classic_features_lite_v1,frontier_classic_features_lite_v1 \
  --reference-preset frontier_open2_mid2_late_plus1 \
  --small-depths 5,6 \
  --extended-depths 7,8 \
  --small-games 48 \
  --extended-games 96 \
  --promote-top 1 \
  --openings data/openings/eval_regression_openings.txt \
  --seed 20260530 \
  --build-dir build \
  --out runs/eval/evaluator-orchestrator-v2-ntest \
  --run-ntest-sanity \
  --ntest-engine ntest8 \
  --engines runs/local-engines/engines.txt \
  --ntest-depth 6 \
  --ntest-games 12 \
  --ntest-openings data/openings/smoke_openings.txt
```

The NTest sanity hook is optional. If the external engine config is unavailable,
the report records a skip reason instead of failing the experiment runner.
External engine binaries are never vendored into this repository.

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
move is legal. Otherwise it forwards commands unchanged. Nested wrappers can be
used for multi-ply interventions, but no forced-move policy belongs in engine
core behavior.

Probe the canonical external engine adapter CLI with the fake engine:

```sh
printf 'board text\n' | python3 tools/scripts/run_external_engine_once.py \
  --stdin-board \
  --adapter one-shot \
  --engine-cmd -- python3 tools/scripts/external_engines/fake_engine.py --move d3
```

External engine binaries, including NTest or Edax, are not stored in this
repository. The current adapter is only a process/timeout/error-handling
scaffold; engine-specific protocols belong under `external_engines/`. Adapter
options must appear before the `--engine-cmd --` boundary; everything after that
boundary is passed to the engine command. The one-shot adapter validates move
tokens as `a1` through `h8` or `pass`, and the CLI can pass `--workdir PATH` and
repeated `--env KEY=VALUE` overrides to the engine process.

Do not add a new per-engine probe CLI for each external engine. Keep
`run_external_engine_once.py` as the canonical one-move probe and add adapter
implementations under `external_engines/`.

Probe the NTest proof-of-life adapter with a local wrapper or binary:

```sh
printf '(;GM[Othello]PC[NBoard]PB[test]PW[test]RE[?]TY[8]BO[8 ---------------------------O*------*O--------------------------- *];)\n' | \
python3 tools/scripts/run_external_engine_once.py \
  --stdin-board \
  --adapter ntest \
  --protocol nboard \
  --workdir /path/to/ntest/build \
  --timeout-ms 10000 \
  --engine-cmd -- /path/to/ntest/build/ntest x
```

NTest-specific support defaults to the NBoard external viewer protocol exposed
by NTest's `x` mode: the adapter sends `nboard 2`, `set depth`, `set game`,
`go`, and `quit`, then reads the `=== MOVE` response. Use
`--adapter ntest --protocol one-shot` for local wrapper commands that accept
board text on stdin and print a move such as `d3` or `pass` on stdout. CI does
not require NTest; it exercises parser behavior and process handling with the
fake engine. Full-game external-engine matches are intentionally left for a
later PR.

One-shot process adapters live in `external_engines/one_shot.py`; NTest-specific
proof-of-life handling lives in `external_engines/ntest.py`;
persistent/stateful protocol adapters should grow separately under
`external_engines/persistent.py`.

Python script tests run in CI. Run the same checks locally with:

```sh
python3 -m unittest discover tools/scripts/tests
```
