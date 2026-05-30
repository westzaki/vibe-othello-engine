# Capability Map

This document describes the durable capability map for the engine.

It is not a linear development phase checklist. The areas below are workstreams.
They can be revisited whenever correctness, API clarity, measurement quality, or
maintainability requires it.

The repository should be developed as a reusable C++ engine library first.
External wrappers and integrations may use this library later, but they should
not shape the core engine too early.

## Guiding Principles

Do not optimize what cannot be checked.

A clever evaluation function is not useful if the rule core is wrong. It is also
hard to judge search or evaluation changes if the engine is unstable, shallow in
uncontrolled ways, or difficult to benchmark.

Prefer changes that improve at least one of these:

- correctness
- API clarity
- reproducibility
- observability
- search or evaluation strength
- performance
- maintainability

Avoid changes that make the core engine depend on product-specific integration
concerns, local machine configuration, or external binaries.

## Rule Core

The rule core is the foundation of the engine.

It should cover:

- board representation
- side-to-move
- legal move generation
- flip calculation
- move application
- pass handling
- game-over detection
- terminal scoring
- coordinate conversion
- simple position serialization where useful

The public API does not need to be large, but it should stay clear. Avoid
exposing internal implementation details too early.

Rule-core changes are always allowed when they improve correctness, clarity, or
testability, even if the engine has already advanced into search, endgame, or
evaluation work.

## Correctness Harness

Rule bugs, search bugs, and endgame bugs should be cheap to find.

The test suite should cover:

- initial positions
- normal moves
- edge and corner flips
- multi-direction flips
- pass positions
- terminal positions
- scoring
- coordinate conversion
- fixed regression positions
- random legal playouts

External engines may be used as optional validation tools, but the core library
must not depend on them.

Correctness tests should remain easy to run in the normal development workflow.
Heavier validation can live in optional tools, scripts, or local experiments.

## Public API and Library Boundaries

The repository should expose a small public C++ API that can be used by tests,
tools, sample programs, and external wrapper repositories.

Public headers should stay clean and stable. Internal search details, benchmark
schemas, external-process handling, and local configuration should not leak into
the public API.

Add public API only when there is a clear use case and a test or example that
shows how it should be used.

## Baseline Players and Evaluation

Simple deterministic players are useful before building a serious engine.

Useful baseline players include:

- first-legal-move player
- explicitly configured random player
- one-ply evaluator-based player
- fixed-depth search player

These are mainly for tests, samples, match-runner plumbing, and diagnostics.

The first evaluation functions should be modest and understandable. They may use
basic features such as disc difference, mobility, corners, potential mobility,
frontier discs, and risky-square penalties.

Early evaluation should prioritize explainability over raw playing strength.

## Search

Search should be deterministic unless randomness is explicitly configured.

Useful search capabilities include:

- fixed-depth negamax
- alpha-beta pruning
- pass-node handling
- terminal handling
- principal variation reporting
- node and timing stats
- root candidate diagnostics
- transposition tables
- move ordering
- iterative deepening
- aspiration windows
- PVS or similar safe search optimizations

Search changes should be measured. Useful measurements include selected move,
score, depth, nodes, wall-clock time, PV, TT stats, and benchmark checksums.

Search values should not be confused with exact endgame scores unless the search
has actually entered an exact solving mode.

## Exact Endgame

The engine should support exact solving for small-empty-count positions.

Exact endgame work should include:

- exact disc-difference search
- pass-aware solving
- clear exact-score semantics
- endgame-specific stats
- safe move ordering
- transposition tables where appropriate
- last-N optimizations where appropriate

Exact endgame solving should stay separate from heuristic evaluation and
selective pruning that can change the true result.

Optimizations are welcome when they preserve exactness. Result-changing
forward-pruning techniques belong outside the exact core unless the mode is
explicitly documented as non-exact.

## Evaluation Improvement

Evaluation should improve through measured experiments.

Evaluation work is most useful when the rule core, search, and benchmark workflow
are trustworthy enough to judge the change. This is a dependency guideline, not a
one-way phase gate. If evaluation work reveals a rule, search, or tooling issue,
go back and fix it.

Evaluation changes should be:

- deterministic
- understandable
- measurable
- easy to compare with a baseline
- easy to roll back

Useful evidence includes:

- evaluation breakdowns
- fixed-position analysis
- tactical or semantic position suites
- search behavior changes
- base/head match results
- optional games against external engines
- runtime and node-count impact

Avoid large parameter sets or complex learned tables until there is a tuning,
comparison, and rollback workflow that can support them.

## Tooling and Experiments

Tooling is part of engine development.

Useful tools include:

- rule-core benchmarks
- search benchmarks
- endgame benchmarks
- evaluation benchmarks
- position analyzers
- match runners
- base/head comparison scripts
- external-engine adapters
- summary and report scripts

Tooling-only changes are valuable when they make a decision possible, reduce
correctness risk, improve reproducibility, or support a requested cleanup.

Before adding a new tool, check:

- what decision it enables
- why existing tools are not enough
- whether raw output and curated summaries are separated
- whether it stays outside the core engine
- how it will be verified

Raw local experiment output should not be committed. Curated summaries may be
kept when they have lasting historical value.

## External Validation

External engine comparison is useful, but it should remain optional.

External validation may include:

- fake external engine tests
- protocol adapter tests
- smoke tests against a locally configured engine
- base/head comparison using separate binaries
- optional reference-engine matches

External engine binaries, local paths, machine-specific configs, and large
datasets should not be required for the normal build or unit-test workflow.

Base/head comparisons should use separate builds or processes when comparing
code changes. In-process players inside one binary are useful for testing, but
they are not a valid old-code-versus-new-code comparison.

## Performance

Performance work should be based on repeatable measurements.

Useful measurements include:

- node count
- wall-clock time
- nodes per second
- selected move
- score
- principal variation
- benchmark checksum
- TT lookups and hits
- beta cuts
- first-move beta-cut rate
- evaluation calls
- build type
- compiler and platform notes where relevant

Do not judge performance by NPS alone. A slower evaluation or ordering step may
still be useful if it reduces the search tree enough or improves playing strength.

Likewise, a faster benchmark is not automatically a stronger engine.

## Library Hardening

The engine should eventually be comfortable to use from external repositories.

Library hardening includes:

- public header cleanup
- documented move and coordinate formats
- documented failure behavior
- install/export targets where useful
- small examples
- stable release artifacts
- clear separation between public API and internal implementation

Library hardening should not turn the core engine into an application framework.
Product-specific UI, platform packaging, and wrapper-specific bindings should
stay outside this repository.

## Research Backlog

The following may become valuable, but should not drive the core design too
early:

- opening books
- large learned evaluation tables
- complex pattern evaluation
- selective midgame pruning
- advanced time management
- parallel search
- large external datasets
- platform-specific packaging
- product-specific UI
- networking or multiplayer features
- heavy dependencies

These should be introduced through dedicated issues, experiments, wrapper
repositories, or optional tools when the need is clear.

## Work Selection

Prefer work that does at least one of the following:

- reduces correctness risk
- makes behavior easier to observe
- makes experiments easier to reproduce
- improves search, evaluation, or endgame capability
- clarifies public APIs
- strengthens repository boundaries
- removes unnecessary complexity

Short-term priority should be decided in issues, PR descriptions, project boards,
or task prompts, not by treating this capability map as a strict sequence.
