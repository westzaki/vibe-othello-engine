# Strong-v1 Main Baseline

Status: strength and speed baseline for `strong-v1` on latest `main`.

This snapshot records the current `strong-v1` position before nearby engine
improvement work. It is a measurement-only baseline: no engine, evaluator,
tooling, or documentation behavior was changed to produce these numbers.

Measured commit:

```text
5d03b7b85f8b9ba301c504dbbb97b1726d157d04
5d03b7b build: allow core tests without experiment tools (#278)
```

## Commands

All commands were run from the repository root on a Release build.

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j 8
ctest --test-dir build/release --output-on-failure

./build/release/othello_search_bench \
  --mode iterative \
  --depths 5,6,7,8 \
  --positions suite \
  --repetitions 1 \
  --tt on \
  --pvs on \
  --aspiration on \
  --aspiration-profile score-delta-aware \
  --exact-endgame-threshold adaptive16 \
  --by-position \
  --emit-iterative-depth-rows \
  --eval-config data/eval/current_default.eval \
  --format jsonl > build/release/strong-v1-main-search-suite.jsonl

./build/release/othello_endgame_bench \
  --positions endgame \
  --empties 14,16,18,20 \
  --repetitions 1 \
  --format jsonl > build/release/strong-v1-main-endgame-14-20.jsonl
```

The external Ntest match used a local Ntest build configured through a temporary
`othello_match_runner` engine config. Ntest first failed while trying to open
`JA_s8.book`. The local Ntest runtime had a different book depth available, so
the match was rerun with a temporary `parameters.txt` whose book flag was set to
`0` for both computer slots. No repository files were changed for that runtime.

```sh
./build/release/othello_match_runner \
  --black search:depth=5,preset=strong-v1,eval_config=data/eval/current_default.eval \
  --white external:ntest8 \
  --games 40 \
  --swap-sides true \
  --seed 20260606 \
  --openings data/openings/smoke_openings.txt \
  --engines build/release/ntest8.engines.txt \
  --external-timeout-ms 30000 \
  --output build/release/strong-v1-vs-ntest8-match.jsonl \
  --format jsonl \
  --quiet

python3 tools/scripts/match_summary.py \
  --input build/release/strong-v1-vs-ntest8-match.jsonl \
  --by-opening
