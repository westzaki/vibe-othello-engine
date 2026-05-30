# Midgame TT Bucket Baseline

Status: historical baseline snapshot. Follow-up ideas in this snapshot describe
what looked useful at the time of collection; they are evidence, not current
instructions.

## Environment

- Date: 2026-05-24
- Base commit SHA at collection: 51ebd5282ebe1d4708ed3f6afc03660dec9bc889
- Measured tree: `codex/bucketed-midgame-tt` working tree with this PR's bucketed TT changes
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
  --exact-endgame-threshold 0
```

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 5,6 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --tt-entries 1024 \
  --pvs on \
  --exact-endgame-threshold 0
```

## Suite Results

| depth | mode | tt | pvs | tt entries | nodes | time ms | nodes/search | nps | tt hit % | tt overwrites | tt collisions | tt rejected stores | PVS scouts | PVS researches | PVS scout cutoffs | beta cutoffs | beta cut first move % | eval calls | searched moves | result checksum | work checksum | notes |
| ---: | :--- | :---: | :---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| 3 | iterative | on | on | 262144 | 11001 | 48.229 | 146.680 | 228099.078 | 6.327 | 2382 | 0 | 0 | 432 | 30 | 402 | 1536 | 72.266 | 10254 | 10695 | 4769913536460543414 | 8106253618459156446 | same result checksum as PR105 |
| 4 | iterative | on | on | 262144 | 30063 | 46.133 | 400.840 | 651661.692 | 7.205 | 7788 | 0 | 0 | 1515 | 90 | 1425 | 6771 | 78.068 | 27816 | 29592 | 16619357740453395049 | 7672591263279852813 | same result checksum as PR105 |
| 5 | iterative | on | on | 262144 | 113586 | 68.737 | 1514.480 | 1652484.488 | 8.481 | 23613 | 0 | 0 | 7914 | 324 | 7590 | 19941 | 72.935 | 103812 | 112746 | 7724537790029064038 | 17263458311163772656 | same result checksum as PR105 |
| 6 | iterative | on | on | 262144 | 277431 | 88.424 | 3699.080 | 3137524.167 | 7.999 | 60675 | 0 | 0 | 19335 | 612 | 18723 | 68556 | 79.232 | 254955 | 276084 | 4754957974613531568 | 4638116824541471188 | same result checksum as PR105; work checksum changed |
| 7 | iterative | on | on | 262144 | 956133 | 192.804 | 12748.440 | 4959093.172 | 7.917 | 162348 | 60 | 0 | 74286 | 1413 | 72873 | 180240 | 74.953 | 879726 | 953481 | 16982518219044581611 | 5977783844804353519 | same result checksum as PR105; work checksum changed |

## Tiny TT Stress

| depth | mode | tt | pvs | tt entries | nodes | time ms | nodes/search | nps | tt hit % | tt overwrites | tt collisions | tt rejected stores | PVS scouts | PVS researches | PVS scout cutoffs | beta cutoffs | beta cut first move % | eval calls | searched moves | result checksum | work checksum | notes |
| ---: | :--- | :---: | :---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| 5 | iterative | on | on | 1024 | 113592 | 16.653 | 1514.560 | 6821044.910 | 7.590 | 53961 | 32961 | 2853 | 7914 | 324 | 7590 | 19947 | 72.943 | 104829 | 112752 | 7724537790029064038 | 16217045021295476435 | bucketed table remains deterministic under pressure |
| 6 | iterative | on | on | 1024 | 278304 | 44.736 | 3710.720 | 6220972.194 | 6.102 | 158286 | 119292 | 40878 | 19335 | 612 | 18723 | 69150 | 79.275 | 261036 | 276957 | 4754957974613531568 | 17784310571229888363 | bucketed table remains deterministic under pressure |

## Comparison Against PR105 Direct-Mapped Snapshot

PR105 baseline: `docs/perf/baselines/2026-05-24-main-search-iterative-tt-pvs-comparison.md`.

| depth | PR105 nodes | bucket nodes | PR105 tt hit % | bucket tt hit % | PR105 collisions | bucket collisions | PR105 rejected stores | bucket rejected stores | result checksum |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| 3 | 11001 | 11001 | 6.327 | 6.327 | 6 | 0 | 3 | 0 | same |
| 4 | 30063 | 30063 | 7.205 | 7.205 | 27 | 0 | 6 | 0 | same |
| 5 | 113586 | 113586 | 8.473 | 8.481 | 222 | 0 | 105 | 0 | same |
| 6 | 277437 | 277431 | 7.987 | 7.999 | 1317 | 0 | 411 | 0 | same |
| 7 | 956256 | 956133 | 7.879 | 7.917 | 18024 | 60 | 6408 | 0 | same |

## Interpretation

- This PR changes only the midgame TT replacement policy. Default
  `SearchOptions` remain unchanged: TT and PVS are still opt-in.
- The sample best move, score, PV, and result checksum for the suite comparison
  matched the PR105 iterative+TT+PVS rows at all measured depths.
- Work checksum changed at depths 6 and 7, which is expected because the TT
  replacement policy changes the visited work trace even when the returned
  result is unchanged.
- With the default-sized TT, bucketed replacement eliminated rejected stores in
  this suite run and reduced collisions sharply. TT hit rate improved only
  slightly at deeper depths, so this is a robustness improvement rather than a
  clear strength/default-setting justification by itself.
- The tiny TT stress run still shows substantial collision pressure and rejected
  stores, giving a useful regression surface for replacement policy changes.

## Historical Follow-up Candidate

At the time of this snapshot, the suggested follow-up was to consider one
focused change if the bucketed table remained stable in CI and additional local
runs: aspiration windows for iterative search, or a stats-driven move-ordering
cleanup. The snapshot advised not combining those with evaluation tuning or
selective pruning.
