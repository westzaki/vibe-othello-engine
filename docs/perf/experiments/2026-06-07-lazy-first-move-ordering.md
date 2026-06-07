# Lazy First-Move Ordering Ablation

Date: 2026-06-07

## Hypothesis

When a PV/root/TT preferred move is legal, trying it before building the full midgame ordering list
can avoid expensive dynamic ordering work on first-move beta cuts. The default remains eager
ordering; this experiment only adds an ablation option and counters.

## Build and Inputs

- Base commit: `97b807460f1fa8e39fb2f1353a4bb17ec65da757`
- Measured worktree: Step 3 lazy first-move ordering option on top of base
- Build: `CMAKE_BUILD_TYPE=Release`, `-O3 -DNDEBUG`
- Machine: macOS arm64, Darwin 24.6.0
- Raw output: `runs/perf/step3-*.jsonl` (gitignored)

## Commands

```sh
cmake --build build
./build/othello_tests "[search]"
./build/othello_tests "[match-runner]"
ctest --test-dir build --output-on-failure

./build/othello_search_bench --mode iterative --depths 5,6,7,8 --positions suite --repetitions 2 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --format jsonl
./build/othello_search_bench --mode iterative --depths 5,6,7,8 --positions suite --repetitions 2 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --lazy-first-move-ordering on --format jsonl

./build/othello_search_bench --mode fixed --depths 6,7,8 --positions suite --repetitions 2 --tt on --pvs on --exact-endgame-threshold 0 --format jsonl
./build/othello_search_bench --mode fixed --depths 6,7,8 --positions suite --repetitions 2 --tt on --pvs on --exact-endgame-threshold 0 --lazy-first-move-ordering on --format jsonl

./build/othello_search_bench --mode iterative --depths 6 --positions smoke --repetitions 2 --tt off --pvs off --aspiration off --exact-endgame-threshold 0 --format jsonl
./build/othello_search_bench --mode iterative --depths 6 --positions smoke --repetitions 2 --tt off --pvs off --aspiration off --exact-endgame-threshold 0 --lazy-first-move-ordering on --format jsonl

./build/othello_search_bench --mode iterative --depths 6 --positions smoke --repetitions 2 --tt off --pvs on --aspiration off --exact-endgame-threshold 0 --format jsonl
./build/othello_search_bench --mode iterative --depths 6 --positions smoke --repetitions 2 --tt off --pvs on --aspiration off --exact-endgame-threshold 0 --lazy-first-move-ordering on --format jsonl
```

## Suite Results

All rows below preserved aggregate best move, score, and result checksum.

| mode | depth | elapsed baseline | elapsed lazy | change | nodes baseline | nodes lazy | full builds baseline | full builds lazy | lazy cuts | scored moves saved |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| iterative | 5 | 106.344ms | 106.894ms | +0.5% | 78,326 | 77,056 | 19,706 | 18,976 | 570 | 4,120 |
| iterative | 6 | 142.867ms | 138.774ms | -2.9% | 202,448 | 200,730 | 64,332 | 63,008 | 1,302 | 8,858 |
| iterative | 7 | 250.655ms | 249.600ms | -0.4% | 652,234 | 654,962 | 173,752 | 171,850 | 3,234 | 24,146 |
| iterative | 8 | 586.769ms | 563.412ms | -4.0% | 1,705,134 | 1,698,554 | 550,710 | 543,500 | 7,540 | 55,696 |
| fixed | 6 | 143.168ms | 134.065ms | -6.4% | 154,186 | 151,358 | 52,684 | 51,414 | 732 | 4,598 |
| fixed | 7 | 241.734ms | 227.662ms | -5.8% | 598,400 | 596,832 | 148,318 | 146,194 | 2,634 | 20,566 |
| fixed | 8 | 492.124ms | 448.943ms | -8.8% | 1,356,020 | 1,356,882 | 463,950 | 461,142 | 6,502 | 45,978 |

## TT-Off Smoke

TT-off rows also preserved aggregate best move, score, and result checksum. Lazy preferred moves
were available from iterative PV/root hints, but there were no lazy cuts in this small smoke run.

| mode | options | depth | elapsed change | lazy cuts | scored moves saved |
| --- | --- | ---: | ---: | ---: | ---: |
| iterative | TT off, PVS off | 6 | +1.7% | 0 | 0 |
| iterative | TT off, PVS on | 6 | -2.1% | 0 | 0 |

## Decision

Keep `use_lazy_first_move_ordering` default-off. The option is useful for ablation and shows real
full-ordering savings when TT/PVS/iterative hints are active, but the gains are workload-dependent
and TT-off smoke shows no lazy cuts. Broader regression and tactical sets should be checked before
promoting this into any preset.
