# Adaptive Exact16 Root Threshold

Status: experimental performance evidence.

This snapshot adds an opt-in adaptive exact root profile after PR132's threshold
research. The experiment checks whether selected 15/16-empty roots can be solved
exactly without making the default profile behave like fixed `exact=16`.

The exact solver core is unchanged. The new search option only decides whether
the existing root exact handoff should happen before depth-limited search starts.
No forward pruning, MPC, ProbCut, or eval-based pruning was added.

## Final Profile

`adaptive16` uses this root-only gate:

- `empties <= 14`: solve exactly at the root
- `empties == 15 || empties == 16`: solve exactly only when the root is not a
  pass, `legal_moves_current <= 10`, and `legal_moves_opponent <= 10`
- otherwise: keep depth-limited search

The benchmark prints `exact_profile`, `exact_root`, and `exact_skip_reason` in
`--by-position` output. Adaptive skip reasons include `adaptive_root_pass`,
`adaptive_too_many_legal_moves`, and
`adaptive_opponent_too_many_legal_moves`.

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
  --exact-endgame-thresholds 14,16,adaptive16_current,adaptive16_cap8,adaptive16_cap6,adaptive16_opp10,adaptive16_shape,adaptive16_split,adaptive16 \
  --by-position

./build/release/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions threshold \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --exact-endgame-thresholds 14,16,adaptive16_current,adaptive16_cap8,adaptive16_cap6,adaptive16_opp10,adaptive16_shape,adaptive16_split,adaptive16
```

`ctest` passed: 173/173.

The `threshold` position set contains the normal search suite plus 12 focused
15/16-empty fixtures. `--describe-positions` reported 37 total positions,
12 `threshold_endgame` positions, no duplicate hashes, successful parse and
round-trip validation, and 0 tag consistency warnings.

## Gate Candidate Matrix

The latency columns below come from `--by-position` totals across 3 repetitions.
The node, TT, and checksum columns come from the aggregate matrix run with the
same options. `adaptive16_current` is the original PR133 gate
`!root_pass && legal_moves_current <= 10`; `adaptive16_opp10` is the adopted
gate and matches the final `adaptive16` rows.

| profile | depth | exact positions | exact searches | elapsed ms | p95 ms | max ms | nodes | tt hits | tt stores | tt collisions | tt rejects | tt order used | result checksum | work checksum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 14 | 5 | 4 | 12 | 126.925 | 5.513 | 42.035 | 659088 | 27051 | 220695 | 6 | 0 | 699 | 16862106491899255446 | 17090911097487390677 |
| 14 | 6 | 4 | 12 | 174.487 | 7.579 | 41.838 | 924798 | 49605 | 463851 | 6 | 0 | 1590 | 11074444395529033897 | 3133197338037304321 |
| 14 | 7 | 4 | 12 | 333.777 | 23.684 | 41.649 | 1775226 | 117024 | 1246860 | 60 | 0 | 3894 | 2058012177191467732 | 8733970900851853078 |
| 16 | 5 | 16 | 48 | 6715.330 | 1004.994 | 1604.970 | 79012149 | 1381614 | 10717047 | 377763 | 36504 | 124089 | 10771908028756078351 | 7026922321084717132 |
| 16 | 6 | 16 | 48 | 6778.948 | 1005.553 | 1614.348 | 79197249 | 1396383 | 10887378 | 377763 | 36504 | 124755 | 17951627163408822685 | 11621785751850506781 |
| 16 | 7 | 16 | 48 | 6894.126 | 993.378 | 1642.823 | 79835559 | 1441935 | 11480136 | 377817 | 36504 | 126633 | 10447987388750384557 | 4785386902644746397 |
| adaptive16_current | 5 | 12 | 36 | 2557.911 | 318.621 | 741.720 | 27717135 | 557337 | 4399017 | 26268 | 1479 | 60318 | 8457168178253632463 | 14766315611800480939 |
| adaptive16_current | 6 | 12 | 36 | 2597.080 | 318.139 | 741.259 | 27935847 | 574875 | 4600191 | 26268 | 1479 | 61044 | 15426617469385266235 | 10348856323092117873 |
| adaptive16_current | 7 | 12 | 36 | 2729.945 | 319.651 | 743.875 | 28680435 | 632001 | 5287653 | 26322 | 1479 | 62988 | 12836715295151725146 | 8463101136174543138 |
| adaptive16_cap8 | 5 | 8 | 24 | 1527.978 | 320.121 | 734.482 | 16622019 | 346896 | 2625921 | 25305 | 1398 | 22962 | 18268349999238356251 | 151241845288489049 |
| adaptive16_cap8 | 6 | 8 | 24 | 1563.991 | 313.615 | 729.913 | 16860477 | 365853 | 2845422 | 25305 | 1398 | 23700 | 11276078617595851465 | 3959157466303351326 |
| adaptive16_cap8 | 7 | 8 | 24 | 1769.665 | 321.988 | 767.491 | 17678274 | 430137 | 3598935 | 25359 | 1398 | 25956 | 15509322700832126615 | 6959567710591792128 |
| adaptive16_cap6 | 5 | 8 | 24 | 1521.575 | 313.452 | 736.814 | 16622019 | 346896 | 2625921 | 25305 | 1398 | 22962 | 18268349999238356251 | 151241845288489049 |
| adaptive16_cap6 | 6 | 8 | 24 | 1585.323 | 313.370 | 746.336 | 16860477 | 365853 | 2845422 | 25305 | 1398 | 23700 | 11276078617595851465 | 3959157466303351326 |
| adaptive16_cap6 | 7 | 8 | 24 | 1722.815 | 316.123 | 730.055 | 17678274 | 430137 | 3598935 | 25359 | 1398 | 25956 | 15509322700832126615 | 6959567710591792128 |
| adaptive16_opp10 | 5 | 9 | 27 | 1388.751 | 259.783 | 319.270 | 14764290 | 300858 | 2461776 | 1296 | 84 | 39861 | 6201606973134274868 | 4654120057371283909 |
| adaptive16_opp10 | 6 | 9 | 27 | 1431.550 | 254.103 | 321.688 | 15007614 | 321906 | 2684052 | 1296 | 84 | 40740 | 17607187514542259604 | 10746489409953566585 |
| adaptive16_opp10 | 7 | 9 | 27 | 1595.481 | 263.102 | 318.322 | 15776253 | 381075 | 3393522 | 1350 | 84 | 42708 | 9711819699844517000 | 6669751809884779164 |
| adaptive16_shape | 5 | 4 | 12 | 121.982 | 5.498 | 41.125 | 659088 | 27051 | 220695 | 6 | 0 | 699 | 16862106491899255446 | 17090911097487390677 |
| adaptive16_shape | 6 | 4 | 12 | 174.132 | 7.606 | 41.159 | 924798 | 49605 | 463851 | 6 | 0 | 1590 | 11074444395529033897 | 3133197338037304321 |
| adaptive16_shape | 7 | 4 | 12 | 332.308 | 23.715 | 41.161 | 1775226 | 117024 | 1246860 | 60 | 0 | 3894 | 2058012177191467732 | 8733970900851853078 |
| adaptive16_split | 5 | 10 | 30 | 2003.461 | 313.635 | 737.871 | 21826962 | 456447 | 3487896 | 25668 | 1419 | 47229 | 11224400053047128387 | 2458974580397796028 |
| adaptive16_split | 6 | 10 | 30 | 2073.149 | 321.060 | 750.447 | 22056027 | 474606 | 3698802 | 25668 | 1419 | 47955 | 5285062416457847840 | 6897818971380706463 |
| adaptive16_split | 7 | 10 | 30 | 2197.541 | 312.251 | 742.037 | 22842516 | 536343 | 4423554 | 25722 | 1419 | 50109 | 17397038825450070186 | 8498196892420521694 |
| adaptive16 | 5 | 9 | 27 | 1386.733 | 262.326 | 319.353 | 14764290 | 300858 | 2461776 | 1296 | 84 | 39861 | 6201606973134274868 | 4654120057371283909 |
| adaptive16 | 6 | 9 | 27 | 1438.560 | 254.805 | 318.821 | 15007614 | 321906 | 2684052 | 1296 | 84 | 40740 | 17607187514542259604 | 10746489409953566585 |
| adaptive16 | 7 | 9 | 27 | 1580.552 | 255.427 | 319.369 | 15776253 | 381075 | 3393522 | 1350 | 84 | 42708 | 9711819699844517000 | 6669751809884779164 |

Fixed `exact=16` remains unsafe as a default candidate on this threshold suite:
at depth 7 it exact-solves all 15/16 fixtures, pushes p95 per-position latency
to about 993 ms, and reaches a max of about 1643 ms.

The original current gate (`adaptive16_current`) is much lighter than fixed 16,
but still keeps a heavy 16-empty low-current-mobility root with high opponent
mobility. Adding the opponent mobility cap (`adaptive16_opp10`) reduces depth-7
p95 from 319.651 ms to 263.102 ms and max from 743.875 ms to 318.322 ms while
still exact-solving 5 of the 12 focused 15/16-empty fixtures.

`adaptive16_cap8`, `adaptive16_cap6`, and `adaptive16_split` reduce exactized
roots but still keep the same worst 16-empty opponent-heavy roots, so max latency
stays around 730-767 ms. `adaptive16_shape` is very light, but exactizes none of
the focused 15/16-empty fixtures and collapses back to `exact=14` behavior for
this suite.

## Final Adaptive Root Decisions

Depth 7 by-position rows for the focused threshold fixtures with final
`adaptive16`:

| position | empty | legal current | legal opponent | pass | adaptive16 | skip reason | elapsed ms | nodes | best move | score |
| --- | ---: | ---: | ---: | --- | --- | --- | ---: | ---: | --- | ---: |
| threshold-15-gated-exact | 15 | 10 | 2 | no | yes | - | 255.383 | 2598279 | c7 | -2000 |
| threshold-15-gated-skip | 15 | 12 | 4 | no | no | adaptive_too_many_legal_moves | 10.755 | 46398 | h1 | 113 |
| threshold-15-low-branching | 15 | 3 | 13 | no | no | adaptive_opponent_too_many_legal_moves | 4.338 | 13986 | c1 | -321 |
| threshold-15-parity-ish | 15 | 10 | 3 | no | yes | - | 226.451 | 2613789 | f7 | 45000 |
| threshold-16-low-mobility | 16 | 3 | 11 | no | no | adaptive_opponent_too_many_legal_moves | 4.417 | 11310 | g8 | 164 |
| threshold-16-normal-mobility | 16 | 6 | 12 | no | no | adaptive_opponent_too_many_legal_moves | 7.504 | 30768 | h7 | 88 |
| threshold-16-high-mobility | 16 | 14 | 3 | no | no | adaptive_too_many_legal_moves | 8.766 | 42216 | c1 | 461 |
| threshold-16-root-pass | 16 | 0 | 13 | yes | no | adaptive_root_pass | 4.268 | 15234 | - | -465 |
| threshold-16-corner-choice | 16 | 9 | 8 | no | yes | - | 214.468 | 2204412 | d1 | -14000 |
| threshold-16-corner-race | 16 | 10 | 5 | no | yes | - | 319.369 | 3697179 | g6 | 34000 |
| threshold-16-edge-heavy | 16 | 13 | 5 | no | no | adaptive_too_many_legal_moves | 13.268 | 60309 | h1 | 35 |
| threshold-16-parity-ish | 16 | 4 | 9 | no | yes | - | 255.427 | 3011688 | f8 | -43000 |

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

Keep the default threshold behavior unchanged. Adopt the opponent-mobility guard
inside the experimental `adaptive16` profile because it is substantially lighter
than fixed `exact=16`, improves p95/max over the original current gate, and still
exact-solves a non-trivial subset of 15/16-empty roots. This remains an
experimental opt-in profile; more 15/16 fixtures and match-level checks are
needed before recommending it as a default or normal profile.
