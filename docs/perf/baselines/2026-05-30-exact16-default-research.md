# Exact 16 Default Research

Status: historical performance evidence.

## Conclusion

結論: `exact_endgame_empty_threshold = 16` を固定 default にする次の一手は、
**adaptive threshold** が最有力。理由は、16-empty の tail が平均ではなく少数の
root shape に強く偏っており、今回試した last-4、TT 容量増、reduced parity
拡張はいずれも 16-empty の p95/max wall-clock を default 候補として改善しなかったため。

推奨する次 PR は、exact core を変えずに root exact trigger だけを拡張し、
`empties <= 14` は従来どおり、`15-16` は root metrics による conservative gate
で opt-in exact 化する実験プロファイルを作ること。固定 16 default はまだ厳しい。

## Environment

- Commit: `2f4aee0`
- Build type: `Release`
- Host: Darwin arm64, macOS kernel `24.6.0`
- CMake: `4.3.2`
- Date: 2026-05-30

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
./build/release/othello_endgame_bench --positions endgame --empties 16 --repetitions 1 --root-breakdown --expand-worst-candidate
./build/release/othello_endgame_bench --positions endgame --empties 20 --repetitions 1
```

Local experiments were also run with temporary source edits and then reverted:

- correct last-4 dispatch experiment: `last_n_specialized_empties = 4` plus `case 4`
- exact TT capacity experiment: root empties `> 12` capacity `1 << 20` to `1 << 21`
- reduced parity activation experiment: current empties gate `<= 4` to `<= 6`

## Verification

- `ctest`: passed, 168/168.
- No default threshold changed.
- No MPC, ProbCut, eval-based pruning, or forward pruning was added.
- Temporary experiment code was not left in the final diff.

## Search Threshold Matrix

Suite search, iterative depths 5/6/7, TT on, PVS on, 3 repetitions:

| exact threshold | depth | exact positions | exact roots | elapsed ms | total nodes | nodes/s | result checksum | work checksum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 5 | 0 | 0 | 69.749 | 106911 | 1532802 | 17499861738422034076 | 14306551115201376168 |
| 0 | 6 | 0 | 0 | 131.913 | 322032 | 2441249 | 3368769080471942308 | 4056216879473949416 |
| 0 | 7 | 0 | 0 | 278.471 | 1097793 | 3942221 | 9619237167655969401 | 6350198096732665977 |
| 12 | 5 | 3 | 9 | 69.580 | 168063 | 2415402 | 14954352794227533343 | 4823608271166245968 |
| 12 | 6 | 3 | 9 | 116.323 | 381291 | 3277871 | 3769968071557168734 | 17547459445872055960 |
| 12 | 7 | 3 | 9 | 281.194 | 1153074 | 4100633 | 2286133763455112233 | 18329032133199987188 |
| 14 | 5 | 4 | 12 | 131.172 | 608547 | 4639291 | 17006524584433183298 | 615270513628672822 |
| 14 | 6 | 4 | 12 | 173.141 | 820050 | 4736327 | 15451754614129487100 | 4357021245810454477 |
| 14 | 7 | 4 | 12 | 323.974 | 1588971 | 4904621 | 2587242397277389217 | 654043229131580896 |
| 16 | 5 | 4 | 12 | 118.525 | 608547 | 5134345 | 17006524584433183298 | 615270513628672822 |
| 16 | 6 | 4 | 12 | 159.723 | 820050 | 5134206 | 15451754614129487100 | 4357021245810454477 |
| 16 | 7 | 4 | 12 | 323.584 | 1588971 | 4910529 | 2587242397277389217 | 654043229131580896 |

Per-position latency tail uses totals across 3 repetitions:

| exact threshold | depth | p95 ms | max ms | p95 nodes | max nodes |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 5 | 6.574 | 6.755 | 9729 | 10410 |
| 0 | 6 | 11.919 | 16.249 | 36999 | 42087 |
| 0 | 7 | 33.885 | 34.401 | 118314 | 119538 |
| 12 | 5 | 6.601 | 6.659 | 10410 | 61938 |
| 12 | 6 | 11.177 | 15.186 | 42087 | 61938 |
| 12 | 7 | 34.096 | 35.658 | 118314 | 119538 |
| 14 | 5 | 7.593 | 68.398 | 61938 | 441354 |
| 14 | 6 | 19.705 | 68.297 | 61938 | 441354 |
| 14 | 7 | 48.555 | 66.491 | 119538 | 441354 |
| 16 | 5 | 7.951 | 66.269 | 61938 | 441354 |
| 16 | 6 | 14.419 | 56.521 | 61938 | 441354 |
| 16 | 7 | 33.550 | 53.565 | 119538 | 441354 |

`exact=12` exactized the 2-, 8-, and 10-empty endgame-ish suite positions.
`exact=14` additionally exactized `late-black-pass` at 14 empties. `exact=16`
did not add any root positions because the current suite has no 15- or 16-empty
root fixture. Therefore this search suite cannot by itself validate fixed 16
default latency.

The result/work checksum changes from `12` to `14` are intentional semantic
changes from root exact solving. `late-black-pass` changes to exact score
`-62000`. `exact=16` matches `exact=14` on this suite because it exactizes the
same roots.

## Endgame Tail

Standalone exact endgame, standard solver, 1 repetition:

| empties | count | total ms | avg ms | p50 ms | p95/max ms | avg nodes | p95/max nodes | TT collisions | TT rejects | tt_order_used |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 14 | 13 | 736.353 | 56.643 | 37.299 | 253.038 | 632661 | 3355360 | 1192 | 72 | 24239 |
| 16 | 8 | 1909.566 | 238.696 | 121.173 | 622.950 | 2515893 | 6838892 | 117823 | 11765 | 12626 |
| 18 | 10 | 18368.987 | 1836.899 | 2029.210 | 3597.713 | 15337714 | 31326565 | 9302034 | 4913007 | 152668 |
| 20 | 9 | 20621.388 | 2291.265 | 1642.483 | 8516.171 | 16746523 | 52680535 | 8814671 | 9245313 | 143489 |

16-empty root tail is not uniform:

| position | class | legal cur/opp | regions/odd/largest | elapsed ms | nodes | TT collisions | TT rejects |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `16-empty-root-pass` | root pass, high opponent mobility, TT pressure | 0 / 13 | 3 / 2 / 8 | 622.950 | 6838892 | 93945 | 9972 |
| `16-empty-high-mobility` | high mobility, corner choice | 14 / 3 | 8 / 6 / 7 | 377.169 | 4241490 | 10127 | 796 |
| `16-empty-normal-mobility` | many small regions, high opponent mobility | 6 / 12 | 10 / 8 / 4 | 296.671 | 2679375 | 8237 | 461 |
| `16-empty-edge-heavy` | high mobility, edge-heavy | 13 / 5 | 7 / 4 / 4 | 277.088 | 3074370 | 5205 | 515 |
| `16-empty-corner-race` | corner race | 10 / 5 | 5 / 4 / 7 | 121.173 | 1232393 | 157 | 15 |
| `16-empty-parity-ish` | low current mobility, parity-ish | 4 / 9 | 4 / 2 / 7 | 94.418 | 1003896 | 109 | 1 |
| `16-empty-corner-choice` | corner choice | 9 / 8 | 9 / 8 / 4 | 83.279 | 734804 | 43 | 5 |
| `16-empty-low-mobility` | low current mobility | 3 / 11 | 5 / 2 / 8 | 36.818 | 321925 | 0 | 0 |

Root breakdown shows move ordering has some opportunity, but not enough to make
it the clear next default-enabler:

| position | root candidates | total candidate ms | worst move | worst ms | worst rank by margin | worst is best |
| --- | ---: | ---: | --- | ---: | ---: | --- |
| `16-empty-high-mobility` | 14 | 2732.530 | `b7` | 490.620 | 13 | no |
| `16-empty-normal-mobility` | 6 | 2080.121 | `h7` | 531.323 | 1 | yes |
| `16-empty-edge-heavy` | 13 | 2588.950 | `h1` | 401.443 | 1 | yes |
| `16-empty-root-pass` | 1 | 640.084 | pass | 640.084 | 1 | yes |
| `16-empty-corner-choice` | 9 | 1497.562 | `h5` | 271.376 | 2 | no |
| `16-empty-corner-race` | 10 | 884.460 | `g7` | 130.966 | 8 | no |

For `normal`, `edge-heavy`, and `root-pass`, the heaviest root move is already
the best or mandatory. Ordering can still help deeper children, but the fixed
16 tail also needs root-level selectivity.

## Experiments

| experiment | 14 avg/max ms | 16 avg/max ms | 18 avg/max ms | decision |
| --- | ---: | ---: | ---: | --- |
| baseline | 56.643 / 253.038 | 238.696 / 622.950 | 1836.899 / 3597.713 | reference |
| correct last-4 dispatch | 92.885 / 459.294 | 356.151 / 925.177 | 2524.243 / 5634.950 | reject |
| TT capacity `1<<21` for `>12` empties | 62.516 / 265.510 | 262.430 / 688.243 | 1728.918 / 3302.118 | not for 16 default |
| reduced parity current `<=6` | 82.258 / 372.107 | 404.182 / 1095.945 | 3061.449 / 5993.069 | reject |

Notes:

- The last-4 experiment was measured with a correct `case 4` dispatch. It
  increased nodes and tail latency at 14/16/18, so last-4/last-5 specialization
  is not the next candidate.
- Doubling exact TT capacity reduced 18-empty collisions/rejects and slightly
  improved the 18-empty tail, but 16-empty avg/max wall-clock worsened. This is
  worth revisiting for 18/20 or as an opt-in capacity policy, but it does not
  justify fixed 16 default.
- Extending reduced parity from current empties `<=4` to `<=6` worsened 16
  elapsed and max substantially. A more targeted move-ordering PR may still be
  useful, but this broad activation is not the right lever.

## Adaptive Gate Sketch

A simple 16-only diagnostic gate such as:

```text
empties <= 14: exact root always
empties 15-16: exact root only when !root_pass && legal_moves_current <= 10
```

would exactize 5 of the 8 16-empty fixtures in this sample:

- exactized: `low-mobility`, `normal-mobility`, `corner-choice`,
  `corner-race`, `parity-ish`
- skipped: `root-pass`, `high-mobility`, `edge-heavy`

On the standalone 16-empty fixture set, this would reduce the exactized fixture
time from 1909.566 ms to about 632.359 ms and reduce exactized max latency from
622.950 ms to 296.671 ms. This is not a complete search benchmark because the
skipped roots would fall back to depth-limited search, and that is a semantic
tradeoff. It is still the clearest evidence that fixed 16 is too blunt while
adaptive 16 may be realistic.

The next PR should turn this into a measured search profile, not a silent
default:

- add 15/16-empty root fixtures to the search suite or a dedicated threshold
  suite, so `exact=16` actually exactizes new roots in search benchmarks
- add an explicit experimental adaptive root-exact option or profile
- keep exact core correctness unchanged; no pruning inside the exact solver
- report best move, score, PV, result checksum, work checksum, exact root count,
  p95/max latency, and skipped root reasons

## WLD-First

WLD-first could help large-margin positions such as `16-empty-high-mobility`
and `16-empty-root-pass`, but it is not the best next PR yet. The current search
API reports exact final margins, so WLD-first would require a separated outcome
API, TT policy, and either exact-margin confirmation or a deliberate scoring
semantic change. That is a larger design step than adaptive root gating.

## 14 Before 16

`exact=14` remains a more plausible promotion candidate than fixed `16`, but it
should be handled as its own gate. In this suite it exactizes one additional
14-empty root (`late-black-pass`) and changes the result checksum by returning
the exact final margin. It is still not changed here because this task is a
research snapshot, and because 14/16 promotion should be judged with broader
search fixtures and latency-tail evidence.

## Next Decision

Choose option 4:

```text
fixed 16 default は厳しく、adaptive threshold が最有力
```

Fixed 16 remains blocked by tail-heavy root shapes. Move ordering work should
continue, especially for non-best expensive candidates in `high-mobility`,
`corner-choice`, and `corner-race`, but the next default-facing PR should first
make 15/16 root exactization selective and measurable at the search layer.
