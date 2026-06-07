# TT Shallow Plus Lazy Ordering Ablation

Date: 2026-06-07

## Hypothesis

Shallow TT policy and lazy first-move ordering target different costs, but their effects may not be
additive:

- `tt_min_probe_depth=1, tt_min_store_depth=1` removes depth-0 TT probe/store traffic.
- `lazy_first_move_ordering=on` avoids full dynamic ordering work when a legal preferred move beta
  cuts before sorting the rest.
- Reducing TT probes can also reduce TT best-move hints, so lazy ordering may become less effective.

This experiment measures that interaction only. It does not change default options, `strong-v1`,
evaluation, pattern learning, exact endgame solving, or pruning semantics.

## Build and Inputs

- Base commit: `e0ea0b9a` (`origin/main` after PR303)
- Measured worktree: PR304 test/docs changes on top of the base commit
- Build: `CMAKE_BUILD_TYPE=Release`
- Machine: macOS arm64, Darwin 24.6.0
- Raw output: `runs/perf/step4-*.jsonl` (gitignored)

## Variants

| variant | options |
| --- | --- |
| baseline | default TT policy, eager ordering |
| shallow-tt | `--tt-min-probe-depth 1 --tt-min-store-depth 1` |
| lazy | `--lazy-first-move-ordering on` |
| shallow-tt+lazy | `--tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on` |
| aggressive+lazy | `--tt-min-probe-depth 2 --tt-min-store-depth 1 --lazy-first-move-ordering on` |

## Commands

```sh
cmake --build build -j
./build/othello_tests "[search]"
./build/othello_tests "[match-runner]"
ctest --test-dir build --output-on-failure

./build/othello_search_bench --mode fixed --depths 6,7,8 --positions suite --repetitions 2 --tt on --pvs on --exact-endgame-threshold 0 --format jsonl
./build/othello_search_bench --mode fixed --depths 6,7,8 --positions suite --repetitions 2 --tt on --pvs on --exact-endgame-threshold 0 --tt-min-probe-depth 1 --tt-min-store-depth 1 --format jsonl
./build/othello_search_bench --mode fixed --depths 6,7,8 --positions suite --repetitions 2 --tt on --pvs on --exact-endgame-threshold 0 --lazy-first-move-ordering on --format jsonl
./build/othello_search_bench --mode fixed --depths 6,7,8 --positions suite --repetitions 2 --tt on --pvs on --exact-endgame-threshold 0 --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --format jsonl
./build/othello_search_bench --mode fixed --depths 6,7,8 --positions suite --repetitions 2 --tt on --pvs on --exact-endgame-threshold 0 --tt-min-probe-depth 2 --tt-min-store-depth 1 --lazy-first-move-ordering on --format jsonl

./build/othello_search_bench --mode iterative --depths 5,6,7,8 --positions suite --repetitions 2 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --format jsonl
./build/othello_search_bench --mode iterative --depths 5,6,7,8 --positions suite --repetitions 2 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --tt-min-probe-depth 1 --tt-min-store-depth 1 --format jsonl
./build/othello_search_bench --mode iterative --depths 5,6,7,8 --positions suite --repetitions 2 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --lazy-first-move-ordering on --format jsonl
./build/othello_search_bench --mode iterative --depths 5,6,7,8 --positions suite --repetitions 2 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --format jsonl
./build/othello_search_bench --mode iterative --depths 5,6,7,8 --positions suite --repetitions 2 --tt on --pvs on --aspiration on --exact-endgame-threshold 0 --tt-min-probe-depth 2 --tt-min-store-depth 1 --lazy-first-move-ordering on --format jsonl

./build/othello_match_runner --black search:depth=5,tt=on,pvs=on,exact=off,tt_min_probe_depth=1,tt_min_store_depth=1,lazy_first_move_ordering=on --white search:depth=5,tt=on,pvs=on,exact=off --games 4 --swap-sides true --seed 20260607 --output runs/perf/step4-match-runner-combined-smoke.jsonl --format jsonl --quiet
```

## Results

All rows preserved aggregate best move, score, and result checksum against the per-mode/depth
baseline.

