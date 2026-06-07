# TT Shallow Policy Ablation

Date: 2026-06-07

## Hypothesis

Depth-0 and shallow midgame transposition-table probe/store traffic may cost more wall-clock time
than it saves, especially leaf stores. This experiment keeps the default policy unchanged and adds
options to measure shallower TT policies without changing evaluation, move legality, exact solving,
or pruning semantics.

## Build and Inputs

- Base commit: `1526144affd94d0972784884c884107fb2d0b44a`
- Measured worktree: Step 2 option/instrumentation changes on top of the base commit
- Build: `CMAKE_BUILD_TYPE=Release`, `-O3 -DNDEBUG`
- Machine: macOS arm64, Darwin 24.6.0
- Raw output: `runs/perf/step2-*.jsonl` and `runs/perf/step2-suite-*.jsonl` (gitignored)

## Commands

```sh
cmake --build build
./build/othello_tests "[search]"
./build/othello_tests "[match-runner]"
ctest --test-dir build --output-on-failure

./build/othello_search_bench --mode fixed --depths 5,6,7,8 --positions smoke --repetitions 3 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --format jsonl
./build/othello_search_bench --mode fixed --depths 5,6,7,8 --positions smoke --repetitions 3 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --tt-store-leaf off --format jsonl
./build/othello_search_bench --mode fixed --depths 5,6,7,8 --positions smoke --repetitions 3 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --tt-min-probe-depth 1 --tt-min-store-depth 1 --format jsonl
./build/othello_search_bench --mode fixed --depths 5,6,7,8 --positions smoke --repetitions 3 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --tt-min-probe-depth 2 --tt-min-store-depth 1 --format jsonl

./build/othello_search_bench --mode fixed --depths 5,6,7,8 --positions suite --repetitions 2 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --format jsonl
./build/othello_search_bench --mode fixed --depths 5,6,7,8 --positions suite --repetitions 2 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --tt-store-leaf off --format jsonl
./build/othello_search_bench --mode fixed --depths 5,6,7,8 --positions suite --repetitions 2 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --tt-min-probe-depth 1 --tt-min-store-depth 1 --format jsonl
./build/othello_search_bench --mode fixed --depths 5,6,7,8 --positions suite --repetitions 2 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --tt-min-probe-depth 2 --tt-min-store-depth 1 --format jsonl
```

## Variants

| variant | options |
| --- | --- |
| baseline | defaults: `store_leaf_tt_entries=true`, `tt_min_probe_depth=0`, `tt_min_store_depth=0` |
| A | `--tt-store-leaf off` |
| B | `--tt-min-probe-depth 1 --tt-min-store-depth 1` |
| C | `--tt-min-probe-depth 2 --tt-min-store-depth 1` |

## Suite Results

All suite rows kept the same aggregate best move, score, and result checksum as baseline.

| depth | variant | elapsed ms | vs baseline | nodes | TT lookups | TT stores | leaf stores | probe skip | store skip |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 | baseline | 169.431 | 0.0% | 65,168 | 65,168 | 58,862 | 43,032 | 0 | 0 |
| 5 | A | 186.099 | +9.8% | 65,168 | 65,168 | 15,830 | 0 | 0 | 0 |
| 5 | B | 142.315 | -16.0% | 65,168 | 17,964 | 15,830 | 0 | 47,204 | 47,204 |
| 5 | C | 138.094 | -18.5% | 68,270 | 6,536 | 17,136 | 0 | 61,734 | 50,306 |
| 6 | baseline | 185.909 | 0.0% | 154,186 | 154,186 | 140,080 | 87,266 | 0 | 0 |
| 6 | A | 241.086 | +29.7% | 154,186 | 154,186 | 52,814 | 0 | 0 | 0 |
| 6 | B | 216.585 | +16.5% | 154,186 | 59,458 | 52,814 | 0 | 94,728 | 94,728 |
| 6 | C | 167.548 | -9.9% | 162,248 | 16,848 | 57,402 | 0 | 145,400 | 102,828 |
| 7 | baseline | 515.458 | 0.0% | 598,400 | 598,400 | 542,904 | 394,312 | 0 | 0 |
| 7 | A | 601.174 | +16.6% | 598,400 | 598,400 | 148,592 | 0 | 0 | 0 |
| 7 | B | 500.355 | -2.9% | 598,400 | 167,488 | 148,592 | 0 | 430,912 | 430,912 |
| 7 | C | 455.476 | -11.6% | 627,666 | 64,772 | 159,422 | 0 | 562,894 | 460,172 |
| 8 | baseline | 1086.130 | 0.0% | 1,356,020 | 1,356,020 | 1,239,592 | 774,740 | 0 | 0 |
| 8 | A | 1070.400 | -1.4% | 1,356,020 | 1,356,020 | 464,852 | 0 | 0 | 0 |
| 8 | B | 959.462 | -11.7% | 1,356,020 | 519,848 | 464,852 | 0 | 836,172 | 836,172 |
| 8 | C | 1014.400 | -6.6% | 1,423,944 | 150,956 | 503,408 | 0 | 1,272,988 | 903,658 |

## Smoke Results

All smoke rows also kept the same aggregate best move, score, and result checksum as baseline.
The smoke set is very small, so wall-clock rows below 100 ms should be treated as noisy.

| depth | best variant by elapsed | elapsed change | note |
| ---: | --- | ---: | --- |
| 5 | baseline | 0.0% | A/B/C were slower on this short run |
| 6 | B | -2.9% | same nodes and checksum as baseline |
| 7 | baseline | 0.0% | A/B/C were slower on this short run |
| 8 | B | -0.5% | same nodes and checksum as baseline |

## Decision

Keep defaults unchanged. The ablation hooks are useful and safe enough to keep:

- `--tt-store-leaf off` alone reduces stores but usually worsened wall-clock, likely because leaf TT
  hits are still probed and then missed.
- `--tt-min-probe-depth 1 --tt-min-store-depth 1` is the cleanest candidate: same node count as
  baseline in the measured suite rows, same result checksum, and depth-8 suite improved by 11.7%.
- `--tt-min-probe-depth 2 --tt-min-store-depth 1` can improve wall-clock despite higher node count,
  but it is less stable and should remain an experiment setting for now.

Next step before changing any preset: run the B/C variants on broader regression and tactical
position sets, including iterative-search rows and match-runner traces.
