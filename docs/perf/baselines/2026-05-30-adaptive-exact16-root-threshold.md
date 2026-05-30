# Adaptive Exact16 Root Threshold

Status: experimental performance evidence.

This snapshot adds an opt-in adaptive exact root profile after PR132's threshold
research. The experiment checks whether selected 15/16-empty roots can be solved
exactly without making the default profile behave like fixed `exact=16`.

The exact solver core is unchanged. The new search option only decides whether
the existing root exact handoff should happen before depth-limited search starts.
No forward pruning, MPC, ProbCut, or eval-based pruning was added.

## Profile

`adaptive16` uses this root-only gate:

- `empties <= 14`: solve exactly at the root
- `empties == 15 || empties == 16`: solve exactly only when the root is not a
  pass and `legal_moves_current <= 10`
- otherwise: keep depth-limited search

The benchmark prints `exact_profile`, `exact_root`, and `exact_skip_reason` in
`--by-position` output. Adaptive skip reasons are
`adaptive_root_pass` and `adaptive_too_many_legal_moves`.

## Commands

Branch measurement was taken on a Release build based on `origin/main` at
`890694f` plus this PR's working-tree changes.

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --target othello_tests othello_search_bench othello_endgame_bench othello_match_runner -j 8
ctest --test-dir build/release --output-on-failure

./build/release/othello_search_bench --positions threshold --describe-positions

./build/release/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions threshold \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --exact-endgame-thresholds 0,12,14,16,adaptive16 \
  --by-position

