# Main Search Promotion Candidate Check

Date: 2026-06-07

Status: measurement-only candidate report. No default, preset, evaluator, pattern table, or
training-script behavior was changed.

## Question

Latest `main` contains three nearby speed-oriented changes:

- `e0170e7` adds shallow TT best-move ordering hints.
- `3171afc` specializes exact endgame last-3 and last-4 tails.
- `69653f5` reduces midgame PV copying.

This report checks whether any current default or preset promotion is justified after those changes
landed on `main`.

## Build and Inputs

- Measured commit: `69653f5` (`origin/main`, `perf: reduce midgame PV copies (#319)`)
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- Build command: `cmake --build build -j 8`
- Correctness check: `ctest --test-dir build --output-on-failure`
- Result: 358/358 tests passed.
- Raw output: `runs/perf/main-promotion-2026-06-07/*.jsonl` (gitignored)

Search commands used `--exact-endgame-threshold 0` so midgame search profiles did not switch to the
root exact solver. Exact endgame was measured separately.

## Current Speed Features Inventory

Current public `SearchOptions` defaults remain conservative: fixed depth, TT off, PVS off,
aspiration off, lazy ordering off, shallow TT ordering hints off, and fixed exact root threshold 12.

Tooling has a stronger opt-in preset, `strong-v1`, defined in `tools/common/search_preset.cpp`.
It enables iterative search, TT, `tt_min_probe_depth=1`, `tt_min_store_depth=1`,
lazy first-move ordering, PVS, score-delta-aware aspiration, and adaptive16 exact root solving.
It does not enable `use_shallow_tt_move_ordering_hint`.

Shallow TT best-move hints are ordering-only. They do not satisfy TT cutoffs unless the entry depth
is sufficient. In practice they can still change alpha-beta work, PVs, and equal-score best-move
selection because they alter move order.

Exact endgame now dispatches the final 0-4 empties through specialized tail solvers and avoids TT,
full move ordering, and generic square conversion overhead in that tail. Tests compare last-3 and
last-4 behavior against generic reference cases and random near-end boards.

Midgame PV copy reduction stores PV lines in a reusable table during recursive search and converts
the root line to a vector only at the API boundary. It is intended to preserve search meaning.

## Search Profile Matrix

The promotion comparison below uses the three decision profiles: fixed plain default measurement,
current `strong-v1`-equivalent midgame settings with shallow hint explicitly off, and the same
settings with shallow hint on.

| profile | mode/options |
| --- | --- |
| default fixed plain | `--mode fixed --tt off --pvs off --aspiration off --exact-endgame-threshold 0` |
| strong-v1 hint off | iterative, TT on, PVS on, score-delta-aware aspiration, TT min probe/store 1, lazy ordering on, shallow hint off |
| strong-v1 hint on | same as strong-v1 hint off, plus shallow hint on |

