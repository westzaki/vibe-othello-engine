# Benchmarking

Use benchmarks to compare search and rule-core changes with repeatable evidence.
Keep benchmark notes practical: record the build type, machine, command, and the
commit being compared.

## Build

Prefer Release builds for benchmark numbers:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Do not compare Debug numbers with Release numbers. Use the same machine, build
type, tool options, and position set when comparing two commits.

## Quick smoke checks

Run a small set first to catch obvious regressions before collecting longer
numbers:

```sh
./build/othello_rule_core_bench
./build/othello_search_bench --mode both --depths 1,2,3,4,5 --positions smoke --repetitions 3
./build/othello_endgame_bench --positions smoke
```

Available options may change; use `--help` for current details.

## Comparing Search Changes

Compare one change at a time when possible. Keep the command stable, then record
wall time, node counts, and any benchmark checksum or result fields that help
detect behavior changes.

If a checksum, best move, or score changes, do not treat the result as a pure
speed comparison. First check whether the search behavior changed intentionally.

## Reading Results

Performance numbers are environment-dependent. Prefer relative comparisons from
the same local setup over absolute numbers copied from another machine.

For small endgame positions, remember that an enabled exact endgame threshold can
switch root search from depth-limited search to exact solving. Disable or fix the
threshold explicitly when you need to compare only depth-limited behavior. Exact
root endgame results also report score and depth with exact-endgame semantics,
not ordinary heuristic depth-limited search semantics.

## Position Analysis

Use the analysis tool for a focused position check rather than a benchmark suite:

```sh
./build/othello_analyze_position --stdin --depth 10 --mode iterative --tt on
```

Use `--help` for the current input and option details.

## Notes

Benchmark tools are lightweight development aids, not public API contracts. Keep
their output useful for comparison, but avoid depending on exact formatting in
external scripts unless that contract is introduced deliberately.
