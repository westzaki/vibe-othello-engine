# Main Search Iterative TT PVS Comparison

Status: local numbers collected.

## Environment

- Date: 2026-05-24
- Commit SHA: 3e91b8f6106a910b53af0b5f12b85deae69b73ff
- Machine: MasatoshinoMacBook-Air.local, arm64 Apple Silicon MacBook Air
- OS: Darwin 24.6.0
- Compiler: Apple clang 17.0.0 (clang-1700.0.13.5)
- Build type: Release
- Exact endgame threshold: 0

CPU model note: `sysctl -n machdep.cpu.brand_string` was unavailable in this
sandbox, so the machine line records the host and architecture reported by
`uname -a`.

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
  --mode fixed \
  --depths 3,4,5,6,7 \
  --positions suite \
  --repetitions 3 \
  --tt off \
  --pvs off \
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
  --exact-endgame-threshold 0
```

## Results

| depth | mode | tt | pvs | nodes | time ms | nodes/search | nps | tt hit % | PVS scouts | PVS researches | PVS scout cutoffs | beta cutoffs | beta cut first move % | eval calls | searched moves | result checksum | work checksum | notes |
| ---: | :--- | :---: | :---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| 3 | fixed | off | off | 10005 | 1.162 | 133.400 | 8607073.543 | 0.000 | 0 | 0 | 0 | 909 | 67.987 | 9984 | 9909 | 10756326307094541830 | 10519234209852117885 | plain fixed-depth baseline |
| 3 | iterative | on | on | 11001 | 44.753 | 146.680 | 245817.758 | 6.327 | 432 | 30 | 402 | 1536 | 72.266 | 10254 | 10695 | 4769913536460543414 | 8106253618459156446 | sample best move differs from fixed (`f6` vs `d6`) |
| 4 | fixed | off | off | 20820 | 3.290 | 277.600 | 6328829.185 | 0.000 | 0 | 0 | 0 | 5700 | 81.263 | 20784 | 20709 | 2267285456986049860 | 10943463697994852672 | plain fixed-depth baseline |
| 4 | iterative | on | on | 30063 | 44.416 | 400.840 | 676843.690 | 7.205 | 1515 | 90 | 1425 | 6771 | 78.068 | 27816 | 29592 | 16619357740453395049 | 7672591263279852813 | same sample best move and score as fixed |
| 5 | fixed | off | off | 95016 | 12.242 | 1266.880 | 7761609.392 | 0.000 | 0 | 0 | 0 | 12690 | 71.300 | 94941 | 94866 | 15199578951113932930 | 11968181221187283878 | plain fixed-depth baseline |
| 5 | iterative | on | on | 113586 | 62.510 | 1514.480 | 1817082.854 | 8.473 | 7914 | 324 | 7590 | 19941 | 72.935 | 103821 | 112746 | 7724537790029064038 | 17263458311163772656 | sample best move differs from fixed (`f7` vs `d2`) |
| 6 | fixed | off | off | 210645 | 34.279 | 2808.600 | 6144926.268 | 0.000 | 0 | 0 | 0 | 62397 | 83.095 | 210438 | 210363 | 8691423865958320729 | 10147373326126678456 | plain fixed-depth baseline |
| 6 | iterative | on | on | 277437 | 90.307 | 3699.160 | 3072143.976 | 7.987 | 19335 | 612 | 18723 | 68562 | 79.233 | 254994 | 276090 | 4754957974613531568 | 7608175170768995054 | same sample best move and score as fixed |
| 7 | fixed | off | off | 833172 | 109.651 | 11108.960 | 7598381.231 | 0.000 | 0 | 0 | 0 | 121863 | 75.894 | 832686 | 832611 | 2113316818183450510 | 2546440097601498293 | plain fixed-depth baseline |
| 7 | iterative | on | on | 956256 | 182.768 | 12750.080 | 5232086.372 | 7.879 | 74286 | 1413 | 72873 | 180261 | 74.951 | 880203 | 953604 | 16982518219044581611 | 9893958666564314422 | sample best move differs from fixed (`d6` vs `c2`) |

## Interpretation

- This is a preset comparison, not a pure speed comparison. The sample best move
  differs at depths 3, 5, and 7, so behavior differs for at least the first
  sampled suite position.
- In this local run, iterative+TT+PVS searched more total nodes than fixed plain
  at every measured depth because iterative deepening includes all shallower
  iterations.
- TT hit rate was roughly 6-8.5% for the stronger preset with the current
  direct-mapped table.
- PVS scout cutoffs dominated PVS re-searches, but the current preset did not
  reduce total node count in this aggregate suite measurement.
- `beta_cut_first_move_pct` stayed in the same broad range as fixed plain. The
  new counters are useful for future move-ordering and TT replacement work, but
  this snapshot does not by itself justify changing defaults.

## Follow-up Candidate

Use these measurements to choose one focused search improvement in a later PR,
for example TT replacement/bucketization, aspiration windows for iterative
search, or a stats-driven move-ordering cleanup. Do not combine those changes
with this baseline snapshot.
