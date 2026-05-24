# Experimental Python Scripts

These scripts are lightweight orchestration and reporting helpers for local
experiments. The C++ tools remain the source of truth for engine execution and
JSONL generation, while Python is the canonical layer for match summary and
reporting workflows.

The scripts use only the Python standard library. They do not import the C++
engine directly.

## Examples

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
