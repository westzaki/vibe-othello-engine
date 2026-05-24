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

Probe the external engine adapter scaffold with the fake engine:

```sh
printf 'board text\n' | python3 tools/scripts/run_external_engine_once.py \
  --stdin-board \
  --engine-cmd -- python3 tools/scripts/external_engines/fake_engine.py --move d3
```

External engine binaries, including NTest or Edax, are not stored in this
repository. The current adapter is only a process/timeout/error-handling
scaffold; engine-specific protocols belong in later scripts. Adapter options
must appear before the `--engine-cmd --` boundary; everything after that boundary
is passed to the engine command. The one-shot adapter validates move tokens as
`a1` through `h8` or `pass`, and the CLI can pass `--workdir PATH` and repeated
`--env KEY=VALUE` overrides to the engine process. One-shot process adapters
live in `external_engines/one_shot.py`; persistent/stateful protocol adapters
should grow separately under `external_engines/persistent.py`.

Python script tests run in CI. Run the same checks locally with:

```sh
python3 -m unittest discover tools/scripts/tests
```
