# Iterative Aspiration Baseline

Status: historical baseline snapshot. Follow-up ideas in this snapshot describe
what looked useful at the time of collection; they are evidence, not current
instructions.

## Environment

- Date: 2026-05-24
- Base commit SHA at collection: cbf4991683a801b275498bedc83b45a054aeb113
- Measured tree: `codex/iterative-aspiration-window` working tree with this PR's aspiration changes
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

## Full-Window Iterative TT/PVS

| depth | asp | nodes | time ms | nodes/search | nps | tt hit % | tt overwrites | tt collisions | tt rejected stores | PVS scouts | PVS researches | PVS scout cutoffs | beta cut first move % | asp searches | asp researches | fail lows | fail highs | fallbacks | result checksum | work checksum |
| ---: | :---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3 | off | 11001 | 82.616 | 146.680 | 133157.488 | 6.327 | 2382 | 0 | 0 | 432 | 30 | 402 | 72.266 | 0 | 0 | 0 | 0 | 0 | 4769913536460543414 | 8106253618459156446 |
| 4 | off | 30063 | 81.133 | 400.840 | 370539.160 | 7.205 | 7788 | 0 | 0 | 1515 | 90 | 1425 | 78.068 | 0 | 0 | 0 | 0 | 0 | 16619357740453395049 | 7672591263279852813 |
| 5 | off | 113586 | 88.153 | 1514.480 | 1288504.269 | 8.481 | 23613 | 0 | 0 | 7914 | 324 | 7590 | 72.935 | 0 | 0 | 0 | 0 | 0 | 7724537790029064038 | 17263458311163772656 |
| 6 | off | 277431 | 160.700 | 3699.080 | 1726394.368 | 7.999 | 60675 | 0 | 0 | 19335 | 612 | 18723 | 79.232 | 0 | 0 | 0 | 0 | 0 | 4754957974613531568 | 4638116824541471188 |
| 7 | off | 956133 | 302.002 | 12748.440 | 3165981.034 | 7.917 | 162348 | 60 | 0 | 74286 | 1413 | 72873 | 74.953 | 0 | 0 | 0 | 0 | 0 | 16982518219044581611 | 5977783844804353519 |
| 8 | off | 2399703 | 707.755 | 31996.040 | 3390584.514 | 8.038 | 446415 | 3135 | 54 | 178038 | 2799 | 175239 | 80.377 | 0 | 0 | 0 | 0 | 0 | 12115017075483172927 | 9368948679848761409 |
| 9 | off | 7942137 | 2413.676 | 105895.160 | 3290473.763 | 7.785 | 1637835 | 456129 | 12900 | 631359 | 5322 | 626037 | 76.719 | 0 | 0 | 0 | 0 | 0 | 9628926587309888703 | 12143850752691977430 |
| 10 | off | 18312816 | 7870.603 | 244170.880 | 2326736.010 | 7.454 | 6667335 | 3885975 | 410151 | 1399797 | 8823 | 1390974 | 82.677 | 0 | 0 | 0 | 0 | 0 | 8162574130287206337 | 797389258160049964 |

## Aspiration Window 50

| depth | asp | nodes | time ms | nodes/search | nps | tt hit % | tt overwrites | tt collisions | tt rejected stores | PVS scouts | PVS researches | PVS scout cutoffs | beta cut first move % | asp searches | asp researches | fail lows | fail highs | fallbacks | result checksum | work checksum |
| ---: | :---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3 | on | 11031 | 51.482 | 147.080 | 214269.240 | 6.418 | 2400 | 0 | 0 | 432 | 30 | 402 | 72.374 | 150 | 15 | 0 | 15 | 3 | 4769913536460543414 | 8803961593881438778 |
| 4 | on | 30036 | 53.571 | 400.480 | 560677.794 | 7.281 | 7788 | 0 | 0 | 1515 | 90 | 1425 | 78.122 | 225 | 15 | 0 | 15 | 3 | 16619357740453395049 | 7658640848064503121 |
| 5 | on | 113529 | 188.775 | 1513.720 | 601398.758 | 8.506 | 23607 | 0 | 0 | 7914 | 324 | 7590 | 72.954 | 300 | 15 | 0 | 15 | 3 | 7724537790029064038 | 15058362422385365073 |
| 6 | on | 275907 | 114.974 | 3678.760 | 2399726.903 | 8.004 | 60600 | 0 | 0 | 19197 | 609 | 18588 | 79.195 | 375 | 15 | 0 | 15 | 3 | 4754957974613531568 | 10754401905056236659 |
| 7 | on | 954399 | 313.144 | 12725.320 | 3047792.252 | 7.919 | 162150 | 60 | 0 | 74121 | 1407 | 72714 | 74.918 | 450 | 18 | 3 | 15 | 3 | 16982518219044581611 | 13156752647507988631 |
| 8 | on | 2385984 | 956.813 | 31813.120 | 2493677.842 | 8.016 | 445026 | 2910 | 51 | 177153 | 2736 | 174417 | 80.375 | 525 | 33 | 18 | 15 | 6 | 12115017075483172927 | 5034028228088796457 |
| 9 | on | 7909191 | 3231.258 | 105455.880 | 2447712.372 | 7.769 | 1623522 | 442932 | 11979 | 629475 | 5235 | 624240 | 76.619 | 600 | 48 | 33 | 15 | 9 | 9628926587309888703 | 8170970776368498439 |
| 10 | on | 18073935 | 7947.142 | 240985.800 | 2274268.572 | 7.436 | 6505440 | 3720234 | 370971 | 1384179 | 8307 | 1375872 | 82.886 | 675 | 78 | 63 | 15 | 15 | 8162574130287206337 | 4817286292970261463 |

