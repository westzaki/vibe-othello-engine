# Experimental Shallow TT Candidate Preset

Date: 2026-06-07

Status: behavior-changing candidate preset added for opt-in match / exact-gate evaluation.
Default, `strong-v1`, public API, evaluator weights, pattern tables, training scripts,
`data/eval/current_default.eval`, retained presets, and committed eval tables were not changed.

## Question

Add an opt-in `experimental-shallow-tt` preset that is intentionally the current `strong-v1` profile plus
shallow TT best-move ordering hints. The preset is not a strength adoption claim. It exists so the
shallow TT hint candidate can be measured through match and exact-gate workflows without mutating
default or `strong-v1`.

## Preset Definition

`experimental-shallow-tt` uses the same main settings as `strong-v1`:

- iterative search
- midgame TT on
- `tt_min_probe_depth=1`
- `tt_min_store_depth=1`
- lazy first-move ordering on
- PVS on
- score-delta-aware aspiration
- adaptive16 exact root policy
- project default evaluator unless the caller passes an evaluator override

The only intended search-option difference is:

- `use_shallow_tt_move_ordering_hint=true`

Shallow TT hints are ordering-only. TT cutoffs still require sufficient stored depth, but the changed
ordering can change alpha-beta work, public best move, PV, result checksum, and work checksum. This
is therefore not a pure speed change.

## Build and Inputs

- Base read before change: latest `origin/main` at `23d2034` (`fix: clarify root candidate score diagnostics (#329)`)
- PR branch update: merged latest `origin/main` at `14416c8`
  (`refactor: centralize pattern training features (#330)`) before the broader gate.
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- Build command: `cmake --build build -j`
- Correctness check: `ctest --test-dir build --output-on-failure`
- Result: 371/371 tests passed.
- Raw output: `runs/perf/strong-v2-candidate-2026-06-07/*.jsonl` (gitignored; directory name
  retained from the initial PR prompt before the preset was renamed)

## Commands

Search benchmark:

```sh
./build/othello_search_bench --preset strong-v1 --depths 5,6,7,8,9 --positions suite --repetitions 3 --exact-endgame-threshold 0 --by-position --format jsonl
./build/othello_search_bench --preset experimental-shallow-tt --depths 5,6,7,8,9 --positions suite --repetitions 3 --exact-endgame-threshold 0 --by-position --format jsonl
```

Match smoke gate:

```sh
./build/othello_match_runner \
  --black search:depth=5,preset=experimental-shallow-tt \
  --white search:depth=5,preset=strong-v1 \
  --games 40 \
  --swap-sides true \
  --seed 20260607 \
  --openings data/openings/smoke_openings.txt \
  --output runs/perf/strong-v2-candidate-2026-06-07/experimental-shallow-tt-vs-strong-v1-match.jsonl \
  --format jsonl

python3 tools/scripts/match_summary.py \
  --input runs/perf/strong-v2-candidate-2026-06-07/experimental-shallow-tt-vs-strong-v1-match.jsonl \
  --by-opening
```

Broader match gate:

```sh
./build/othello_match_runner \
  --black search:depth=5,preset=experimental-shallow-tt \
  --white search:depth=5,preset=strong-v1 \
  --games 200 \
  --swap-sides true \
  --seed 20260607 \
  --openings data/openings/eval_regression_openings.txt \
  --output runs/perf/strong-v2-candidate-2026-06-07/experimental-shallow-tt-vs-strong-v1-depth5-gate.jsonl \
  --format jsonl

python3 tools/scripts/match_summary.py \
  --input runs/perf/strong-v2-candidate-2026-06-07/experimental-shallow-tt-vs-strong-v1-depth5-gate.jsonl \
  --by-opening

./build/othello_match_runner \
  --black search:depth=6,preset=experimental-shallow-tt \
  --white search:depth=6,preset=strong-v1 \
  --games 100 \
  --swap-sides true \
  --seed 20260608 \
  --openings data/openings/eval_regression_openings.txt \
  --output runs/perf/strong-v2-candidate-2026-06-07/experimental-shallow-tt-vs-strong-v1-depth6-gate.jsonl \
  --format jsonl

python3 tools/scripts/match_summary.py \
  --input runs/perf/strong-v2-candidate-2026-06-07/experimental-shallow-tt-vs-strong-v1-depth6-gate.jsonl \
  --by-opening
```

## Search Summary