| mode | depth | variant | elapsed_ms | vs_baseline | nodes | eval_calls | tt_lookups | tt_hits | tt_stores | tt_probe_skipped | tt_store_skipped | lazy_hits | lazy_cuts | scored_moves_saved | beta_cut_first_move_pct | checksum_match |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| fixed | 6 | baseline | 122.441 | +0.0% | 154,186 | 139,956 | 154,186 | 14,106 | 140,080 | 0 | 0 | 0 | 0 | 0 | 71.9% | yes |
| fixed | 6 | shallow-tt | 142.120 | +16.1% | 154,186 | 147,418 | 59,458 | 6,644 | 52,814 | 94,728 | 94,728 | 0 | 0 | 0 | 71.9% | yes |
| fixed | 6 | lazy | 133.147 | +8.7% | 151,358 | 137,652 | 151,358 | 12,852 | 138,506 | 0 | 0 | 3,604 | 732 | 4,598 | 72.9% | yes |
| fixed | 6 | shallow-tt+lazy | 132.415 | +8.1% | 151,358 | 143,976 | 58,838 | 6,528 | 52,310 | 92,520 | 92,520 | 3,604 | 732 | 4,598 | 72.9% | yes |
| fixed | 6 | aggressive+lazy | 125.943 | +2.9% | 160,182 | 157,792 | 16,720 | 1,978 | 56,836 | 143,462 | 101,368 | 1,904 | 280 | 1,856 | 72.5% | yes |
| fixed | 7 | baseline | 251.951 | +0.0% | 598,400 | 542,636 | 598,400 | 55,496 | 542,904 | 0 | 0 | 0 | 0 | 0 | 64.4% | yes |
| fixed | 7 | shallow-tt | 224.651 | -10.8% | 598,400 | 579,236 | 167,488 | 18,896 | 148,592 | 430,912 | 430,912 | 0 | 0 | 0 | 64.4% | yes |
| fixed | 7 | lazy | 234.716 | -6.8% | 596,832 | 542,652 | 596,832 | 51,280 | 545,552 | 0 | 0 | 11,586 | 2,634 | 20,566 | 65.8% | yes |
| fixed | 7 | shallow-tt+lazy | 222.187 | -11.8% | 596,832 | 574,770 | 168,342 | 19,162 | 149,180 | 428,490 | 428,490 | 11,586 | 2,634 | 20,566 | 65.8% | yes |
| fixed | 7 | aggressive+lazy | 233.696 | -7.2% | 628,718 | 618,990 | 65,392 | 8,178 | 159,918 | 563,326 | 460,622 | 6,316 | 1,264 | 8,784 | 65.0% | yes |
| fixed | 8 | baseline | 530.751 | +0.0% | 1,356,020 | 1,238,758 | 1,356,020 | 116,392 | 1,239,592 | 0 | 0 | 0 | 0 | 0 | 73.7% | yes |
| fixed | 8 | shallow-tt | 612.419 | +15.4% | 1,356,020 | 1,300,154 | 519,848 | 54,996 | 464,852 | 836,172 | 836,172 | 0 | 0 | 0 | 73.7% | yes |
| fixed | 8 | lazy | 517.469 | -2.5% | 1,356,882 | 1,239,466 | 1,356,882 | 110,046 | 1,246,808 | 0 | 0 | 25,998 | 6,502 | 45,978 | 74.4% | yes |
| fixed | 8 | shallow-tt+lazy | 457.660 | -13.8% | 1,356,882 | 1,293,664 | 524,544 | 55,848 | 468,696 | 832,338 | 832,338 | 25,998 | 6,502 | 45,978 | 74.4% | yes |
| fixed | 8 | aggressive+lazy | 471.874 | -11.1% | 1,439,008 | 1,417,418 | 153,164 | 17,266 | 509,546 | 1,285,844 | 912,196 | 14,570 | 3,348 | 24,196 | 73.9% | yes |
| iterative | 5 | baseline | 108.523 | +0.0% | 78,326 | 69,514 | 78,326 | 8,666 | 69,660 | 0 | 0 | 0 | 0 | 0 | 67.4% | yes |
| iterative | 5 | shallow-tt | 106.170 | -2.2% | 78,326 | 75,000 | 23,040 | 3,180 | 19,860 | 55,286 | 55,286 | 0 | 0 | 0 | 67.4% | yes |
| iterative | 5 | lazy | 104.240 | -3.9% | 77,056 | 68,392 | 77,056 | 7,948 | 69,108 | 0 | 0 | 2,348 | 570 | 4,120 | 69.4% | yes |
| iterative | 5 | shallow-tt+lazy | 103.005 | -5.1% | 77,056 | 73,054 | 23,044 | 3,286 | 19,758 | 54,012 | 54,012 | 2,348 | 570 | 4,120 | 69.4% | yes |
| iterative | 5 | aggressive+lazy | 105.841 | -2.5% | 81,766 | 79,754 | 8,830 | 1,614 | 21,430 | 72,936 | 58,722 | 1,556 | 242 | 1,488 | 67.4% | yes |
| iterative | 6 | baseline | 131.384 | +0.0% | 202,448 | 183,006 | 202,448 | 19,166 | 183,282 | 0 | 0 | 0 | 0 | 0 | 73.1% | yes |
| iterative | 6 | shallow-tt | 182.731 | +39.1% | 202,448 | 193,876 | 72,916 | 8,296 | 64,620 | 129,532 | 129,532 | 0 | 0 | 0 | 73.1% | yes |
| iterative | 6 | lazy | 139.773 | +6.4% | 200,730 | 181,670 | 200,730 | 17,484 | 183,246 | 0 | 0 | 5,328 | 1,302 | 8,858 | 74.1% | yes |
| iterative | 6 | shallow-tt+lazy | 140.599 | +7.0% | 200,730 | 190,736 | 73,112 | 8,418 | 64,694 | 127,618 | 127,618 | 5,328 | 1,302 | 8,858 | 74.1% | yes |
| iterative | 6 | aggressive+lazy | 137.328 | +4.5% | 211,928 | 207,628 | 23,408 | 3,366 | 69,742 | 188,520 | 138,820 | 3,364 | 626 | 3,956 | 73.2% | yes |
| iterative | 7 | baseline | 258.092 | +0.0% | 652,234 | 593,808 | 652,234 | 57,894 | 594,340 | 0 | 0 | 0 | 0 | 0 | 70.0% | yes |
| iterative | 7 | shallow-tt | 329.645 | +27.7% | 652,234 | 630,430 | 195,572 | 21,272 | 174,300 | 456,662 | 456,662 | 0 | 0 | 0 | 70.0% | yes |
| iterative | 7 | lazy | 249.237 | -3.4% | 654,962 | 597,452 | 654,962 | 53,748 | 601,214 | 0 | 0 | 11,630 | 3,234 | 24,146 | 71.0% | yes |
| iterative | 7 | shallow-tt+lazy | 244.999 | -5.1% | 654,962 | 629,574 | 197,390 | 21,626 | 175,764 | 457,572 | 457,572 | 11,630 | 3,234 | 24,146 | 71.0% | yes |
| iterative | 7 | aggressive+lazy | 252.161 | -2.3% | 687,294 | 674,694 | 75,982 | 10,422 | 186,916 | 611,312 | 489,956 | 7,408 | 1,598 | 10,550 | 70.1% | yes |
| iterative | 8 | baseline | 591.488 | +0.0% | 1,705,134 | 1,561,346 | 1,705,134 | 142,534 | 1,562,552 | 0 | 0 | 0 | 0 | 0 | 75.1% | yes |
| iterative | 8 | shallow-tt | 565.292 | -4.4% | 1,705,134 | 1,643,000 | 612,884 | 60,880 | 552,004 | 1,092,250 | 1,092,250 | 0 | 0 | 0 | 75.1% | yes |
| iterative | 8 | lazy | 565.629 | -4.4% | 1,698,554 | 1,557,446 | 1,698,554 | 132,318 | 1,566,188 | 0 | 0 | 25,996 | 7,540 | 55,696 | 75.8% | yes |
| iterative | 8 | shallow-tt+lazy | 558.428 | -5.6% | 1,698,554 | 1,628,836 | 613,482 | 60,928 | 552,554 | 1,085,072 | 1,085,072 | 25,996 | 7,540 | 55,696 | 75.8% | yes |
| iterative | 8 | aggressive+lazy | 589.636 | -0.3% | 1,778,794 | 1,749,976 | 197,766 | 23,578 | 589,416 | 1,581,028 | 1,165,800 | 16,236 | 3,856 | 28,084 | 75.3% | yes |

