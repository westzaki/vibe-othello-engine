# AGENTS.md

## Goal

Build a small, understandable Othello/Reversi engine in modern C++.

This repository produces a reusable static library as its main release artifact.

The engine should eventually support:

- position evaluation
- move recommendation
- coach / review analysis primitives
- benchmark games against external engines
- integration from external wrapper repositories

Correctness, readability, and reproducibility come before raw strength.
Strength and performance improvements should be measurable.

## Repository Scope

This repository owns:

- the C++ engine core
- the static library release artifact
- public C++ headers
- unit tests
- small sample CLI applications
- lightweight benchmark or experiment tools
- documentation for using the library

This repository does not own product-specific UI, platform packaging, or
application-specific integration code. Those should live in external wrapper
repositories or integration layers.

## Architecture Rule

Keep the engine as a pure C++ library.

The engine should expose a small public C++ API that can be called from:

- unit tests
- sample CLI applications
- benchmark tools
- external wrapper repositories

External repositories may create product-specific integrations on top of this library.

Prefer this dependency direction:

```text
wrappers / integrations / CLI / tools
        ↓
public C++ API
        ↓
engine core
        ↓
internal implementation
```

The core engine must not depend on UI frameworks, platform SDKs, network APIs,
or product-specific concepts.

## Roadmap

Follow [`docs/ROADMAP.md`](docs/ROADMAP.md).

The roadmap is a staged capability plan, not a queue of current tasks. Put
short-term priorities, task-specific hypotheses, temporary diagnostics, and
one-off investigation notes in issues, PR descriptions, task prompts, or
experiment reports instead.

In short, build the rule core first, prove it with tests, improve search depth,
add endgame primitives, and only then invest seriously in evaluation tuning.

When roadmap guidance and the user's current task conflict, follow the current
task while preserving correctness and repository boundaries.

## Documentation Rule

Permanent documentation should contain durable policies, architecture
boundaries, workflows, and command references.

Historical baselines and experiment notes are evidence, not current
instructions. If historical notes conflict with current docs or the user's
current task, current docs and the current task win.

Task-specific hypotheses, temporary diagnostics, and stale experimental
conclusions should not be promoted into permanent docs.

## Coding Style

Use modern C++ for learning and maintainability.

Default language standard:

- C++23

Guidelines:

- prefer value types and RAII
- prefer `std::array`, `std::vector`, `std::optional`, `std::span`, and `std::string_view` where appropriate
- prefer `enum class` over plain enums
- prefer `constexpr` for small pure functions and constants
- prefer clear ownership over raw pointers
- avoid global mutable state
- avoid macros unless there is a strong reason
- avoid inheritance-heavy designs in the core engine
- avoid premature templates and metaprogramming
- keep public headers clean and stable
- separate public API from internal implementation
- make search deterministic unless randomness is explicitly configured

Use modern C++ features to make the code safer and clearer, not to show off.

### Error Handling and Invariants

Core search, move generation, and evaluation code should be `noexcept` where practical.

Use assertions only for internal invariants, not for user input validation.

Public APIs should validate user-provided input explicitly and document their failure behavior.
Normal gameplay paths should avoid exceptions where practical.

## Testing Rule

Prefer tests that make rule bugs obvious.

At minimum, test legal move generation, flip calculation, move application,
pass handling, terminal scoring, coordinate conversion, and random legal playouts.

Move generation and `apply_move` must be tested before search strength is optimized.
External-engine comparison tests are useful, but they should be optional.
When behavior, search, evaluation, or performance-sensitive code changes,
include enough evidence for a reviewer to understand what changed and how it was
verified.

## Implementation Rule for Agents

When modifying this project:

1. make the smallest coherent change that advances the user's stated goal
2. keep the engine independent of product-specific integration concerns
3. add or update tests when behavior or correctness changes
4. do not introduce heavy dependencies without strong reason
5. avoid large rewrites unless explicitly requested
6. explain what changed and how to verify it

Do not confuse "small" with "low-value".

A small coherent change may be a semantic code change, test, fixture,
documentation update, tooling improvement, or refactor. Tooling-only,
documentation-only, and refactor-only changes are valid when they directly
support the requested goal, reduce correctness risk, make measurement possible,
or are explicitly requested.

When making a tooling-only, documentation-only, or refactor-only change, explain:

- why it is needed now
- what it enables
- what semantic change, measurement, or cleanup can follow

If the user asks for behavior, strength, evaluation, search, or performance
improvement, the result should include at least one of:

- a semantic code change related to that goal
- a measured experiment or report that directly informs that goal
- a tool, fixture, or refactor that makes the next decision possible

Intentional behavior changes are acceptable when the user asks for strength,
evaluation, search, or performance improvement. In those cases, changed scores,
best moves, benchmark checksums, or match outcomes are expected evidence, not
automatic regressions. Preserve old behavior only when it is part of the stated
goal, a correctness contract, or a named baseline needed for comparison.

A drastic evaluator or search change is acceptable when it is isolated,
measurable, and reversible. Prefer a named preset, explicit option, fixture, or
baseline comparison when the change is too large to judge from unit tests alone.

Avoid repeating prerequisite-only changes unless the user explicitly asks for
repository cleanup or the next step is still blocked.

## Experiment and Measurement Rule

For changes intended to affect playing strength, search behavior, evaluation
quality, or performance:

- define the hypothesis before changing code
- change one primary idea at a time
- keep correctness checks separate from strength or speed claims
- compare against a stable baseline when practical
- record commands, inputs, build type, and relevant options
- report both positive and negative results

Prefer reproducible measurements over broad claims.

## Pull Request Rule

When creating a pull request for this project:

1. use `.github/PULL_REQUEST_TEMPLATE.md`
2. write the PR body in Japanese
3. write only the PR title in English
4. format the PR title as a Conventional Commit, for example `feat: add legal move generation`

For behavior, strength, evaluation, search, or performance PRs, include the
relevant measurement or explain why measurement was not possible. For
tooling-only, documentation-only, or refactor-only PRs, explain what they enable.