| depth | preset | elapsed_ms | nodes | nps | eval_calls | beta_cutoffs | beta_first% | ord_builds | lazy_cuts | scored_saved | tt look/hit/% | shallow probe/hit/used | pvs scout/research |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |
| 5 | strong-v1 | 139.147 | 113,436 | 815,225 | 108,315 | 19,464 | 68.79 | 28,239 | 624 | 4,578 | 33,456/4,290/12.82% | 0/0/0 | 8,700/273 |
| 5 | experimental-shallow-tt | 137.068 | 113,196 | 825,838 | 102,483 | 18,612 | 75.44 | 22,083 | 6,033 | 37,755 | 32,904/4,464/13.57% | 7,707/7,680/7,680 | 9,063/279 |
| 6 | strong-v1 | 178.023 | 297,996 | 1,673,920 | 284,076 | 73,065 | 74.01 | 94,119 | 1,650 | 11,346 | 108,201/11,874/10.97% | 0/0/0 | 22,077/549 |
| 6 | experimental-shallow-tt | 179.855 | 283,491 | 1,576,221 | 255,585 | 70,407 | 81.12 | 77,862 | 14,055 | 90,219 | 105,984/13,446/12.69% | 20,028/19,938/19,938 | 21,375/534 |
| 7 | strong-v1 | 319.701 | 977,067 | 3,056,186 | 941,304 | 184,230 | 70.84 | 257,604 | 4,179 | 31,209 | 293,589/30,810/10.49% | 0/0/0 | 79,134/1,116 |
| 7 | experimental-shallow-tt | 336.639 | 933,126 | 2,771,890 | 844,455 | 167,748 | 78.76 | 188,739 | 53,970 | 354,765 | 277,779/33,966/12.23% | 67,695/67,494/67,494 | 80,040/1,095 |
| 8 | strong-v1 | 833.524 | 2,540,400 | 3,047,781 | 2,438,451 | 639,660 | 75.74 | 814,782 | 10,578 | 78,054 | 917,118/89,517/9.76% | 0/0/0 | 191,319/2,055 |
| 8 | experimental-shallow-tt | 690.271 | 2,421,288 | 3,507,737 | 2,206,335 | 613,242 | 81.93 | 669,696 | 116,337 | 786,705 | 885,354/97,005/10.96% | 164,025/163,545/163,545 | 184,065/1,980 |
| 9 | strong-v1 | 2,166.573 | 8,008,077 | 3,696,196 | 7,739,997 | 1,605,879 | 72.31 | 2,197,467 | 28,173 | 218,295 | 2,466,513/235,908/9.56% | 0/0/0 | 642,741/4,386 |
| 9 | experimental-shallow-tt | 1,652.486 | 7,426,509 | 4,494,144 | 6,739,914 | 1,435,752 | 79.64 | 1,593,339 | 425,517 | 2,932,644 | 2,281,611/257,763/11.30% | 534,918/533,730/533,730 | 637,032/3,825 |

## Behavior Changes

| depth | elapsed delta | node delta | nps delta | result checksum changes | best changes | score changes | PV changes | work checksum changes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 | -1.49% | -0.21% | +1.30% | 1/25 | 1/25 | 0/25 | 2/25 | 23/25 |
| 6 | +1.03% | -4.87% | -5.84% | 5/25 | 5/25 | 0/25 | 8/25 | 24/25 |
| 7 | +5.30% | -4.50% | -9.30% | 7/25 | 7/25 | 0/25 | 11/25 | 24/25 |
| 8 | -17.19% | -4.69% | +15.09% | 4/25 | 4/25 | 0/25 | 16/25 | 24/25 |
| 9 | -23.73% | -7.26% | +21.59% | 6/25 | 6/25 | 0/25 | 15/25 | 24/25 |

Public score did not change in this suite run, but public best move and result checksum changed at
every measured depth. PV and work checksum changes were widespread. That confirms the candidate is
behavior-changing, not a pure speed preset.

Best-move changes:

| depth | position | strong-v1 best | experimental-shallow-tt best | score | result checksum | work checksum |
| ---: | --- | --- | --- | ---: | --- | --- |
| 5 | `opening-a1-access` | b2 | c2 | 38 / 38 | changed | changed |
| 6 | `opening-wide-mobility` | e3 | d3 | 184 / 184 | changed | changed |
| 6 | `opening-quiet-development` | c4 | e6 | 133 / 133 | changed | changed |
| 6 | `early-corner-race` | f4 | f3 | 200 / 200 | changed | changed |
| 6 | `midgame-normal-mobility` | d8 | b3 | 440 / 440 | changed | changed |
| 6 | `midgame-wide-x-risk` | h7 | b3 | 321 / 321 | changed | changed |
| 7 | `opening-a1-access` | b2 | c2 | 29 / 29 | changed | changed |
| 7 | `opening-quiet-development` | e6 | d3 | 33 / 33 | changed | changed |
| 7 | `early-balanced-mobility` | a5 | c3 | -94 / -94 | changed | changed |
| 7 | `early-corner-race` | h8 | e2 | 113 / 113 | changed | changed |
| 7 | `midgame-white-wall` | b3 | f5 | 129 / 129 | changed | changed |
| 7 | `midgame-x-risk` | c2 | b4 | 156 / 156 | changed | changed |
| 7 | `midgame-wide-x-risk` | b2 | h7 | 225 / 225 | changed | changed |
| 8 | `opening-quiet-development` | c4 | e6 | 151 / 151 | changed | changed |
| 8 | `early-corner-race` | e2 | c3 | 192 / 192 | changed | changed |
| 8 | `midgame-white-wall` | f5 | b3 | 165 / 165 | changed | changed |
| 8 | `midgame-x-risk` | b4 | b5 | 218 / 218 | changed | changed |
| 9 | `opening-a1-access` | d2 | b2 | 39 / 39 | changed | changed |
| 9 | `opening-quiet-development` | e6 | d3 | 41 / 41 | changed | changed |
| 9 | `early-balanced-mobility` | g3 | c3 | -74 / -74 | changed | changed |
| 9 | `early-corner-race` | c3 | d3 | 156 / 156 | changed | changed |
| 9 | `midgame-white-wall` | b3 | f5 | 83 / 83 | changed | changed |
| 9 | `midgame-normal-mobility` | c7 | d8 | 475 / 475 | changed | changed |

## Match Smoke Gate

This was a small deterministic smoke gate, not a promotion-grade match. Player A was `experimental-shallow-tt`;
player B was `strong-v1`; both used depth 5, the same project default evaluator, the same
`data/openings/smoke_openings.txt` opening set, seed `20260607`, and color swap.

| games | A wins | B wins | draws | avg diff A | errors | avg nodes A | avg nodes B | avg time A ms | avg time B ms |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 40 | 26 | 14 | 0 | +3.80 | 0 | 970,697.28 | 847,349.88 | 96.56 | 89.76 |

By opening:

| opening | games | A wins | B wins | draws | avg diff A |
| --- | ---: | ---: | ---: | ---: | ---: |
| `initial` | 14 | 0 | 14 | 0 | -17.00 |
| `c4-c3` | 14 | 14 | 0 | 0 | +15.00 |
| `d3-c3-c4` | 12 | 12 | 0 | 0 | +15.00 |

The match did not produce negative evidence against keeping the candidate. It is still too small and
opening-sensitive to support a formal strength claim.

## Broader Match Gate

`data/openings/` contains `eval_regression_openings.txt`, a committed opening set broader than
`smoke_openings.txt`. It has 37 named openings, so the broader gate used that set instead of the
smoke set. Player A was `experimental-shallow-tt`; player B was `strong-v1`; both used the same project default
evaluator, equal fixed depth, the same opening set, deterministic seeds, and color swap.

| depth | games | A wins | B wins | draws | avg diff A | errors | avg nodes A | avg nodes B | avg time A ms | avg time B ms | exact roots A/B |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 5 | 200 | 96 | 104 | 0 | +0.10 | 0 | 1,025,583.45 | 1,125,651.45 | 96.82 | 106.17 | 1,524/1,536 |
| 6 | 100 | 45 | 53 | 2 | -5.46 | 0 | 1,184,198.69 | 1,174,989.37 | 129.12 | 136.31 | 761/776 |

Depth 5 is close: `experimental-shallow-tt` lost the win count by 8 games but had nearly even average disc
difference. It also used fewer nodes and less time on average in this run. This is enough to keep
the candidate alive, but not enough to recommend it.

Depth 6 is negative evidence: `experimental-shallow-tt` lost 45-53-2 and average disc difference was -5.46 from
the `experimental-shallow-tt` perspective. Average time was lower, but average nodes were slightly higher. This
argues against any formal strength recommendation in this PR.

Depth 7+ was not run for this PR update. The depth 6 broader gate already produced negative evidence
large enough to block recommendation, and the goal here is to decide whether the candidate remains
worth measuring, not to promote it.

Depth 5 by opening:

| opening | games | A wins | B wins | draws | avg diff A |
| --- | ---: | ---: | ---: | ---: | ---: |
| `initial` | 6 | 0 | 6 | 0 | -17.00 |
| `g0_p0_4_s0` | 6 | 6 | 0 | 0 | +33.00 |
| `g1_p0_4_s1` | 6 | 6 | 0 | 0 | +35.00 |
| `g2_p0_4_s2` | 6 | 3 | 3 | 0 | +22.00 |
| `g3_p0_4_s3` | 6 | 3 | 3 | 0 | -12.00 |
| `g4_p0_4_s4` | 6 | 6 | 0 | 0 | +11.00 |
| `g5_p0_4_s5` | 6 | 6 | 0 | 0 | +16.00 |
| `g6_p0_4_s6` | 6 | 3 | 3 | 0 | -8.00 |
| `g7_p0_4_s7` | 6 | 6 | 0 | 0 | +41.00 |
| `g8_p0_4_s8` | 6 | 0 | 6 | 0 | -22.00 |
| `g9_p0_4_s9` | 6 | 0 | 6 | 0 | -15.00 |
| `g10_p0_4_s10` | 6 | 3 | 3 | 0 | +9.00 |
| `g11_p0_4_s11` | 6 | 0 | 6 | 0 | -41.00 |
| `g12_p0_5_s0` | 6 | 3 | 3 | 0 | +7.00 |
| `g13_p0_5_s1` | 6 | 0 | 6 | 0 | -13.00 |
| `g14_p0_5_s2` | 6 | 3 | 3 | 0 | -27.00 |
| `g15_p0_5_s3` | 6 | 3 | 3 | 0 | -15.00 |
| `g16_p0_5_s4` | 6 | 0 | 6 | 0 | -18.00 |
| `g17_p0_5_s5` | 6 | 3 | 3 | 0 | -8.00 |
| `g18_p0_5_s6` | 6 | 3 | 3 | 0 | -14.00 |
| `g19_p0_5_s7` | 6 | 6 | 0 | 0 | +41.00 |
| `g20_p0_5_s8` | 6 | 3 | 3 | 0 | +1.00 |
| `g21_p0_5_s9` | 6 | 3 | 3 | 0 | +3.00 |
| `g22_p0_5_s10` | 6 | 0 | 6 | 0 | -16.00 |
| `g23_p0_5_s11` | 6 | 0 | 6 | 0 | -41.00 |
| `g24_p0_6_s0` | 6 | 3 | 3 | 0 | +20.00 |
| `g25_p0_6_s1` | 4 | 0 | 4 | 0 | -6.00 |
| `g26_p0_6_s2` | 4 | 2 | 2 | 0 | 0.00 |
| `g27_p0_6_s3` | 4 | 2 | 2 | 0 | -15.00 |
| `g28_p0_6_s4` | 4 | 0 | 4 | 0 | -37.00 |
| `g29_p0_6_s5` | 4 | 2 | 2 | 0 | +2.00 |
| `g30_p0_6_s6` | 4 | 4 | 0 | 0 | +45.00 |
| `g31_p0_6_s7` | 4 | 4 | 0 | 0 | +41.00 |
| `g32_p0_6_s8` | 4 | 2 | 2 | 0 | +3.00 |
| `g33_p0_6_s9` | 4 | 4 | 0 | 0 | +10.00 |
| `g34_p0_6_s10` | 4 | 4 | 0 | 0 | +21.00 |
| `g35_p0_6_s11` | 4 | 0 | 4 | 0 | -17.00 |

Depth 6 by opening:

