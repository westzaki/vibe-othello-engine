# Benchmarks

This is the canonical benchmark and measurement guide for the engine.

Benchmarks are measurement workflows, not permanent pass/fail thresholds. Use
them to compare changes with repeatable evidence, then explain what changed and
why it matters.

Keep benchmark notes practical: record the build type, machine, command, and the
commit being compared. Concrete depths, repetitions, and game counts are profile
parameters, not permanent standards. Keep parameters stable within one
comparison, but update profile defaults as engine speed, evaluator cost, and
measurement needs change.

## Benchmark Families

The current benchmark and measurement families are:

- rule core: low-level rule operation cost and correctness-adjacent performance
- search: depth-limited and iterative search behavior, speed, and observability
- exact endgame: exact solver speed, stats, and diagnostic breakdowns
- evaluation: score, best-move, PV, checksum, and breakdown changes
- match/self-play: deterministic game samples inside one build
- base/head comparison: external-process old-code-versus-new-code measurement

## Benchmark Profiles

Choose a profile by intent before choosing exact parameters:

- smoke: quick sanity check for obvious regressions
- standard: everyday PR evidence with stable local settings
- deep: slower or broader check for changes that only show value at larger depth,
  more positions, or more games
- diagnostic: focused investigation of one position, opening, fixture, or search
  behavior; diagnostic totals may not be comparable to normal benchmark totals
- promotion: stronger evidence before changing defaults, presets, or public
  guidance

The command examples below show current useful profiles. Treat their depths,
repetitions, empty counts, and game counts as examples to keep stable within a
comparison, not as durable thresholds for all future work.

## Build

Prefer Release builds for benchmark numbers:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Do not compare Debug numbers with Release numbers. Use the same machine, build
type, tool options, and position set when comparing two commits.

## Quick Smoke Checks

Run a small smoke profile first to catch obvious regressions before collecting
longer numbers. Current examples:

```sh
./build/othello_rule_core_bench
./build/othello_search_bench --mode both --depths 1,2,3,4,5 --positions smoke --repetitions 3
./build/othello_endgame_bench --positions smoke
```

Available options may change; use `--help` for current details.

## Search Benchmarks

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

For the stronger existing search path, the current standard-profile example
measures iterative deepening with TT and PVS enabled, including per-position
rows:

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

To decide whether root exact endgame should trigger earlier in the normal search
pipeline, use the threshold matrix form instead of running separate one-off
commands:

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions threshold \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --exact-endgame-thresholds 0,12,14,16,adaptive16 \
  --by-position
```

The `threshold` position set extends the normal suite with focused 15/16-empty
root fixtures for exact-threshold experiments. The matrix output includes the
threshold/profile, input empty count, whether the root was solved exactly, skip
reason for non-exact roots, per-position result/work checksums, TT stats, and
per-threshold p95/max latency summaries. The `adaptive16` profile is
experimental: roots at 14 empties or fewer solve exactly, while 15/16-empty roots
solve only when the current side has a move, `legal_moves_current <= 10`, and
`legal_moves_opponent <= 10`. Treat threshold/profile changes as semantic search
changes whenever a position becomes root-exact; exact scores, best moves, PVs,
and checksums may legitimately differ from depth-limited search.

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

## Comparing Iterative Aspiration Windows

Aspiration windows are opt-in and apply only to `search_iterative()`. They use
the previous iterative score to narrow the next depth's root alpha/beta window,
then re-search with wider windows or a final full-window fallback on fail-low or
fail-high. Defaults keep full-window behavior.

Compare the full-window iterative preset with:

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

Compare aspiration with:

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 3,4,5,6,7 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --aspiration on \
  --exact-endgame-threshold 0
```

A deep profile is useful for search changes that may only show value after
shallow overhead is amortized. Current examples:

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 8,9,10 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --exact-endgame-threshold 0
```

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 8,9,10 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --aspiration on \
  --exact-endgame-threshold 0
```

Compare a wider window when the default window shows repeated fail-high/fallback
work:

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 8,9 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --aspiration on \
  --aspiration-window 100 \
  --exact-endgame-threshold 0
```

Stress fail-low/high handling with a deliberately narrow window:

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 5,6 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --aspiration on \
  --aspiration-window 1 \
  --aspiration-max-researches 2 \
  --exact-endgame-threshold 0
```

When measuring depth-limited midgame search, keep `--exact-endgame-threshold 0`
explicit. If best move, score, PV, or result checksum changes versus the
full-window iterative run, treat it as a correctness bug unless a semantic
change was intentionally documented. Compare nodes, wall-clock time, TT stats,
PVS stats, `beta_cut_first_move_pct`, and the aspiration counters
(`asp_searches`, `asp_researches`, `asp_fail_lows`, `asp_fail_highs`,
`asp_fallbacks`). Work checksum may change when aspiration changes the verified
search path.

## Exact Endgame Benchmarks

