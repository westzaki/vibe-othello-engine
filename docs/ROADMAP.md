# Roadmap

This document describes how this Othello/Reversi engine should grow over time.

The repository should be developed as a reusable C++ engine library first.
Web, WASM, Android, and other application integrations may use this library later,
but they should not shape the core engine too early.

The overall direction is simple:

1. build the rules correctly
2. prove the rules with tests
3. make the engine search deeper
4. add exact endgame support
5. tune evaluation only after the foundation is trustworthy

Correctness and readability come before strength.

## Guiding Principle

This project should avoid tuning an evaluation function before the engine can
reliably generate legal moves, apply moves, handle passes, score terminal
positions, and run deterministic searches.

A clever evaluation function is not useful if the rule core is wrong. It is also
hard to judge an evaluation function if the searcher is shallow, unstable, or
difficult to benchmark.

For that reason, the early roadmap focuses on bitboards, correctness tests,
deterministic search, and reproducible benchmarks.

## Stage 1: Project Foundation

Create a small C++ project that builds a reusable static library.

The initial repository should include public headers, engine implementation,
unit tests, small examples, tools, and documentation. The exact build setup can
evolve, but a clean checkout should be easy to build and test.

The goal of this stage is not engine strength. The goal is a stable development
workflow.

## Stage 2: Bitboard Rule Core

Implement the Othello/Reversi rules using bitboards from the beginning.

The rule core should cover board representation, side-to-move, legal move
generation, flip calculation, move application, pass handling, game-over
detection, scoring, coordinate conversion, and simple position serialization.

The public API does not need to be perfect immediately, but it should stay small.
Avoid exposing internal implementation details too early.

## Stage 3: Correctness Tests

Make rule bugs cheap to find.

The test suite should cover initial positions, normal moves, edge and corner
flips, multi-direction flips, pass positions, terminal positions, scoring,
coordinate conversion, fixed regression positions, and random legal playouts.

External engines may be used as optional validation tools, but the core library
must not depend on them.

## Stage 4: Minimal Players and Evaluation

Add simple deterministic players before building a serious engine.

Useful early players include a first-legal-move player, an explicitly configured
random player, and a one-ply evaluator-based player. These are mainly for tests,
samples, and wiring the move-selection path.

The first evaluation function should be modest and understandable. It may include
basic features such as disc difference, mobility, corners, and simple risky-square
penalties. It should not be heavily tuned yet.

## Stage 5: Fixed-Depth Search

Add a straightforward deterministic searcher.

Start with negamax and alpha-beta pruning. The searcher should handle pass nodes
and terminal positions correctly, and it should report useful debug information
such as best move, score, depth, and node count.

At this stage, search correctness matters more than raw speed.

## Stage 6: Search Performance

Improve search depth through measurement.

Important future improvements include iterative deepening, move ordering,
position hashing, transposition tables, benchmark positions, and optional time
control.

These changes should be driven by repeatable benchmarks rather than guesswork.

## Stage 7: Endgame Solving

Add exact search for small-empty-count positions.

The engine should clearly distinguish heuristic scores from exact scores. This is
important for future review and coach features, where callers need to know whether
the engine is estimating or solving.

## Stage 8: Evaluation Iteration

Only after the rule core and search are trustworthy should evaluation tuning
become a major focus.

Evaluation changes should be deterministic, understandable, and measurable.
Useful comparison methods include fixed-position tests, self-play, benchmark
positions, and optional games against external engines.

Avoid large parameter sets or complex learned tables until there is a real tuning
pipeline.

## Stage 9: Library Hardening

Make the engine comfortable to use from other repositories.

Review public headers, hide implementation details, document error behavior,
document move and coordinate formats, and provide small examples.

An external project should be able to link the static library and use the engine
without knowing the internal source layout.

## Early Non-Goals

The following are valuable later, but should not drive the early design:

- app-specific UI
- web UI
- WASM packaging
- Android integration
- opening book
- large learned evaluation tables
- complex pattern evaluation
- multiplayer networking
- heavy dependencies

These can come later through wrapper repositories, optional tools, or dedicated
follow-up milestones.