## Wider Window 100

| depth | asp window | nodes | time ms | nodes/search | nps | tt hit % | tt overwrites | tt collisions | tt rejected stores | PVS scouts | PVS researches | PVS scout cutoffs | beta cut first move % | asp searches | asp researches | fail lows | fail highs | fallbacks | result checksum | work checksum |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 100 | 2399817 | 784.205 | 31997.560 | 3060192.193 | 8.039 | 446376 | 3135 | 54 | 178050 | 2796 | 175254 | 80.377 | 525 | 30 | 15 | 15 | 6 | 12115017075483172927 | 1819639425010070440 |
| 9 | 100 | 7942569 | 2251.498 | 105900.920 | 3527681.949 | 7.786 | 1638024 | 456129 | 12900 | 631488 | 5328 | 626160 | 76.719 | 600 | 45 | 30 | 15 | 9 | 9628926587309888703 | 2597756935550839541 |

## Narrow Window Stress

| depth | asp window | max researches | nodes | time ms | nodes/search | nps | tt hit % | tt overwrites | tt collisions | tt rejected stores | PVS scouts | PVS researches | PVS scout cutoffs | beta cut first move % | asp searches | asp researches | fail lows | fail highs | fallbacks | result checksum | work checksum |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 | 1 | 2 | 128052 | 83.519 | 1707.360 | 1533198.834 | 12.897 | 29841 | 0 | 0 | 9648 | 369 | 9279 | 73.032 | 300 | 816 | 396 | 420 | 267 | 7724537790029064038 | 7102880196930691029 |
| 6 | 1 | 2 | 318360 | 126.441 | 4244.800 | 2517847.546 | 12.410 | 76218 | 0 | 0 | 25596 | 636 | 24960 | 79.894 | 375 | 1023 | 597 | 426 | 330 | 4754957974613531568 | 538285822737518174 |

## Interpretation

- Aspiration is opt-in and only affects `search_iterative()`. Fixed-depth
  `search()` remains full-window.
- Result checksums matched the full-window iterative TT/PVS rows at every
  measured depth, including the deeper depth 8-10 pass, so sample best move,
  score, and PV were preserved in this suite run.
- Work checksums changed in all aspiration rows, as expected: aspiration changes
  the verified search path and records additional fail-high/fallback work.
- With the default window of 50, aggregate nodes decreased at depths 4-10 and
  increased slightly at depth 3 in this run. The deeper pass showed the node
  reduction more clearly, but wall-clock time was still noisy locally, so treat
  time numbers as a directional snapshot rather than a stable speed claim.
- A wider window of 100 matched result checksums at depths 8-9 but kept node
  counts much closer to the full-window run; it reduced some aspiration
  re-search pressure without clearly improving work in this suite snapshot.
- Fail-high dominated the default-window run. The narrow-window stress produced
  both fail-low and fail-high outcomes plus many full-window fallbacks, while
  still preserving result checksum.

## Historical Follow-up Candidate

At the time of this snapshot, the suggested follow-up was to keep aspiration
opt-in while gathering more match and per-position data if it remained
result-stable across more positions and CI. A focused follow-up candidate was
stats-driven move ordering cleanup, especially around fail-high depth and
first-move cutoff behavior.