Use the exact endgame benchmark when changing the standalone exact solver,
endgame fixtures, or exact endgame observability:

Current standard-profile examples:

```sh
./build/othello_endgame_bench --empties 14 --repetitions 1
./build/othello_endgame_bench --empties 16 --repetitions 1
./build/othello_endgame_bench --empties 18 --repetitions 1
./build/othello_endgame_bench --empties 20 --repetitions 1
```

Current diagnostic examples:

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

## Evaluation Measurements

Evaluation-strength PRs are allowed to change best moves, scores, PVs, and
result checksums. Treat those checksum changes as expected behavior changes, not
as pure speed regressions. Still keep the comparison deterministic and explain
the changed scores with `evaluate_basic_breakdown()` or `othello_analyze_position`.

For phase-aware or feature-weight evaluation PRs, a useful current profile
usually includes:

- Release build and full `ctest`
- `othello_analyze_position` output for a representative input board
- smoke search benchmark with `--exact-endgame-threshold 0`
- suite search benchmark using the current opt-in stronger preset
- a base/head comparison when an old evaluator binary is available

Useful commands:

```sh
./build/othello_search_bench \
  --mode both \
  --depths 1,2,3,4,5 \
  --positions smoke \
  --repetitions 1 \
  --exact-endgame-threshold 0
```

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --aspiration on \
  --exact-endgame-threshold 0
```

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 8,9 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --aspiration on \
  --exact-endgame-threshold 0
```

Do not describe an evaluation PR as a pure speed comparison. Record score,
best-move/PV, result checksum, work checksum, and a short explanation of the
new breakdown fields or weights.

## Base/Head Comparison

Use the specialized external-process base/head workflow when a change may affect
playing strength and an old-code-versus-new-code comparison is needed:
[`docs/benchmarks/base-head.md`](benchmarks/base-head.md).

Do not compare two in-process `search:` players when the code itself changed,
because both players use the same linked engine. Base/head depths, game counts,
openings, and timeouts are profile parameters. Keep them stable within one
comparison, then update the profile defaults when engine speed, evaluator cost,
or measurement needs change.

The current base/head smoke and standard-profile examples live in
[`docs/benchmarks/base-head.md`](benchmarks/base-head.md). Stronger claims need
broader openings, larger game counts, and careful explanation of changed match
outcomes.

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
`exact=off|N|adaptive16`, and `tt_entries=N`. The plain `search:depth=N` form
keeps the same defaults as the existing fixed-depth search path. `exact=N` uses
the fixed root threshold, while `exact=adaptive16` is an experimental opt-in
profile that solves roots up to 14 empties and conservatively gates 15/16-empty
roots. `tt_entries=N` only sets the transposition-table capacity; include
`tt=on` when the match should use the table.

For match-level adaptive16 smoke tests, keep the comparison deterministic and
swap sides across the same openings:

```sh
./build/othello_match_runner \
  --black search:depth=5,tt=on,pvs=on,exact=14 \
  --white search:depth=5,tt=on,pvs=on,exact=adaptive16 \
  --games 40 \
  --swap-sides true \
  --seed 20260531 \
  --openings data/openings/smoke_openings.txt \
  --output build/matches/exact14_vs_adaptive16.jsonl

python3 tools/scripts/match_summary.py \
  --input build/matches/exact14_vs_adaptive16.jsonl \
  --by-opening
```

The Python summary script reports A/B wins, draws, average disc diff from the
player A perspective, average plies, average passes, error-game count, optional
nodes/time averages, exact-root counts, and optional per-opening rows. Match
JSONL records also include `exact_root_events` for each fired exact root, with
the ply, board, legal move counts, elapsed time, node count, TT counters, best
move, score, and PV. Those events are intended for diagnosing heavy adaptive
endgame roots before changing gates or exact-solver ordering. The summary script
is intentionally not an Elo or significance tool.

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

Treat baseline snapshots as historical evidence, not current instructions.
Settings such as depths, repetitions, thresholds, and fixture selections describe
the profile used at the time of collection unless current project guidance or
the current task explicitly adopts them.

Current exact endgame reference:

- [2026-05-24 PR78 child breakdown exact endgame baseline](perf/baselines/2026-05-24-pr78-8d31a40-exact-endgame-child-breakdown.md)
- [2026-05-23 PR70 interior PVS exact endgame baseline](perf/baselines/2026-05-23-pr70-0c89ba4-exact-endgame-interior-pvs.md)

## Position Analysis

Use the analysis tool for a focused position check rather than a benchmark suite.
The current example uses a deeper search profile; adjust depth and options to
match the comparison intent:

```sh
./build/othello_analyze_position --stdin --depth 10 --mode iterative --tt on
```

Use `--help` for the current input and option details.

## Notes

Benchmark tools are lightweight development aids, not public API contracts. Keep
their output useful for comparison, but avoid depending on exact formatting in
external scripts unless that contract is introduced deliberately.