| opening | games | A wins | B wins | draws | avg diff A |
| --- | ---: | ---: | ---: | ---: | ---: |
| `initial` | 4 | 2 | 2 | 0 | -20.00 |
| `g0_p0_4_s0` | 4 | 4 | 0 | 0 | +47.00 |
| `g1_p0_4_s1` | 4 | 4 | 0 | 0 | +9.00 |
| `g2_p0_4_s2` | 4 | 2 | 2 | 0 | +1.00 |
| `g3_p0_4_s3` | 4 | 2 | 2 | 0 | +1.00 |
| `g4_p0_4_s4` | 4 | 2 | 2 | 0 | +15.00 |
| `g5_p0_4_s5` | 4 | 2 | 2 | 0 | +12.00 |
| `g6_p0_4_s6` | 4 | 2 | 0 | 2 | +2.00 |
| `g7_p0_4_s7` | 4 | 2 | 2 | 0 | -14.00 |
| `g8_p0_4_s8` | 4 | 2 | 2 | 0 | -17.00 |
| `g9_p0_4_s9` | 4 | 0 | 4 | 0 | -30.00 |
| `g10_p0_4_s10` | 4 | 0 | 4 | 0 | -28.00 |
| `g11_p0_4_s11` | 4 | 0 | 4 | 0 | -39.00 |
| `g12_p0_5_s0` | 2 | 2 | 0 | 0 | +20.00 |
| `g13_p0_5_s1` | 2 | 0 | 2 | 0 | -23.00 |
| `g14_p0_5_s2` | 2 | 1 | 1 | 0 | -25.00 |
| `g15_p0_5_s3` | 2 | 1 | 1 | 0 | +1.00 |
| `g16_p0_5_s4` | 2 | 0 | 2 | 0 | -31.00 |
| `g17_p0_5_s5` | 2 | 0 | 2 | 0 | -27.00 |
| `g18_p0_5_s6` | 2 | 1 | 1 | 0 | -10.00 |
| `g19_p0_5_s7` | 2 | 0 | 2 | 0 | -21.00 |
| `g20_p0_5_s8` | 2 | 1 | 1 | 0 | +6.00 |
| `g21_p0_5_s9` | 2 | 1 | 1 | 0 | +19.00 |
| `g22_p0_5_s10` | 2 | 1 | 1 | 0 | -20.00 |
| `g23_p0_5_s11` | 2 | 1 | 1 | 0 | -26.00 |
| `g24_p0_6_s0` | 2 | 0 | 2 | 0 | -28.00 |
| `g25_p0_6_s1` | 2 | 0 | 2 | 0 | -23.00 |
| `g26_p0_6_s2` | 2 | 1 | 1 | 0 | -25.00 |
| `g27_p0_6_s3` | 2 | 1 | 1 | 0 | +5.00 |
| `g28_p0_6_s4` | 2 | 1 | 1 | 0 | +14.00 |
| `g29_p0_6_s5` | 2 | 1 | 1 | 0 | +3.00 |
| `g30_p0_6_s6` | 2 | 1 | 1 | 0 | -10.00 |
| `g31_p0_6_s7` | 2 | 1 | 1 | 0 | +27.00 |
| `g32_p0_6_s8` | 2 | 1 | 1 | 0 | -12.00 |
| `g33_p0_6_s9` | 2 | 2 | 0 | 0 | +30.00 |
| `g34_p0_6_s10` | 2 | 2 | 0 | 0 | +31.00 |
| `g35_p0_6_s11` | 2 | 1 | 1 | 0 | -26.00 |

The broader opening set reduces the extreme three-opening smoke split, but opening sensitivity
remains visible. Several openings are still lopsided in opposite directions even with color swap.
That makes the result useful as a gate check, not as a strength claim.

## Exact / Eval Gate

Exact gate was not run in this update. Reason: existing committed exact labels cover only tiny
terminal or near-terminal fixtures, while the changed-best rows prioritized here
(`opening-a1-access`, `early-corner-race`, `midgame-normal-mobility`, `midgame-white-wall`,
`midgame-wide-x-risk`, and `opening-quiet-development`) are opening or midgame positions with far
more empties. The committed tooling can dump new exact labels, but doing so safely for these rows
would require new long exact solves or a new exact dataset, which is outside this PR update.

## Decision

1. `experimental-shallow-tt` is faster than `strong-v1` at depths 8 and 9 in this run, slightly faster at depth 5,
   and slower by wall-clock at depths 6 and 7 despite lower node counts.
2. Public score did not change on the measured suite rows.
3. Best move, PV, result checksum, and work checksum changed often enough that this is clearly a
   behavior-changing preset.
4. The small match smoke favored `experimental-shallow-tt` overall, but the opening split was extreme and should
   not be overread.
5. The broader depth 5 gate did not clearly refute the candidate, but the broader depth 6 gate is
   negative evidence against recommendation.
6. `experimental-shallow-tt` is worth keeping as an opt-in candidate because it creates a named, reversible target
   for continued match and exact-gate evaluation.
7. `experimental-shallow-tt` should not be formally recommended yet. Strength claims should wait for stronger
   match evidence and exact-gate evidence on changed-best rows.
8. The preset was renamed from `strong-v2` to `experimental-shallow-tt` in this PR update because
   the broader gate does not support a strength-sounding name.
9. Default and `strong-v1` should remain unchanged.

## Next Checks

- Add exact-label or exact-gate evidence for rows whose best move changes under equal public score.
- Run a larger follow-up match gate before any recommendation, especially at depth 6.
- Investigate depth 6/7 match weakness and wall-clock regressions before claiming a general speed or
  strength win.
