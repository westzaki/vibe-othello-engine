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

## Midgame Search Baselines

Use Release builds for performance comparisons:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Do not compare Debug and Release numbers. For depth-limited midgame search
baselines, disable exact root endgame solving so the benchmark measures the
search pipeline instead of switching small-empty positions to the exact solver:

```sh
./build/othello_search_bench \
  --mode both \
  --depths 3,4,5,6,7 \
  --positions suite \
  --repetitions 3 \
  --exact-endgame-threshold 0
```

For the stronger existing search path, measure iterative deepening with TT and
PVS enabled, including per-position rows:

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --exact-endgame-threshold 0 \
  --by-position
```

Compare node count, wall-clock time, best move, score, result checksum, work
checksum, TT hit rate, PVS scouts/researches, and
`beta_cut_first_move_pct`. The first-move beta-cut percentage is a useful move
ordering signal; deeper midgame search usually benefits more from better ordering
than from raw NPS alone. If best move, score, or checksum changes, the run is not
a pure speed comparison.

## Comparing the Existing Stronger Midgame Preset

The current stronger preset is existing behavior, not a default policy change:

- fixed plain: fixed depth, TT off, PVS off
- stronger preset: iterative deepening, TT on, PVS on

Compare fixed plain with:

```sh
./build/othello_search_bench \
  --mode fixed \
  --depths 3,4,5,6,7 \
  --positions suite \
  --repetitions 3 \
  --tt off \
  --pvs off \
  --exact-endgame-threshold 0
```

Compare the existing stronger preset with:

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 3,4,5,6,7 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --exact-endgame-threshold 0
```

This comparison uses the observability counters in `SearchStats`; it does not
imply that TT, PVS, or iterative search should become default-on. Keep Release
builds separate from Debug builds, and keep `--exact-endgame-threshold 0` when
the goal is depth-limited midgame measurement. Treat best move, score, PV, or
checksum changes as behavior changes first, not pure speed changes. When
comparing different benchmark modes, remember that benchmark checksums include
the mode to make each command reproducible; use them together with best move,
score, and PV rather than as a mode-independent equality check.

`--tt-entries` requests an approximate number of midgame transposition table
entries. The engine may round this to a small bucketed power-of-two capacity, so
use the same setting when comparing TT hit, collision, overwrite, and rejected
store rates across runs.

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

Use the C++ match runner for lightweight deterministic self-play checks, then
summarize the JSONL output with the canonical Python summary script:

```sh
./build/othello_match_runner \
  --black search:depth=3 \
  --white random \
  --games 8 \
  --swap-sides true \
  --seed 1 \
  --openings data/openings/smoke_openings.txt \
  --output build/matches/search3_vs_random.jsonl

python3 tools/scripts/match_summary.py \
  --input build/matches/search3_vs_random.jsonl \
  --by-opening
```

Search players also accept runner-local options for comparing existing search
features without changing engine code:

```sh
./build/othello_match_runner \
  --black search:depth=4,tt=on,pvs=on,exact=off \
  --white search:depth=4,tt=off,pvs=off,exact=off \
  --games 8 \
  --swap-sides true \
  --seed 1 \
  --openings data/openings/smoke_openings.txt \
  --output build/matches/search4_tt_pvs_vs_plain.jsonl

python3 tools/scripts/match_summary.py \
  --input build/matches/search4_tt_pvs_vs_plain.jsonl \
  --by-opening
```

Supported search player options are `tt=on|off`, `pvs=on|off`,
`exact=off|N`, and `tt_entries=N`. The plain `search:depth=N` form keeps the
same defaults as the existing fixed-depth search path. `tt_entries=N` only sets
the transposition-table capacity; include `tt=on` when the match should use the
table.

The Python summary script reports A/B wins, draws, average disc diff from the
player A perspective, average plies, average passes, error-game count, optional
nodes/time averages, and optional per-opening rows. It is intentionally not an
Elo or significance tool.

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