```

## Verification

Release build succeeded.

`ctest` failed: 309 of 328 tests passed. The remaining 19 failures were not
from this baseline artifact. The visible failure cluster was default evaluator
resolution from the CTest working directory:

```text
project default eval config not found: data/eval/current_default.eval
```

The search and match commands above pass
`--eval-config data/eval/current_default.eval` explicitly.

## Search Suite

`othello_search_bench` was run with iterative search, TT on, PVS on,
score-delta-aware aspiration, `adaptive16`, and `--by-position`.

The `PVS scouts/research` column is always `scouts / research`.

| depth | nodes | time ms | nps | beta_cut_first_move_pct | TT hit | TT stores | TT collisions | TT rejected stores | PVS scouts/research | eval_calls | exact root fired |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 | 203,711 | 73.131 | 2,785,573 | 68.05 | 12.54% | 58,245 | 2 | 0 | 2,578 / 89 | 31,815 | 4 |
| 6 | 262,592 | 95.357 | 2,753,765 | 74.54 | 10.81% | 111,910 | 2 | 0 | 6,796 / 192 | 85,432 | 4 |
| 7 | 490,876 | 162.101 | 3,028,219 | 69.95 | 9.45% | 320,329 | 42 | 0 | 25,644 / 412 | 293,790 | 4 |
| 8 | 1,019,270 | 303.106 | 3,362,748 | 75.34 | 8.66% | 805,697 | 2,040 | 24 | 63,126 / 756 | 778,890 | 4 |

Current search JSONL reports TT hit/store/collision/rejected-store counters, but
does not report search TT bound-cut fields or hashfull. Those were therefore not
measured for the search bench.

### Slow Search Positions

Sorted by elapsed time across all requested depths.

| depth | position | time ms | nodes | eval_calls | beta_cut_first_move_pct | TT hit | PVS scouts/research | best move initial rank |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | `opening-a1-access` | 34.756 | 98,361 | 90,584 | 48.53 | 7.91% | 6,584 / 97 | 5 |
| 8 | `midgame-x-risk` | 31.166 | 104,113 | 96,124 | 85.28 | 7.67% | 7,260 / 76 | 7 |
| 8 | `midgame-wide-x-risk` | 24.834 | 91,302 | 79,634 | 78.65 | 12.72% | 7,301 / 121 | 6 |
| 8 | `midgame-lopsided-edge` | 19.871 | 62,092 | 56,032 | 67.19 | 9.76% | 4,616 / 83 | 8 |
| 7 | `late-black-pass` | 19.443 | 147,118 | 0 | 0.00 | 15.61% | 0 / 0 | - |
| 8 | `early-balanced-mobility` | 18.859 | 52,102 | 47,214 | 56.07 | 9.38% | 3,576 / 66 | 2 |
| 5 | `late-black-pass` | 18.850 | 147,118 | 0 | 0.00 | 15.61% | 0 / 0 | - |
| 6 | `late-black-pass` | 18.476 | 147,118 | 0 | 0.00 | 15.61% | 0 / 0 | - |
| 8 | `late-black-pass` | 18.447 | 147,118 | 0 | 0.00 | 15.61% | 0 / 0 | - |
| 8 | `late-corner-swing` | 17.923 | 54,811 | 51,232 | 81.84 | 6.52% | 4,320 / 49 | 4 |

## Exact Endgame

`othello_endgame_bench` was run on the endgame fixture set with 14, 16, 18, and
20 empties.

`TT cut lower+upper` is the sum of lower-bound and upper-bound TT hits reported
by exact endgame stats. Current exact-endgame JSONL does not emit hashfull, so
hashfull was not measured.

| empties | positions | nodes | time ms | nps | TT hit | TT exact hits | TT cut lower+upper | TT stores | TT collisions | TT rejected stores |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 14 | 13 | 8,224,588 | 575.713 | 14,285,927 | 16.94% | 2,999 | 181,158 | 902,744 | 1,192 | 72 |
| 16 | 10 | 24,499,960 | 1,889.995 | 12,962,977 | 11.08% | 1,187 | 421,420 | 3,379,604 | 132,873 | 13,214 |
| 18 | 10 | 153,377,136 | 14,893.565 | 10,298,215 | 8.96% | 4,303 | 2,279,915 | 18,284,371 | 9,302,034 | 4,913,007 |
| 20 | 9 | 150,718,710 | 19,883.637 | 7,580,037 | 6.47% | 3,349 | 1,782,447 | 16,582,316 | 8,814,671 | 9,245,313 |

The 18/20-empty rows show the clearest TT pressure: hit rate drops below 9%,
while collisions and rejected stores reach millions.

### Slow Endgame Positions

| empties | position | time ms | nodes | nps | best | margin | TT hit | TT collisions | TT rejected stores |
| ---: | --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: |
| 20 | `20-empty-high-mobility-lite` | 7,330.840 | 52,680,535 | 7,186,150 | h5 | 12 | 4.49% | 2,798,277 | 5,624,910 |
| 20 | `20-empty-edge-heavy-low-branching` | 4,201.810 | 31,835,958 | 7,576,720 | a7 | -55 | 7.52% | 1,865,485 | 1,814,319 |
| 18 | `18-empty-parity-ish` | 2,672.060 | 31,326,565 | 11,723,700 | e1 | 16 | 7.42% | 1,752,498 | 1,382,254 |
| 18 | `18-empty-opponent-pass-after-move` | 2,662.930 | 20,257,769 | 7,607,330 | a1 | 62 | 8.41% | 1,331,676 | 805,814 |
| 20 | `20-empty-low-mobility` | 2,548.010 | 16,182,203 | 6,350,910 | h3 | -54 | 7.84% | 1,111,234 | 493,306 |
| 18 | `18-empty-high-mobility-lite` | 2,399.880 | 20,748,008 | 8,645,430 | d1 | 10 | 10.36% | 1,361,171 | 732,311 |
| 20 | `20-empty-corner-race-lite` | 1,946.610 | 15,880,217 | 8,157,900 | a1 | -2 | 8.06% | 1,313,347 | 810,572 |
| 18 | `18-empty-low-mobility` | 1,719.420 | 18,360,344 | 10,678,200 | c2 | -42 | 7.89% | 1,324,830 | 597,481 |
| 18 | `18-empty-corner-choice` | 1,613.490 | 20,909,628 | 12,959,200 | a3 | 46 | 10.01% | 1,019,565 | 434,323 |
| 18 | `18-empty-edge-heavy` | 1,486.370 | 15,471,535 | 10,408,900 | d8 | 14 | 10.14% | 1,186,376 | 559,935 |

## External Match

Player A is `strong-v1` at depth 5. Player B is `external:ntest8` with Ntest
book disabled.

| match | A wins | B wins | draws | avg margin from A | errors | avg nodes A | avg ms A | avg ms B | exact roots A | exact roots B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| strong-v1 depth5 vs ntest8 nobook | 0 | 40 | 0 | -35.50 | 0 | 485,295.8 | 82.80 | 2,219.06 | 293 | 0 |

Opening breakdown:

| opening | games | A wins | B wins | draws | avg margin from A |
| --- | ---: | ---: | ---: | ---: | ---: |
| initial | 14 | 0 | 14 | 0 | -42.00 |
| c4-c3 | 14 | 0 | 14 | 0 | -32.00 |
| d3-c3-c4 | 12 | 0 | 12 | 0 | -32.00 |

## Next PR Candidates

1. Improve root move ordering. The slowest search positions often place the
   eventual best move at rank 5, 6, 7, or 8, which directly increases PVS scout
   and research work.
2. Improve evaluation quality and tactical safety against Ntest8. The 0-40
   match result against Ntest8 nobook indicates that move quality is a larger
   near-term strength gap than raw search speed.
3. Reduce exact endgame TT pressure. The 18/20-empty endgame rows show millions
   of collisions and rejected stores, so TT sizing or replacement policy is a
   concrete speed target.

## Decision

This baseline is now a repository artifact for future comparisons. The most
useful next strength work should compare against this file and report both
correctness checks and measurement deltas.
