# Adaptive16 Match Runner Smoke

Status: match-level smoke evidence for the experimental `adaptive16` exact root
profile.

This snapshot checks the PR133 adaptive exact root profile after exposing it to
`othello_match_runner` player specs. The exact solver core is unchanged. The
match runner only passes the existing `SearchOptions` policy through and records
how often a search call starts from an exact root. No forward pruning, MPC,
ProbCut, or eval-based pruning was added.

## Commands

Measurement was taken on a Release build from `origin/main` at `42af807` plus
this PR's working-tree changes.

```sh
cmake --build build/release --target othello_tests othello_search_bench othello_match_runner -j 8
ctest --test-dir build/release --output-on-failure
python3 -m unittest tools/scripts/tests/test_match_summary.py

./build/release/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions threshold \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --exact-endgame-thresholds 12,14,adaptive16 \
  --by-position

./build/release/othello_match_runner \
  --black search:depth=5,tt=on,pvs=on,exact=12 \
  --white search:depth=5,tt=on,pvs=on,exact=adaptive16 \
  --games 40 \
  --swap-sides true \
  --seed 20260531 \
  --openings data/openings/smoke_openings.txt \
  --output /private/tmp/exact12_vs_adaptive16_match.jsonl

./build/release/othello_match_runner \
  --black search:depth=5,tt=on,pvs=on,exact=14 \
  --white search:depth=5,tt=on,pvs=on,exact=adaptive16 \
  --games 40 \
  --swap-sides true \
  --seed 20260531 \
  --openings data/openings/smoke_openings.txt \
  --output /private/tmp/exact14_vs_adaptive16_match.jsonl
```

`ctest` passed: 179/179. `match_summary.py` unit tests passed: 10/10.

## Search Threshold Smoke

The latency percentiles use per-position totals across 3 repetitions from
`--by-position`.

| profile | depth | exact positions | exact searches | total elapsed ms | p50 ms | p95 ms | max ms | avg nodes/position | p95 nodes | max nodes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 12 | 5 | 3 | 9 | 105.868 | 2.767 | 5.098 | 6.390 | 6,172.7 | 15,324 | 61,938 |
| 12 | 6 | 3 | 9 | 189.585 | 5.239 | 10.040 | 12.063 | 12,778.0 | 29,436 | 61,938 |
| 12 | 7 | 3 | 9 | 409.479 | 9.856 | 28.244 | 46.757 | 39,900.5 | 106,824 | 188,619 |
| 14 | 5 | 4 | 12 | 155.097 | 2.831 | 6.406 | 52.973 | 18,077.1 | 61,938 | 441,354 |
| 14 | 6 | 4 | 12 | 233.456 | 4.694 | 10.193 | 59.536 | 24,636.2 | 61,938 | 441,354 |
| 14 | 7 | 4 | 12 | 502.142 | 10.747 | 53.258 | 60.968 | 51,681.2 | 188,619 | 441,354 |
| adaptive16 | 5 | 9 | 27 | 2,061.508 | 3.687 | 444.547 | 552.635 | 399,305.8 | 3,011,688 | 3,697,179 |
| adaptive16 | 6 | 9 | 27 | 2,241.865 | 6.442 | 476.327 | 576.199 | 405,020.5 | 3,011,688 | 3,697,179 |
| adaptive16 | 7 | 9 | 27 | 2,134.538 | 14.521 | 325.232 | 429.374 | 430,075.0 | 3,011,688 | 3,697,179 |

At depth 7, adaptive16 exactized these focused 15/16-empty roots:

| position | empty | result | elapsed ms | nodes | best | score |
| --- | ---: | --- | ---: | ---: | --- | ---: |
| threshold-15-gated-exact | 15 | exact | 321.464 | 2,598,279 | c7 | -2000 |
| threshold-15-parity-ish | 15 | exact | 290.878 | 2,613,789 | f7 | 45000 |
| threshold-16-corner-choice | 16 | exact | 272.546 | 2,204,412 | d1 | -14000 |
| threshold-16-corner-race | 16 | exact | 429.374 | 3,697,179 | g6 | 34000 |
| threshold-16-parity-ish | 16 | exact | 325.232 | 3,011,688 | f8 | -43000 |

The skipped 15/16-empty roots reported `adaptive_too_many_legal_moves`,
`adaptive_opponent_too_many_legal_moves`, or `adaptive_root_pass`. Best move,
score, PV, result checksum, and work checksum changed against `exact=14` on the
five exactized 15/16 roots. Those are expected semantic changes from root exact
solving, not pure speed differences.

## Match Smoke

Both matches used `data/openings/smoke_openings.txt`, `--games 40`,
`--swap-sides true`, and seed `20260531`.

| match | A wins | B wins | draws | avg margin from A | errors | avg nodes A | avg nodes B | avg ms A | avg ms B | exact roots A | exact roots B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| exact12 vs adaptive16 | 20 | 20 | 0 | -3.80 | 0 | 78,773.6 | 824,458.9 | 31.77 | 126.63 | 226 | 320 |
| exact14 vs adaptive16 | 20 | 20 | 0 | -0.65 | 0 | 385,294.2 | 838,541.6 | 70.87 | 132.35 | 273 | 306 |

Per-opening W/L was balanced in both smoke matches:

| match | opening | games | A wins | B wins | draws | avg margin from A | avg nodes A | avg nodes B | avg ms A | avg ms B | exact roots A | exact roots B |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| exact12 vs adaptive16 | initial | 14 | 7 | 7 | 0 | -9.00 | 75,424 | 776,557 | 32.96 | 117.98 | 70 | 112 |
| exact12 vs adaptive16 | c4-c3 | 14 | 7 | 7 | 0 | -1.00 | 80,696 | 850,371 | 30.42 | 130.61 | 84 | 112 |
| exact12 vs adaptive16 | d3-c3-c4 | 12 | 6 | 6 | 0 | -1.00 | 80,438 | 850,114 | 31.95 | 132.09 | 72 | 96 |
| exact14 vs adaptive16 | initial | 14 | 7 | 7 | 0 | 0.00 | 623,672 | 773,182 | 101.16 | 122.55 | 91 | 98 |
| exact14 vs adaptive16 | c4-c3 | 14 | 7 | 7 | 0 | -1.00 | 257,056 | 873,854 | 56.06 | 142.49 | 98 | 112 |
| exact14 vs adaptive16 | d3-c3-c4 | 12 | 6 | 6 | 0 | -1.00 | 256,798 | 873,596 | 52.80 | 131.98 | 84 | 96 |

## Decision

`adaptive16` is useful to test at match level now that player specs can opt in
with `exact=adaptive16`, but this smoke does not justify changing the default.
Compared with fixed `exact=14`, adaptive16 still has a much heavier search bench
tail on the threshold suite and roughly doubles average match time in this
depth-5 smoke. The match results were legal and balanced, with no opening-level
W/L skew against adaptive16, so the next decision should come from a larger
opening set and a stricter tail budget before considering any default or normal
profile promotion.
