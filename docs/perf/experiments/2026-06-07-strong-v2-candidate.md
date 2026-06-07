# Strong-v2 Candidate Preset

Date: 2026-06-07

Status: behavior-changing candidate preset added for opt-in match / exact-gate evaluation.
Default, `strong-v1`, public API, evaluator weights, pattern tables, training scripts,
`data/eval/current_default.eval`, retained presets, and committed eval tables were not changed.

## Question

Add an opt-in `strong-v2` preset that is intentionally the current `strong-v1` profile plus
shallow TT best-move ordering hints. The preset is not a strength adoption claim. It exists so the
shallow TT hint candidate can be measured through match and exact-gate workflows without mutating
default or `strong-v1`.

## Preset Definition

`strong-v2` uses the same main settings as `strong-v1`:

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
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- Build command: `cmake --build build -j`
- Correctness check: `ctest --test-dir build --output-on-failure`
- Result: 371/371 tests passed.
- Raw output: `runs/perf/strong-v2-candidate-2026-06-07/*.jsonl` (gitignored)

## Commands

Search benchmark:

```sh
./build/othello_search_bench --preset strong-v1 --depths 5,6,7,8,9 --positions suite --repetitions 3 --exact-endgame-threshold 0 --by-position --format jsonl
./build/othello_search_bench --preset strong-v2 --depths 5,6,7,8,9 --positions suite --repetitions 3 --exact-endgame-threshold 0 --by-position --format jsonl
```

Match smoke gate:

```sh
./build/othello_match_runner \
  --black search:depth=5,preset=strong-v2 \
  --white search:depth=5,preset=strong-v1 \
  --games 40 \
  --swap-sides true \
  --seed 20260607 \
  --openings data/openings/smoke_openings.txt \
  --output runs/perf/strong-v2-candidate-2026-06-07/strong-v2-vs-strong-v1-match.jsonl \
  --format jsonl

python3 tools/scripts/match_summary.py \
  --input runs/perf/strong-v2-candidate-2026-06-07/strong-v2-vs-strong-v1-match.jsonl \
  --by-opening
```

## Search Summary

| depth | preset | elapsed_ms | nodes | nps | eval_calls | beta_cutoffs | beta_first% | ord_builds | lazy_cuts | scored_saved | tt look/hit/% | shallow probe/hit/used | pvs scout/research |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |
| 5 | strong-v1 | 139.147 | 113,436 | 815,225 | 108,315 | 19,464 | 68.79 | 28,239 | 624 | 4,578 | 33,456/4,290/12.82% | 0/0/0 | 8,700/273 |
| 5 | strong-v2 | 137.068 | 113,196 | 825,838 | 102,483 | 18,612 | 75.44 | 22,083 | 6,033 | 37,755 | 32,904/4,464/13.57% | 7,707/7,680/7,680 | 9,063/279 |
| 6 | strong-v1 | 178.023 | 297,996 | 1,673,920 | 284,076 | 73,065 | 74.01 | 94,119 | 1,650 | 11,346 | 108,201/11,874/10.97% | 0/0/0 | 22,077/549 |
| 6 | strong-v2 | 179.855 | 283,491 | 1,576,221 | 255,585 | 70,407 | 81.12 | 77,862 | 14,055 | 90,219 | 105,984/13,446/12.69% | 20,028/19,938/19,938 | 21,375/534 |
| 7 | strong-v1 | 319.701 | 977,067 | 3,056,186 | 941,304 | 184,230 | 70.84 | 257,604 | 4,179 | 31,209 | 293,589/30,810/10.49% | 0/0/0 | 79,134/1,116 |
| 7 | strong-v2 | 336.639 | 933,126 | 2,771,890 | 844,455 | 167,748 | 78.76 | 188,739 | 53,970 | 354,765 | 277,779/33,966/12.23% | 67,695/67,494/67,494 | 80,040/1,095 |
| 8 | strong-v1 | 833.524 | 2,540,400 | 3,047,781 | 2,438,451 | 639,660 | 75.74 | 814,782 | 10,578 | 78,054 | 917,118/89,517/9.76% | 0/0/0 | 191,319/2,055 |
| 8 | strong-v2 | 690.271 | 2,421,288 | 3,507,737 | 2,206,335 | 613,242 | 81.93 | 669,696 | 116,337 | 786,705 | 885,354/97,005/10.96% | 164,025/163,545/163,545 | 184,065/1,980 |
| 9 | strong-v1 | 2,166.573 | 8,008,077 | 3,696,196 | 7,739,997 | 1,605,879 | 72.31 | 2,197,467 | 28,173 | 218,295 | 2,466,513/235,908/9.56% | 0/0/0 | 642,741/4,386 |
| 9 | strong-v2 | 1,652.486 | 7,426,509 | 4,494,144 | 6,739,914 | 1,435,752 | 79.64 | 1,593,339 | 425,517 | 2,932,644 | 2,281,611/257,763/11.30% | 534,918/533,730/533,730 | 637,032/3,825 |

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

| depth | position | strong-v1 best | strong-v2 best | score | result checksum | work checksum |
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

This was a small deterministic smoke gate, not a promotion-grade match. Player A was `strong-v2`;
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

## Decision

1. `strong-v2` is faster than `strong-v1` at depths 8 and 9 in this run, slightly faster at depth 5,
   and slower by wall-clock at depths 6 and 7 despite lower node counts.
2. Public score did not change on the measured suite rows.
3. Best move, PV, result checksum, and work checksum changed often enough that this is clearly a
   behavior-changing preset.
4. The small match smoke favored `strong-v2` overall, but the opening split was extreme and should
   not be overread.
5. `strong-v2` is worth keeping as an opt-in candidate because it creates a named, reversible target
   for broader match and exact-gate evaluation.
6. `strong-v2` should not be formally recommended yet. Strength claims should wait for broader
   match and exact-gate evidence.
7. Default and `strong-v1` should remain unchanged.

## Next Checks

- Run a broader, less opening-sensitive match gate before any recommendation.
- Add exact-label or exact-gate evidence for rows whose best move changes under equal public score.
- Investigate depth 6/7 wall-clock regressions despite lower node counts before claiming a general
  speed win.
