# Midgame Ordering Cleanup Baseline

Status: local numbers collected.

## Environment

- Date: 2026-05-24
- Base commit SHA at collection: ecdda225fefa98b8c06fcd0b4dd0969724d75427
- Measured tree: `codex/midgame-ordering-pass-bonus` working tree with this PR's ordering changes
- Machine: local arm64 macOS development machine
- OS: Darwin 24.6.0
- Compiler: Apple clang 17.0.0 (clang-1700.0.13.5)
- Build type: Release
- Exact endgame threshold: 0

Machine note: hostnames and other personally identifying machine details are
intentionally omitted from this snapshot.

## Commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

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
  --depths 3,4,5,6,7 \
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
  --depths 8,9,10 \
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
  --depths 7,8,9 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --exact-endgame-threshold 0
```

## Aspiration Window 50 vs PR107

PR107 comparison values come from
`docs/perf/baselines/2026-05-24-pr107-iterative-aspiration.md`.

| depth | PR107 nodes | this PR nodes | node delta | time ms | nodes/search | nps | tt hit % | PVS scouts | PVS researches | PVS scout cutoffs | beta cut first move % | asp searches | asp researches | fail lows | fail highs | fallbacks | result checksum | work checksum |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3 | 11031 | 11031 | 0 | 49.043 | 147.080 | 224923.731 | 6.418 | 432 | 30 | 402 | 72.374 | 150 | 15 | 0 | 15 | 3 | 4769913536460543414 | 8803961593881438778 |
| 4 | 30036 | 30036 | 0 | 50.506 | 400.480 | 594704.563 | 7.281 | 1515 | 90 | 1425 | 78.122 | 225 | 15 | 0 | 15 | 3 | 16619357740453395049 | 7658640848064503121 |
| 5 | 113529 | 113529 | 0 | 74.705 | 1513.720 | 1519688.140 | 8.506 | 7914 | 324 | 7590 | 72.954 | 300 | 15 | 0 | 15 | 3 | 7724537790029064038 | 15058362422385365073 |
| 6 | 275907 | 275907 | 0 | 97.973 | 3678.760 | 2816148.659 | 8.004 | 19197 | 609 | 18588 | 79.195 | 375 | 15 | 0 | 15 | 3 | 4754957974613531568 | 10754401905056236659 |
| 7 | 954399 | 954453 | 54 | 211.969 | 12726.040 | 4502799.660 | 7.922 | 74118 | 1410 | 72708 | 74.917 | 450 | 18 | 3 | 15 | 3 | 16982518219044581611 | 11787305808476773479 |
| 8 | 2385984 | 2385996 | 12 | 521.529 | 31813.280 | 4575005.240 | 8.017 | 177150 | 2739 | 174411 | 80.376 | 525 | 33 | 18 | 15 | 6 | 12115017075483172927 | 14320305496742950782 |
| 9 | 7909191 | 7909077 | -114 | 1539.959 | 105454.360 | 5135902.411 | 7.769 | 629457 | 5238 | 624219 | 76.620 | 600 | 48 | 33 | 15 | 9 | 9628926587309888703 | 10839843933518662007 |
| 10 | 18073935 | 18074286 | 351 | 3611.280 | 240990.480 | 5004952.699 | 7.437 | 1384371 | 8310 | 1376061 | 82.888 | 675 | 78 | 63 | 15 | 15 | 8162574130287206337 | 4781402927604691199 |

## Full-Window Iterative TT/PVS

| depth | PR107 nodes | this PR nodes | node delta | time ms | nodes/search | nps | tt hit % | PVS scouts | PVS researches | PVS scout cutoffs | beta cut first move % | result checksum | work checksum |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 7 | 956133 | 956187 | 54 | 221.323 | 12749.160 | 4320315.550 | 7.919 | 74283 | 1416 | 72867 | 74.952 | 16982518219044581611 | 6072700354194788415 |
| 8 | 2399703 | 2399715 | 12 | 517.764 | 31996.200 | 4634770.141 | 8.038 | 178035 | 2802 | 175233 | 80.378 | 12115017075483172927 | 7041630093966608332 |
| 9 | 7942137 | 7942023 | -114 | 1569.428 | 105893.640 | 5060458.403 | 7.785 | 631341 | 5325 | 626016 | 76.719 | 9628926587309888703 | 14566117298975346265 |

## Interpretation

- This PR adds only a non-root dynamic ordering bonus for moves that force a
  real opponent pass while leaving the side to move with a legal move after the
  pass.
- Result checksums matched PR107 for all comparable suite rows, including the
  deeper depth 8-10 aspiration baseline. Best move, score, and sampled PV
  therefore remained stable in these aggregate benchmark rows.
- Work checksums changed at depths where the new ordering signal affected the
  searched path. This is expected for an ordering-only change.
- The aggregate node effect was nearly neutral in this suite snapshot:
  aspiration depth 9 improved by 114 nodes, while depths 7, 8, and 10 regressed
  by 54, 12, and 351 nodes respectively. These deltas are tiny relative to the
  total work.
- PVS scout counts improved slightly at depths 7-9 under aspiration, but depth
  10 regressed slightly. Aspiration fail-low, fail-high, and fallback counts did
  not change from PR107.
- Local experiments enabling dynamic ordering at the root and raising the
  opponent mobility penalty were rejected because they changed result checksums
  for comparable iterative TT/PVS aspiration rows.

## Follow-up Candidate

The opponent-pass signal is correctness-safe but not a clear aggregate strength
or speed win in this suite snapshot. A focused follow-up should either continue
with one isolated ordering experiment backed by the same depth 8-10 baseline, or
move to evaluation breakdown instrumentation before changing `evaluate_basic`.
