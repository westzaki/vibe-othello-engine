# vibe-othello-engine

A small, understandable Othello/Reversi engine in modern C++.

This repository builds a reusable static C++ library. The project prioritizes
correctness, readability, and reproducibility before raw engine strength.

Strength and performance improvements are welcome, but they should be measurable.

## Scope

This repository owns the C++ engine core, public headers, unit tests, small
sample programs, lightweight benchmark or experiment tools, and documentation
for using and validating the library.

Product-specific UI, platform packaging, and application-specific bindings
should live outside this repository.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The default build includes the reusable library, developer tools, experiment
tools, and tests. Library consumers can configure a smaller library-only build:

```sh
cmake -S . -B build-lib \
  -DCMAKE_BUILD_TYPE=Release \
  -DOTHELLO_BUILD_TOOLS=OFF \
  -DOTHELLO_BUILD_TESTS=OFF
cmake --build build-lib
```

Build scope options default to `ON` to preserve the normal developer and CI
workflow:

- `OTHELLO_BUILD_TOOLS`: build developer tools and command-line executables.
- `OTHELLO_BUILD_TESTS`: build and register the test suite.
- `OTHELLO_BUILD_EXPERIMENT_TOOLS`: build research, label, and training helper
  tools such as exact-label dumping, eval-vs-exact analysis, and position
  sampling.

The current test suite covers tool cores, so configure
`OTHELLO_BUILD_TESTS=OFF` when disabling `OTHELLO_BUILD_TOOLS` or
`OTHELLO_BUILD_EXPERIMENT_TOOLS`. Experiment tools are built only when tools are
also enabled.

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Development Notes

Read [AGENTS.md](AGENTS.md) for Codex/agent instructions and repository
conventions.

Read [docs/README.md](docs/README.md) for the documentation index.

Read [docs/roadmap.md](docs/roadmap.md) for the long-term development direction.

Read [docs/capability-map.md](docs/capability-map.md) for the durable engine
capability map and workstreams.

Read [docs/benchmarks.md](docs/benchmarks.md) for lightweight benchmark guidance.

The former `tools/scripts/run_match_experiment.py` maintenance wrapper has been
removed. Run `./build/othello_match_runner` directly, then summarize the
generated JSONL with `python3 tools/scripts/match_summary.py`.

## Documentation Model

Permanent docs should describe durable project boundaries, workflows, command
references, and long-term design principles.

Short-lived information should not be promoted into permanent docs. Put it in a
more appropriate place instead:

- current problems and investigation context: issues
- change-specific intent and evidence: PR descriptions
- local raw experiment output: `runs/`
- historical performance snapshots: curated baseline or experiment reports
- short-term priorities: issues, project boards, PRs, or task-specific prompts

Historical baselines and experiment notes are evidence, not current instructions.