| profile | depth | elapsed_ms | nodes | nps | eval_calls | beta_cutoffs | beta_first% | ord_builds | lazy_cuts | scored_saved | tt look/hit/% | shallow probe/hit/used | pvs scout/research |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |
| default fixed plain | 5 | 63.801 | 120,558 | 1,889,585 | 120,480 | 14,787 | 62.22 | 25,347 | 0 | 0 | 0/0/0.00% | 0/0/0 | 0/0 |
| default fixed plain | 6 | 104.425 | 282,723 | 2,707,438 | 282,513 | 73,419 | 73.95 | 95,355 | 0 | 0 | 0/0/0.00% | 0/0/0 | 0/0 |
| default fixed plain | 7 | 377.196 | 1,208,208 | 3,203,129 | 1,207,746 | 170,553 | 64.44 | 273,600 | 0 | 0 | 0/0/0.00% | 0/0/0 | 0/0 |
| default fixed plain | 8 | 1,010.609 | 3,028,509 | 2,996,718 | 3,026,409 | 804,531 | 74.55 | 1,021,536 | 0 | 0 | 0/0/0.00% | 0/0/0 | 0/0 |
| default fixed plain | 9 | 2,709.513 | 12,176,205 | 4,493,873 | 12,172,362 | 1,887,642 | 66.67 | 2,892,738 | 0 | 0 | 0/0/0.00% | 0/0/0 | 0/0 |
| strong-v1 hint off | 5 | 147.753 | 113,436 | 767,743 | 108,315 | 19,464 | 68.79 | 28,239 | 624 | 4,578 | 33,456/4,290/12.82% | 0/0/0 | 8,700/273 |
| strong-v1 hint off | 6 | 176.339 | 297,996 | 1,689,901 | 284,076 | 73,065 | 74.01 | 94,119 | 1,650 | 11,346 | 108,201/11,874/10.97% | 0/0/0 | 22,077/549 |
| strong-v1 hint off | 7 | 326.623 | 977,067 | 2,991,422 | 941,304 | 184,230 | 70.84 | 257,604 | 4,179 | 31,209 | 293,589/30,810/10.49% | 0/0/0 | 79,134/1,116 |
| strong-v1 hint off | 8 | 833.190 | 2,540,400 | 3,049,005 | 2,438,451 | 639,660 | 75.74 | 814,782 | 10,578 | 78,054 | 917,118/89,517/9.76% | 0/0/0 | 191,319/2,055 |
| strong-v1 hint off | 9 | 2,042.903 | 8,008,077 | 3,919,949 | 7,739,997 | 1,605,879 | 72.31 | 2,197,467 | 28,173 | 218,295 | 2,466,513/235,908/9.56% | 0/0/0 | 642,741/4,386 |
| strong-v1 hint on | 5 | 125.035 | 113,196 | 905,313 | 102,483 | 18,612 | 75.44 | 22,083 | 6,033 | 37,755 | 32,904/4,464/13.57% | 7,707/7,680/7,680 | 9,063/279 |
| strong-v1 hint on | 6 | 149.554 | 283,491 | 1,895,571 | 255,585 | 70,407 | 81.12 | 77,862 | 14,055 | 90,219 | 105,984/13,446/12.69% | 20,028/19,938/19,938 | 21,375/534 |
| strong-v1 hint on | 7 | 276.193 | 933,126 | 3,378,531 | 844,455 | 167,748 | 78.76 | 188,739 | 53,970 | 354,765 | 277,779/33,966/12.23% | 67,695/67,494/67,494 | 80,040/1,095 |
| strong-v1 hint on | 8 | 656.778 | 2,421,288 | 3,686,616 | 2,206,335 | 613,242 | 81.93 | 669,696 | 116,337 | 786,705 | 885,354/97,005/10.96% | 164,025/163,545/163,545 | 184,065/1,980 |
| strong-v1 hint on | 9 | 1,876.471 | 7,426,509 | 3,957,700 | 6,739,914 | 1,435,752 | 79.64 | 1,593,339 | 425,517 | 2,932,644 | 2,281,611/257,763/11.30% | 534,918/533,730/533,730 | 637,032/3,825 |

## Behavior Checks

`strong-v1` hint-on is faster than hint-off at every measured depth, but it is not a pure speed
change. Scores stayed equal for changed rows, but best move, PV, result checksum, and work checksum
changed on multiple positions.

| comparison | depth | elapsed delta | node delta | result checksum changes | best/score/PV changes | work checksum changes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| strong hint on vs off | 5 | -15.38% | -0.21% | 1/25 | 2/25 | 23/25 |
| strong hint on vs off | 6 | -15.19% | -4.87% | 5/25 | 8/25 | 24/25 |
| strong hint on vs off | 7 | -15.44% | -4.50% | 7/25 | 11/25 | 24/25 |
| strong hint on vs off | 8 | -21.17% | -4.69% | 4/25 | 16/25 | 24/25 |
| strong hint on vs off | 9 | -8.15% | -7.26% | 6/25 | 15/25 | 24/25 |

The exact command form from the task, without the strong-v1 TT shallow gates or
score-delta-aware aspiration profile, showed the same issue:

| comparison | depth | elapsed delta | node delta | result checksum changes | best/score/PV changes | work checksum changes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| lazy hint on vs off | 5 | -8.43% | -0.75% | 1/25 | 2/25 | 23/25 |
| lazy hint on vs off | 6 | -10.84% | -5.03% | 5/25 | 8/25 | 24/25 |
| lazy hint on vs off | 7 | -15.96% | -4.47% | 7/25 | 11/25 | 24/25 |
| lazy hint on vs off | 8 | -20.97% | -4.26% | 4/25 | 16/25 | 24/25 |
| lazy hint on vs off | 9 | -19.15% | -5.75% | 6/25 | 15/25 | 24/25 |

Against the plain iterative TT/PVS/aspiration run, current strong-v1 hint-off preserved result
checksums at all depths while changing work checksums as expected from lazy ordering and TT shallow
gates.

