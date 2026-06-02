# Base/Head Match Matrix

Use the base/head matrix when an engine change may affect playing strength, such
as evaluation weights or search defaults. The matrix runs two separately built
engine binaries as external NBoard processes, then records one match JSONL file,
one summary, and one Markdown report per run.

This is a specialized benchmark and measurement workflow under
[`docs/benchmarks.md`](../benchmarks.md), not a separate project guidance
category.

This workflow is deterministic, but it is not an Elo estimate. Use it to detect
obvious regressions or directional improvement. Larger game counts and broader
opening suites are needed for stronger strength claims.

Depths, game counts, openings, and timeouts are profile parameters. Keep them
stable within one comparison, then update the profile defaults as engine speed,
evaluator cost, and measurement needs change.

## Why External Processes

Do not compare `search:` players inside one `othello_match_runner` binary when
testing base versus head evaluator changes. In-process players use the same
linked evaluator and search code, so they cannot compare old code against new
code.

Instead, build base and head in separate worktrees and run:

- `external:base` from the base build's `othello_nboard_engine`
- `external:head` from the head build's `othello_nboard_engine`

The Python matrix script generates the line-based engine config consumed by
`othello_match_runner`.

## Example Workflow

Build the base worktree:

```sh
git worktree add /tmp/vibe-othello-base origin/main
cmake -S /tmp/vibe-othello-base -B /tmp/vibe-othello-base/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/vibe-othello-base/build --target othello_nboard_engine
```

Build the head worktree:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target othello_match_runner othello_nboard_engine
```

Run the current smoke-profile example:

```sh
python3 tools/scripts/base_head_match_matrix.py \
  --base-build /tmp/vibe-othello-base/build \
  --head-build build \
  --base-repo /tmp/vibe-othello-base \
  --head-repo . \
  --openings data/openings/smoke_openings.txt \
  --depths 4,8 \
  --games 12 \
  --seed 20260524 \
  --out runs/base-head/my-change-smoke
```

Run the current standard-profile example:

```sh
python3 tools/scripts/base_head_match_matrix.py \
  --base-build /tmp/vibe-othello-base/build \
  --head-build build \
  --base-repo /tmp/vibe-othello-base \
  --head-repo . \
  --openings data/openings/smoke_openings.txt \
  --depths 4,6,8,10 \
  --games 40 \
  --seed 20260524 \
  --external-timeout-ms 30000 \
  --out runs/base-head/my-change
```

For stronger evidence, increase the opening coverage and game count. Keep the
openings, depths, seed, timeout, build type, and game count stable when comparing
one PR against another.

For evaluation PRs, include a deeper profile when evaluator cost may matter. The
current examples include deeper fixed-depth rows because a heavier evaluator can
look acceptable at shallow depths while increasing deeper search time. The
bundled smoke openings are useful for repeatability, but they are too narrow for
a strength claim by themselves.

## Output Layout

Raw output belongs under `runs/`, which is gitignored:

```text
runs/base-head/<run-name>/
  report.md
  depth-4/
    engines.generated.txt
    match.jsonl
    summary.txt
    command.txt
  depth-6/
    engines.generated.txt
    match.jsonl
    summary.txt
    command.txt
```

`report.md` includes:

- base/head repo paths and git metadata when detectable
- base/head binary paths
- openings, depths, game count, seed, and timeout
- one summary table across depths
- commands used for each depth
- per-depth `match_summary.py --by-opening` output

Do not commit files under `runs/`. If a run becomes a meaningful snapshot,
summarize the important numbers in a committed Markdown file under
`docs/perf/baselines/`.

## Reading Results

Check these first:

- error games must be zero unless intentionally investigating failures
- head/base wins and average disc diff
- per-opening splits
- average time per player, especially at deeper depths
- whether one side times out or returns illegal moves

When one depth or opening regresses, use root candidate analysis before tuning
weights:

```sh
printf '........\n........\n........\n...BW...\n...WB...\n........\n........\n........\nside=B\n' | \
./build/othello_analyze_position \
  --stdin \
  --depth 8 \
  --mode iterative \
  --tt on \
  --pvs on \
  --aspiration on \
  --exact-endgame-threshold 0 \
  --root-candidates
```

Root candidate analysis searches each legal candidate independently. Its
per-candidate node counts, TT stats, and PVS stats are diagnostic and may not
exactly match the work trace of one normal shared-context root search.

If the first root positions do not explain the regression, extract the first
base/head divergence positions from the raw depth JSONL by replaying the match
records with the C++ rule core:

```sh
./build/othello_replay_game \
  --match-jsonl runs/base-head/my-change/depth-8/match.jsonl \
  --format jsonl
```

Use the emitted `board_text` values with `othello_analyze_position
--root-candidates`. The legacy Python `extract_divergence_positions.py` script
is kept only for transition and should not be used for new diagnostics because
it duplicates Othello rule logic outside the engine.

The matrix is deterministic for the selected openings and seed. It is still only
a sample of positions. Avoid claiming Elo from it, and treat small game counts as
smoke tests only.
