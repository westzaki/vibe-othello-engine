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

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Development Notes

Read [AGENTS.md](AGENTS.md) for Codex/agent instructions and repository
conventions.

Read [docs/ROADMAP.md](docs/ROADMAP.md) for the long-term development direction.

Read [docs/CAPABILITY_MAP.md](docs/CAPABILITY_MAP.md) for the durable engine
capability map and workstreams.

Read [docs/BENCHMARKS.md](docs/BENCHMARKS.md) for lightweight benchmark guidance.

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