| comparison | depth | elapsed delta | node delta | result checksum changes | best/score/PV changes | work checksum changes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| strong-v1 hint off vs plain iterative | 5 | -1.44% | -3.45% | 0/25 | 1/25 | 22/25 |
| strong-v1 hint off vs plain iterative | 6 | +3.36% | -1.87% | 0/25 | 0/25 | 23/25 |
| strong-v1 hint off vs plain iterative | 7 | -11.55% | -0.13% | 0/25 | 1/25 | 23/25 |
| strong-v1 hint off vs plain iterative | 8 | -9.62% | -0.68% | 0/25 | 0/25 | 24/25 |
| strong-v1 hint off vs plain iterative | 9 | -16.28% | -1.18% | 0/25 | 3/25 | 24/25 |

No aspiration on/off matrix was run. The shallow-hint candidate already fails the pure-speed bar
because it changes best moves/PVs/checksums, so aspiration isolation is not needed for the promotion
decision in this report.

## Exact Endgame

Commands:

```sh
./build/othello_endgame_bench --empties 10 --repetitions 3 --format jsonl
./build/othello_endgame_bench --empties 12 --repetitions 3 --format jsonl
./build/othello_endgame_bench --empties 14 --repetitions 1 --format jsonl
./build/othello_endgame_bench --empties 16 --repetitions 1 --format jsonl
```

| empties | positions | elapsed_ms | nodes | nps | tt look/hit/% |
| ---: | ---: | ---: | ---: | ---: | --- |
| 10 | 3 | 7.181 | 155,793 | 21,694,660 | 9,231/1,089/11.80% |
| 12 | 3 | 20.697 | 387,831 | 18,738,178 | 33,861/5,076/14.99% |
| 14 | 13 | 479.545 | 10,254,970 | 21,384,781 | 608,578/107,970/17.74% |
| 16 | 10 | 1,690.202 | 28,510,943 | 16,868,362 | 2,109,100/228,039/10.81% |

Sample exact results were stable within the single current-main run. Examples:

| empties | sample results |
| ---: | --- |
| 10 | `ten-empty-dense-mobility=g3/-12`, `ten-empty-corner-pressure=a8/4`, `ten-empty-opponent-pass-after-move=h1/24` |
| 12 | `twelve-empty-late-pass-shape=b8/-34`, `twelve-empty-open-corners=h8/2`, `twelve-empty-corner-parity=e8/46` |
| 14 | `fourteen-empty-experimental-pass=None/-62`, `14-empty-low-mobility=c7/-60`, `14-empty-normal-mobility=f6/6`, `14-empty-high-mobility=h6/54` |
| 16 | `16-empty-low-mobility=f1/2`, `adaptive16-heavy-g12-ply44=a2/10`, `adaptive16-heavy-g27-ply44=h2/-14`, `16-empty-normal-mobility=h7/-4` |

This report does not isolate the exact last-3/4 speedup against a pre-specialization commit. The
current-main evidence is that Release tests pass, generic-reference last-3/4 tests pass, and the
current exact endgame benchmark runs cleanly with high NPS. Treat old-vs-new speedup magnitude as PR
evidence, not as re-proven here.

Likewise, this report does not isolate the PV-copy reduction against its parent commit. The current
search matrix shows no score changes from the current strong-v1 baseline, but PV-copy speed should be
claimed from a base/head benchmark if an exact magnitude is needed.

## Decision

Do not promote shallow TT hints into `strong-v1` or a new default yet.

The hint is a promising speed candidate: elapsed time improved by 8-21% across the measured
strong-v1 depths and ordering counters show large avoided full-build work. However, it changes best
moves, PVs, result checksums, and work checksums. Under the task rule, that is behavior change rather
than pure speed.

Do not change the default. The default path is still the safest stable public behavior, and the
fastest candidate here is not behavior-preserving.

Do not mutate `strong-v1` in place. If shallow hints are later accepted after behavior/quality
evidence, add a separate `strong-v2` or experimental preset first so callers can opt into changed
tie/PV behavior.

Exact last-3/4 specialization and midgame PV copy reduction look safe on current `main`, but this
single-main report should not be used to quantify their isolated speedups.

## Next Candidates

1. Make shallow TT hint behavior easier to reason about before promotion. Investigate why equal
   scores can produce different best moves/PVs under hint-on, and decide whether the engine should
   enforce order-independent tie behavior or document the preset as behavior-changing.
2. Add a small base/head or parent/head benchmark helper for current-main speed features. This would
   let future reports isolate PV-copy and exact-tail speedups without mixing them with preset
   options.
3. Continue root and midgame move-ordering diagnostics on positions where hint-on regressed latency,
   especially depth-9 `opening-central-run`, `early-tight-mobility`, and `midgame-normal-mobility`.
   The aggregate is faster, but tail regressions need explanation before preset promotion.
