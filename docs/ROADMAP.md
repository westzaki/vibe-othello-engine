# Roadmap

This document describes the long-term development direction for
`vibe-othello-engine`.

It is not a strict phase checklist and it is not a queue of current tasks. Short
term priorities belong in issues, project boards, PR descriptions, or
task-specific prompts.

For the durable workstream-level capability map, see
[CAPABILITY_MAP.md](CAPABILITY_MAP.md).

## Long-Term Direction

The repository should grow as a reusable C++ Othello/Reversi engine library
first.

The long-term direction is:

1. keep the rule core correct and well tested
2. keep public C++ APIs small and stable
3. grow deterministic search and exact endgame solving
4. improve evaluation through reproducible experiments
5. keep tooling, benchmarks, and external validation outside the core engine
6. harden the library for external wrappers and integrations

External wrappers and integrations may use this library later, but they should
not shape the core engine too early.

## Principles

Correctness, readability, and reproducibility come before raw strength.

This does not mean strength is unimportant. It means strength improvements should
be made on top of a core that can be tested, measured, and trusted.

Revisit earlier areas whenever correctness, API clarity, measurement quality, or
maintainability requires it. The project should never treat earlier workstreams
as permanently closed.

Prefer work that does at least one of the following:

- reduces correctness risk
- makes behavior easier to observe
- makes experiments easier to reproduce
- improves search, evaluation, or endgame capability
- clarifies public APIs
- strengthens repository boundaries
- removes unnecessary complexity

## Boundaries

The core engine should stay independent of:

- product-specific UI
- platform packaging
- application-specific bindings
- local machine configuration
- external engine binaries
- benchmark orchestration details
- raw experiment output

Those concerns can live in tools, scripts, wrapper repositories, local config, or
experiment outputs as appropriate.

## Workstream Map

The durable workstreams are described in
[CAPABILITY_MAP.md](CAPABILITY_MAP.md):

- Rule Core
- Correctness Harness
- Public API and Library Boundaries
- Baseline Players and Evaluation
- Search
- Exact Endgame
- Evaluation Improvement
- Tooling and Experiments
- External Validation
- Performance
- Library Hardening
- Research Backlog

The workstreams are not phases. They can be developed, revisited, or refined in
whatever order best serves the current task while preserving correctness and
repository boundaries.
