# PR129 Exact Threshold Gate

Status: historical performance evidence.

This snapshot checks whether the root exact endgame threshold can move from 12
to 14 after PR129 empty-region parity ordering. The branch under test adds
benchmark observability and forwards exact-root TT stats into `SearchStats`;
search score, best move, PV, and exact solver semantics are otherwise the same
as `main` at `f01d05b`.

## Commands

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --target othello_tests othello_search_bench othello_endgame_bench othello_match_runner -j 8
ctest --test-dir build/release --output-on-failure

./build/release/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --exact-endgame-thresholds 0,12,14,16 \
  --by-position

./build/release/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --exact-endgame-thresholds 0,12,14,16

./build/release/othello_endgame_bench --positions endgame --empties 14,16,18 --repetitions 1
```

`ctest` passed: 166/166.

## Search Threshold Matrix

Aggregate search rows, suite positions, iterative depth 5/6/7, TT on, PVS on,
3 repetitions:

| exact threshold | depth | exact positions | exact searches | elapsed ms | total nodes | nodes/s | result checksum | work checksum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 5 | 0 | 0 | 67.576 | 106911 | 1582089 | 17499861738422034076 | 14306551115201376168 |
| 0 | 6 | 0 | 0 | 119.545 | 322032 | 2693807 | 3368769080471942308 | 4056216879473949416 |
| 0 | 7 | 0 | 0 | 277.167 | 1097793 | 3960763 | 9619237167655969401 | 6350198096732665977 |
| 12 | 5 | 3 | 9 | 67.714 | 168063 | 2481967 | 14954352794227533343 | 4823608271166245968 |
| 12 | 6 | 3 | 9 | 116.772 | 381291 | 3265255 | 3769968071557168734 | 17547459445872055960 |
| 12 | 7 | 3 | 9 | 279.135 | 1153074 | 4130883 | 2286133763455112233 | 18329032133199987188 |
| 14 | 5 | 4 | 12 | 125.795 | 608547 | 4837604 | 17006524584433183298 | 615270513628672822 |
| 14 | 6 | 4 | 12 | 180.352 | 820050 | 4546929 | 15451754614129487100 | 4357021245810454477 |
| 14 | 7 | 4 | 12 | 370.304 | 1588971 | 4290994 | 2587242397277389217 | 654043229131580896 |
| 16 | 5 | 4 | 12 | 126.709 | 608547 | 4802701 | 17006524584433183298 | 615270513628672822 |
| 16 | 6 | 4 | 12 | 174.910 | 820050 | 4688411 | 15451754614129487100 | 4357021245810454477 |
| 16 | 7 | 4 | 12 | 348.952 | 1588971 | 4553546 | 2587242397277389217 | 654043229131580896 |

Per-position latency tails:

| exact threshold | depth | p95 ms | max ms | p95 nodes | max nodes |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 5 | 3.292 | 3.358 | 9729 | 10410 |
| 0 | 6 | 8.546 | 9.390 | 36999 | 42087 |
| 0 | 7 | 25.210 | 27.674 | 118314 | 119538 |
| 12 | 5 | 4.213 | 6.212 | 10410 | 61938 |
| 12 | 6 | 8.612 | 11.300 | 42087 | 61938 |
| 12 | 7 | 27.805 | 29.093 | 118314 | 119538 |
| 14 | 5 | 6.702 | 54.030 | 61938 | 441354 |
| 14 | 6 | 11.731 | 55.285 | 61938 | 441354 |
| 14 | 7 | 66.800 | 143.940 | 119538 | 441354 |
| 16 | 5 | 9.307 | 63.637 | 61938 | 441354 |
| 16 | 6 | 21.824 | 58.255 | 61938 | 441354 |
| 16 | 7 | 34.341 | 54.990 | 119538 | 441354 |

`exact=12` solved 2-, 8-, and 10-empty suite positions exactly at the root.
`exact=14` additionally solved `late-black-pass` at 14 empties exactly at the
root. `exact=16` did not add more suite positions because this suite has no
15- or 16-empty root position.

The `late-black-pass` 14-empty root exact row changed from depth-limited pass
scores such as `-294`, `-332`, and `-336` to the exact final score `-62000`.
This is an intentional semantic change from root exact solving, not a pure speed
comparison.

## Decision

Do not change the default threshold in this snapshot. `exact=14` is a measured
opt-in and promotion candidate, but the latency tail is not yet a default-grade
profile change: the additional 14-empty exact root raised max per-position
latency from about 6-11 ms at `exact=12` depths 5/6 to about 54-55 ms, with a
depth-7 tail outlier at about 144 ms.

`exact=16` should stay analysis/opt-in only. It matched `exact=14` on this search
suite because no 15/16-empty roots were present, while standalone exact endgame
sanity still shows 16-empty positions can be substantially heavier than 14-empty
positions.
