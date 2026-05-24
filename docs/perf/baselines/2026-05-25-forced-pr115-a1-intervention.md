# Forced PR115 A1 Intervention

Status: local diagnostic intervention collected.

## Environment

- Date: 2026-05-25 JST
- Branch: `codex/forced-move-intervention`
- Base branch: `origin/main`
- Base/head comparison target: PR109 basic evaluator
- PR109 base commit SHA: `c4c7d0022c0a091bb2694d4962bcd8b4c117912b`
- Head evaluator: unchanged `origin/main` phase-aware evaluator
- Build type: Release
- Openings: `data/openings/smoke_openings.txt`
- Games: 40
- Depth: 8
- Seed: `20260524`
- External timeout: 30000 ms

This snapshot does not change evaluation weights, evaluator semantics, search
semantics, TT/PVS/aspiration behavior, or exact endgame behavior. It uses an
opt-in NBoard wrapper to force one diagnostic move only when a target board is
reached.

## Intervention Workflow

Added diagnostic wrapper:

```text
tools/scripts/forced_move_nboard_wrapper.py
```

The wrapper launches a child NBoard engine and proxies the normal line protocol.
It tracks the current board by replaying each `set game` command. On `go`, if
the tracked board exactly matches the configured target board and the configured
move is legal, it returns that move directly:

```text
=== a1
```

Otherwise it forwards the command to the child engine unchanged.

The PR115 target board is committed as:

```text
tools/scripts/fixtures/pr115_divergence_board.txt
```

```text
........
........
........
...BW...
..BBW...
..BWW...
.B......
........
side=W
```

## Command

The generated engine config used head through the forced-move wrapper and base
as the PR109 NBoard engine:

```text
head|8|/Users/mnishizaki/.codex/worktrees/d24f/vibe-othello-engine|python3|tools/scripts/forced_move_nboard_wrapper.py|--target-board-file|tools/scripts/fixtures/pr115_divergence_board.txt|--force-move|a1|--|build/othello_nboard_engine
base|8|/tmp/vibe-othello-base-pr109|/tmp/vibe-othello-base-pr109/build/othello_nboard_engine
```

Match command:

```sh
./build/othello_match_runner \
  --black external:head \
  --white external:base \
  --games 40 \
  --swap-sides true \
  --seed 20260524 \
  --openings data/openings/smoke_openings.txt \
  --engines runs/base-head/forced-pr115-a1-depth8/depth-8/engines.generated.txt \
  --external-timeout-ms 30000 \
  --output runs/base-head/forced-pr115-a1-depth8/depth-8/match.jsonl \
  --quiet
```

## Result

| run | depth | games | head wins | base wins | draws | avg diff from head | errors |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| PR112 phase-aware v1 | 8 | 40 | 0 | 40 | 0 | -12.00 | 0 |
| forced PR115 `a1` | 8 | 40 | 20 | 20 | 0 | 19.00 | 0 |

Per-opening rows:

| opening | games | head wins | base wins | draws | avg diff |
| :--- | ---: | ---: | ---: | ---: | ---: |
| initial | 14 | 7 | 7 | 0 | 19.00 |
| c4-c3 | 14 | 7 | 7 | 0 | 19.00 |
| d3-c3-c4 | 12 | 6 | 6 | 0 | 19.00 |

Average time:

| player | average time ms |
| :--- | ---: |
| forced head wrapper | 418.07 |
| PR109 base | 183.55 |

The wrapper adds process/proxy overhead, so this timing should not be read as an
engine performance comparison.

## Divergence Check

After forcing `a1`, first divergences move to the next ply:

| ply | side | head move | base move | preceding moves |
| ---: | :--- | :--- | :--- | :--- |
| 6 | black | `f4` | `c2` | `d3 c3 c4 e3 b2 a1` |

All 20 swap-side pairs had `a1` in the common preceding move list before the
next divergence. This confirms the wrapper forced the intended move at the
PR115 board.

## Verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m unittest discover tools/scripts/tests
python3 -m py_compile tools/scripts/forced_move_nboard_wrapper.py
```

Results:

- `ctest`: 141/141 tests passed
- Python unittest: 73 tests passed
- Python compile: passed
- Tiny wrapper smoke: initial board delegated to child engine (`d3` at depth 1);
  PR115 target board returned forced `a1`

## Interpretation

Classification: clear causal diagnostic.

Forcing White `a1` at the PR115 divergence board repairs the depth-8 smoke
matrix from `0-40 / -12.00` to `20-20 / +19.00` without changing evaluator or
search behavior. This strongly supports the hypothesis that the PR115 immediate
corner decision is a primary cause of the observed depth-8 regression on the
smoke openings.

This does not prove that any specific evaluation feature change is correct. It
does show that a future evaluation PR should be judged by whether it naturally
chooses or materially improves `a1` at this divergence board while preserving
depth-8 and depth-10 matrix behavior.

## Recommendation

Keep this workflow as diagnostic tooling. Do not merge any forced move policy
into engine behavior.

Next evaluation PR should make one real evaluator semantics change and rerun:

- PR115 root-candidate analysis at depth 8 and 10;
- depth-8 matrix against PR109;
- depth-10 matrix against PR109.

The strongest remaining suspects are still the interaction between immediate
corner capture, X-square danger, corner access, potential mobility, and frontier.
