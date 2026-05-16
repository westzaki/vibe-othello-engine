# vibe-othello-engine

A small, understandable Othello/Reversi engine in modern C++.

This repository builds a reusable static C++ library. The project prioritizes
correctness and readability before engine strength.

## Scope

This repository owns the C++ engine core, public headers, unit tests, small
sample programs, lightweight benchmark or experiment tools, and documentation
for using the library.

UI, WASM packaging, Android integration, and app-specific bindings should live
outside this repository.

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

Read [AGENTS.md](AGENTS.md) for repository rules and coding guidance.

Read [docs/ROADMAP.md](docs/ROADMAP.md) for the staged engine development plan.
