# Benchmarks

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

## Exact Endgame Benchmarks

Use the exact endgame benchmark when changing the standalone exact solver,
endgame fixtures, or exact endgame observability:

Standard benchmark commands:

```sh
./build/othello_endgame_bench --empties 14 --repetitions 1
./build/othello_endgame_bench --empties 16 --repetitions 1
./build/othello_endgame_bench --empties 18 --repetitions 1
./build/othello_endgame_bench --empties 20 --repetitions 1
```

Diagnostic commands:

```sh
./build/othello_endgame_bench --empties 20 --repetitions 1 --root-breakdown
./build/othello_endgame_bench --empties 20 --repetitions 1 --root-breakdown --expand-worst-candidate
./build/othello_endgame_bench --breakdown-position 20-empty-high-mobility-lite --repetitions 1 --root-breakdown --expand-worst-candidate
```

The standard exact endgame rows measure one normal `solve_exact_endgame` call per
position. Diagnostic modes intentionally solve candidates separately:

- `--root-breakdown` solves each root candidate independently and reports
  candidate-level cost, margin rank, TT stats, position metrics, and PV.
- `--expand-worst-candidate` requires `--root-breakdown`; it finds the slowest
  root candidate for each selected position and expands that candidate one ply to
  show child-candidate cost.
- `--breakdown-position NAME` restricts the selected benchmark positions by exact
  fixture name. This is useful when inspecting one heavy 18/20-empty fixture
  without running the whole bucket.

Use diagnostic output to understand where time goes. Do not compare total
candidate time from diagnostic modes directly with the normal benchmark row.

## Comparing Search Changes

Compare one change at a time when possible. Keep the command stable, then record
wall time, node counts, and any benchmark checksum or result fields that help
detect behavior changes.

If a checksum, best move, or score changes, do not treat the result as a pure
speed comparison. First check whether the search behavior changed intentionally.

## Self-Play Match Summaries

Use the match runner for lightweight deterministic self-play checks, then summarize
the JSONL output with `othello_match_summary`:

```sh
./build/othello_match_runner \
  --black search:depth=3 \
  --white random \
  --games 8 \
  --swap-sides true \
  --seed 1 \
  --openings data/openings/smoke_openings.txt \
  --output build/matches/search3_vs_random.jsonl

./build/othello_match_summary \
  --input build/matches/search3_vs_random.jsonl \
  --by-opening
```

The summary reports A/B wins, draws, average disc diff from the player A
perspective, average plies, average passes, error-game count, and optional
per-opening rows. It is intentionally not an Elo or significance tool.

## Reading Results

Performance numbers are environment-dependent. Prefer relative comparisons from
the same local setup over absolute numbers copied from another machine.

For small endgame positions, remember that an enabled exact endgame threshold can
switch root search from depth-limited search to exact solving. Disable or fix the
threshold explicitly when you need to compare only depth-limited behavior. Exact
root endgame results also report score and depth with exact-endgame semantics,
not ordinary heuristic depth-limited search semantics.

## Baseline Snapshots

Performance snapshots live under [`docs/perf/baselines/`](perf/baselines/).
They are append-only local measurements, not CI thresholds. Add a new snapshot
when search or exact endgame performance changes meaningfully, and keep older
files for comparison history.

Current exact endgame reference:

- [2026-05-24 PR78 child breakdown exact endgame baseline](perf/baselines/2026-05-24-pr78-8d31a40-exact-endgame-child-breakdown.md)
- [2026-05-23 PR70 interior PVS exact endgame baseline](perf/baselines/2026-05-23-pr70-0c89ba4-exact-endgame-interior-pvs.md)

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