## Correctness

- Aggregate best move, score, and `result_checksum` matched baseline for every measured row.
- The search regression test now compares baseline, shallow TT, lazy ordering, and combined settings
  on the same iterative/PVS/aspiration position.
- The combined test also verifies both shallow TT skip counters and lazy ordering counters increase.
- Match-runner smoke completed 4 games with no illegal/error rows:
  - player A: `search:depth=5,tt=on,pvs=on,exact=off,tt_min_probe_depth=1,tt_min_store_depth=1,lazy_first_move_ordering=on`
  - player B: `search:depth=5,tt=on,pvs=on,exact=off`
  - aggregate player-A score diff: 0

## Decision

Keep defaults unchanged. Promote `shallow-tt+lazy` into the opt-in `strong-v1` practical preset.

`shallow-tt+lazy` is the best preset candidate from this small suite: it preserved checksums, kept
the same lazy cut counts as lazy-only for the `tt_min_probe_depth=1` setting, and was the fastest
variant in 4 of 7 measured depth rows. The effects are not cleanly additive, especially at fixed
depth 6 and iterative depth 6 where baseline stayed fastest, so this is intentionally limited to
`strong-v1` rather than the default search path.

The aggressive combined variant demonstrates the expected interference: TT lookups drop further,
but lazy hits/cuts also drop sharply because TT best-move hints become less available. It should
remain an experiment setting.

Next steps after the `strong-v1` preset change:

- Repeat on broader regression/tactical position sets.
- Add longer repetitions or median reporting if wall-clock noise remains high.
- Include match-runner trace comparison, but not full Elo, before any default promotion.