./build/release/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions threshold \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --exact-endgame-thresholds 0,12,14,16,adaptive16
```

`ctest` passed: 173/173.

The `threshold` position set contains the normal search suite plus 12 focused
15/16-empty fixtures. `--describe-positions` reported 37 total positions,
12 `threshold_endgame` positions, no duplicate hashes, successful parse and
round-trip validation, and 0 tag consistency warnings.

## Matrix

The latency columns below come from `--by-position` totals across 3 repetitions.
The node, TT, and checksum columns come from the aggregate matrix run with the
same options.

| profile | depth | exact positions | exact searches | elapsed ms | p95 ms | max ms | nodes | tt hits | tt stores | tt collisions | tt rejects | tt order used | result checksum | work checksum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 5 | 0 | 0 | 87.947 | 3.636 | 4.272 | 157749 | 12951 | 144798 | 0 | 0 | 261 | 9742978052958068980 | 8286569838420371183 |
| 0 | 6 | 0 | 0 | 162.556 | 11.099 | 11.317 | 427200 | 35730 | 391470 | 0 | 0 | 1152 | 747427656184018066 | 12567274672448995021 |
| 0 | 7 | 0 | 0 | 322.800 | 20.473 | 25.187 | 1285968 | 103857 | 1182111 | 54 | 0 | 3516 | 11611330869328382833 | 17813850696896623080 |
| 12 | 5 | 3 | 9 | 86.886 | 3.951 | 5.738 | 218604 | 13968 | 150657 | 6 | 0 | 669 | 11642763057894049690 | 1248946792561648365 |
| 12 | 6 | 3 | 9 | 149.209 | 7.395 | 8.908 | 486039 | 36600 | 395460 | 6 | 0 | 1560 | 6901725352926766295 | 2213546040478333137 |
| 12 | 7 | 3 | 9 | 317.069 | 18.042 | 26.688 | 1339329 | 104235 | 1181115 | 60 | 0 | 3864 | 1847053620713117447 | 4767190252559119238 |
| 14 | 5 | 4 | 12 | 136.942 | 5.731 | 45.982 | 659088 | 27051 | 220695 | 6 | 0 | 699 | 16862106491899255446 | 17090911097487390677 |
| 14 | 6 | 4 | 12 | 191.765 | 8.975 | 45.302 | 924798 | 49605 | 463851 | 6 | 0 | 1590 | 11074444395529033897 | 3133197338037304321 |
| 14 | 7 | 4 | 12 | 359.486 | 24.971 | 45.775 | 1775226 | 117024 | 1246860 | 60 | 0 | 3894 | 2058012177191467732 | 8733970900851853078 |
| 16 | 5 | 16 | 48 | 7263.918 | 1069.571 | 1757.451 | 79012149 | 1381614 | 10717047 | 377763 | 36504 | 124089 | 10771908028756078351 | 7026922321084717132 |
| 16 | 6 | 16 | 48 | 7274.907 | 1078.737 | 1736.245 | 79197249 | 1396383 | 10887378 | 377763 | 36504 | 124755 | 17951627163408822685 | 11621785751850506781 |
| 16 | 7 | 16 | 48 | 7445.538 | 1073.569 | 1760.354 | 79835559 | 1441935 | 11480136 | 377817 | 36504 | 126633 | 10447987388750384557 | 4785386902644746397 |
| adaptive16 | 5 | 12 | 36 | 2744.514 | 359.152 | 804.469 | 27717135 | 557337 | 4399017 | 26268 | 1479 | 60318 | 8457168178253632463 | 14766315611800480939 |
| adaptive16 | 6 | 12 | 36 | 2808.483 | 345.300 | 812.747 | 27935847 | 574875 | 4600191 | 26268 | 1479 | 61044 | 15426617469385266235 | 10348856323092117873 |
| adaptive16 | 7 | 12 | 36 | 2929.440 | 347.010 | 796.783 | 28680435 | 632001 | 5287653 | 26322 | 1479 | 62988 | 12836715295151725146 | 8463101136174543138 |

Fixed `exact=16` is unsafe as a default candidate on this threshold suite: at
depth 7 it exact-solves all 15/16 fixtures, pushes p95 per-position latency to
about 1074 ms, and reaches a max of about 1760 ms.

`adaptive16` is much lighter than fixed `exact=16`: at depth 7 it reduces p95
from 1073.569 ms to 347.010 ms and max from 1760.354 ms to 796.783 ms while
still exact-solving 8 of the 12 focused 15/16-empty fixtures. It is still far
heavier than `exact=14`, so this remains an experimental opt-in profile rather
than a default.

## Adaptive Root Decisions

Depth 7 by-position rows for the focused threshold fixtures:

| position | empty | legal moves | pass | adaptive16 | skip reason | elapsed ms | nodes | best move | score |
| --- | ---: | ---: | --- | --- | --- | ---: | ---: | --- | ---: |
| threshold-15-gated-exact | 15 | 10 | no | yes | - | 275.439 | 2598279 | c7 | -2000 |
| threshold-15-gated-skip | 15 | 12 | no | no | adaptive_too_many_legal_moves | 11.532 | 46398 | h1 | 113 |
| threshold-15-low-branching | 15 | 3 | no | yes | - | 345.546 | 3956346 | c1 | -36000 |
| threshold-15-parity-ish | 15 | 10 | no | yes | - | 246.335 | 2613789 | f7 | 45000 |
| threshold-16-low-mobility | 16 | 3 | no | yes | - | 107.500 | 965775 | f1 | 2000 |
| threshold-16-normal-mobility | 16 | 6 | no | yes | - | 796.783 | 8038125 | h7 | -4000 |
| threshold-16-high-mobility | 16 | 14 | no | no | adaptive_too_many_legal_moves | 10.070 | 42216 | c1 | 461 |
| threshold-16-root-pass | 16 | 0 | yes | no | adaptive_root_pass | 4.708 | 15234 | - | -465 |
| threshold-16-corner-choice | 16 | 9 | no | yes | - | 229.272 | 2204412 | d1 | -14000 |
| threshold-16-corner-race | 16 | 10 | no | yes | - | 347.010 | 3697179 | g6 | 34000 |
| threshold-16-edge-heavy | 16 | 13 | no | no | adaptive_too_many_legal_moves | 14.123 | 60309 | h1 | 35 |
| threshold-16-parity-ish | 16 | 4 | no | yes | - | 272.130 | 3011688 | f8 | -43000 |

## Semantic Changes

Best move, score, PV, result checksum, and work checksum change when a root moves
from depth-limited search to exact solving. Those changes are expected semantic
changes, not pure speed comparisons. `adaptive16` also differs from fixed
`exact=16` on the skipped 15/16 roots because those rows intentionally remain
depth-limited.

Exact score and best-move correctness are protected by keeping the exact solver
unchanged and by using the adaptive policy only as a root handoff decision. If
the gate returns false, the normal depth-limited search path runs unchanged.

## Decision

Keep the default threshold behavior unchanged. `adaptive16` is useful evidence
for a conservative 15/16-empty opt-in profile, but the remaining tail is too
large for default promotion. Next steps should test a larger threshold fixture
set and consider a stricter 15/16 gate, such as a lower legal-move cap or a
simple root-shape metric, before exposing it as a recommended profile.
