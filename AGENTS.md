# AGENTS.md

## Goal

Build a small, understandable Othello/Reversi engine in modern C++.

This repository produces a reusable static library as its main release artifact.

The engine should eventually support:

- position evaluation
- move recommendation
- coach / review analysis primitives
- benchmark games against external engines
- integration from other repositories such as web, WASM, or Android apps

Correctness and readability come before strength.

## Repository Scope

This repository owns:

- the C++ engine core
- the static library release artifact
- public C++ headers
- unit tests
- small sample CLI applications
- lightweight benchmark or experiment tools
- documentation for using the library

This repository does not own app-specific UI, web UI, Android UI, or WASM packaging.
Those should live in external wrapper repositories or integration layers.

## Architecture Rule

Keep the engine as a pure C++ library.

The engine should expose a small public C++ API that can be called from:

- unit tests
- sample CLI applications
- benchmark tools
- external wrapper repositories

External repositories may create WASM, Android, or app-specific bindings on top of this library.

Prefer this dependency direction:

```text
app / WASM / Android / CLI / tools
        ↓
public C++ API
        ↓
engine core
        ↓
internal implementation
```

The core engine must not depend on UI frameworks, platform SDKs, network APIs, or app-specific concepts.

## Roadmap

Follow [`docs/ROADMAP.md`](docs/ROADMAP.md).

In short, build the rule core first, prove it with tests, improve search depth,
add endgame primitives, and only then invest seriously in evaluation tuning.

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

## Implementation Rule for Agents

When modifying this project:

1. make the smallest useful change
2. keep the engine UI-independent
3. add or update tests
4. do not introduce heavy dependencies without strong reason
5. avoid large rewrites unless explicitly requested
6. explain what changed and how to verify it

## Pull Request Rule

When creating a pull request for this project:

1. use `.github/PULL_REQUEST_TEMPLATE.md`
2. write the PR body in Japanese
3. write only the PR title in English
4. format the PR title as a Conventional Commit, for example `feat: add legal move generation`
