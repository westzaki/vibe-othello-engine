# Shallow TT hint root tie analysis

Date: 2026-06-07

## Purpose

PR #322 found that `shallow_tt_move_ordering_hint=on` is often faster than the
same search profile with the hint disabled, but it also changes best moves, PVs,
result checksums, and work checksums. This follow-up isolates the rows where the
public best move changed and checks whether those changes look like equal-score
root tie-break differences or score-changing search behavior.

This report does not change public API, presets, evaluator weights, pattern
tables, or training scripts.

## Compared revision and build

- Baseline main: `807ede6` (`origin/main`)
- Report branch head: `d29bc55`
- Build type: `Release`

The benchmark JSONL reused the PR #322 shallow-hint behavior run under
`runs/perf/shallow-tt-hint-behavior-2026-06-07/`. Raw JSONL remains untracked.

## Commands

Benchmark rows reused from PR #322:

```sh
./build/othello_search_bench --mode iterative --depths 5,6,7,8,9 --positions suite --repetitions 3 --tt on --pvs on --aspiration on --aspiration-profile score-delta-aware --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --shallow-tt-move-ordering-hint off --exact-endgame-threshold 0 --by-position --format jsonl

./build/othello_search_bench --mode iterative --depths 5,6,7,8,9 --positions suite --repetitions 3 --tt on --pvs on --aspiration on --aspiration-profile score-delta-aware --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --shallow-tt-move-ordering-hint on --exact-endgame-threshold 0 --by-position --format jsonl
```

Additional root-candidate probes for this report:

```sh
for depth in 5 6 7 8 9; do
  ./build/othello_analyze_position --stdin --batch-jsonl --root-candidates --mode fixed --depth "$depth" --tt on --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --shallow-tt-move-ordering-hint off --pvs on --exact-endgame-threshold 0 < "runs/perf/shallow-tt-hint-behavior-2026-06-07/root-candidates-depth${depth}.jsonl" > "runs/perf/shallow-tt-hint-behavior-2026-06-07/root-scores-strong-fixed-off-depth${depth}.jsonl"

  ./build/othello_analyze_position --stdin --batch-jsonl --root-candidates --mode fixed --depth "$depth" --tt on --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --shallow-tt-move-ordering-hint on --pvs on --exact-endgame-threshold 0 < "runs/perf/shallow-tt-hint-behavior-2026-06-07/root-candidates-depth${depth}.jsonl" > "runs/perf/shallow-tt-hint-behavior-2026-06-07/root-scores-strong-fixed-on-depth${depth}.jsonl"
done
```

Limitation: `othello_analyze_position` does not expose
`--aspiration-profile score-delta-aware`, so the root-candidate probes are
diagnostic. Fixed-depth probes do not use aspiration. Iterative root-candidate
probes from the PR #322 run are included as a cross-check.

## Performance summary

Strong-v1-equivalent profile, by-position suite, repetitions 3:

| Profile | elapsed_ms | nodes | nps |
|---|---:|---:|---:|
| hint off | 3611.249 | 11936976 | 3305498 |
| hint on | 3206.072 | 11177610 | 3486388 |
| hint on / off | 0.888x | 0.936x | 1.055x |

The aggregate remains favorable for hint-on, but 55 of 135 by-position rows were
slower with the hint enabled.

## Target rows

Rows with public best, score, or PV differences:

| Difference type | Rows |
|---|---:|
| best move changed, score unchanged | 23 |
| score changed | 0 |
| PV-only changed | 29 |
| result-checksum-only changed | 0 |
| work-checksum-only changed | 67 |

The target positions that had best/score/PV differences were:

`early-balanced-mobility`, `early-corner-race`, `early-tight-mobility`,
`late-black-pass`, `late-corner-swing`, `late-edge-heavy`,
`midgame-balanced-count`, `midgame-lopsided-edge`, `midgame-low-mobility`,
`midgame-normal-mobility`, `midgame-white-wall`, `midgame-wide-x-risk`,
`midgame-x-risk`, `opening-a1-access`, `opening-central-run`,
`opening-quiet-development`, and `opening-wide-mobility`.

## Best-move change classification

The benchmark score stayed identical in every best-change row. However, the
independent fixed-depth root legal-move score table did not give the old and new
best moves the same score in any of those rows. Therefore, under the requested
classification rule:

| Classification | Rows |
|---|---:|
| tie-order difference | 0 |
| score-changing behavior difference | 23 |

Fixed-depth root-candidate scores were identical with hint off and hint on for
all 23 rows. Iterative root-candidate parent rows reproduced the public
off/on best-move changes, while their `root_scores` maps matched the fixed-depth
tables. This means the observed best-move changes are not explained by an
order-independent root score tie in the candidate table.

## Root legal move score table

`Bench score` is the public iterative search score from the benchmark. `Fixed
old/new` and `Iter old/new` are the independently reported root-candidate scores
for the off-best and on-best moves.

| Depth | Position | Off best | On best | Bench score | Fixed old/new | Iter old/new | Class |
|---:|---|---|---|---:|---:|---:|---|
| 5 | opening-a1-access | b2 | c2 | 38 -> 38 | -46 / 38 | -46 / 38 | score-changing behavior difference |
| 6 | early-corner-race | f4 | f3 | 200 -> 200 | 100 / 93 | 100 / 93 | score-changing behavior difference |
| 6 | midgame-normal-mobility | d8 | b3 | 440 -> 440 | 440 / 234 | 440 / 234 | score-changing behavior difference |
| 6 | midgame-wide-x-risk | h7 | b3 | 321 -> 321 | 321 / 260 | 321 / 260 | score-changing behavior difference |
| 6 | opening-quiet-development | c4 | e6 | 133 -> 133 | 93 / 133 | 93 / 133 | score-changing behavior difference |
| 6 | opening-wide-mobility | e3 | d3 | 184 -> 184 | 129 / 109 | 129 / 109 | score-changing behavior difference |
| 7 | early-balanced-mobility | a5 | c3 | -94 -> -94 | -94 / -244 | -94 / -244 | score-changing behavior difference |
| 7 | early-corner-race | h8 | e2 | 113 -> 113 | 113 / -31 | 113 / -31 | score-changing behavior difference |
| 7 | midgame-white-wall | b3 | f5 | 129 -> 129 | 115 / 129 | 115 / 129 | score-changing behavior difference |
| 7 | midgame-wide-x-risk | b2 | h7 | 225 -> 225 | 61 / 225 | 61 / 225 | score-changing behavior difference |
| 7 | midgame-x-risk | c2 | b4 | 156 -> 156 | 148 / 142 | 148 / 142 | score-changing behavior difference |
| 7 | opening-a1-access | b2 | c2 | 29 -> 29 | -49 / 22 | -49 / 22 | score-changing behavior difference |
| 7 | opening-quiet-development | e6 | d3 | 33 -> 33 | 33 / 2 | 33 / 2 | score-changing behavior difference |
| 8 | early-corner-race | e2 | c3 | 192 -> 192 | 31 / 93 | 31 / 93 | score-changing behavior difference |
| 8 | midgame-white-wall | f5 | b3 | 165 -> 165 | 165 / 123 | 165 / 123 | score-changing behavior difference |
| 8 | midgame-x-risk | b4 | b5 | 218 -> 218 | 182 / 103 | 182 / 103 | score-changing behavior difference |
| 8 | opening-quiet-development | c4 | e6 | 151 -> 151 | 91 / 151 | 91 / 151 | score-changing behavior difference |
| 9 | early-balanced-mobility | g3 | c3 | -74 -> -74 | -283 / -245 | -283 / -245 | score-changing behavior difference |
| 9 | early-corner-race | c3 | d3 | 156 -> 156 | 31 / -27 | 31 / -27 | score-changing behavior difference |
| 9 | midgame-normal-mobility | c7 | d8 | 475 -> 475 | 425 / 475 | 425 / 475 | score-changing behavior difference |
| 9 | midgame-white-wall | b3 | f5 | 83 -> 83 | 7 / 83 | 7 / 83 | score-changing behavior difference |
| 9 | opening-a1-access | d2 | b2 | 39 -> 39 | 39 / -38 | 39 / -38 | score-changing behavior difference |
| 9 | opening-quiet-development | e6 | d3 | 41 -> 41 | 41 / 10 | 41 / 10 | score-changing behavior difference |

Condensed root-score tables, sorted by score descending:

| Depth | Position | Fixed root scores |
|---:|---|---|
| 5 | opening-a1-access | c2:38, d2:36, d6:30, f6:18, f7:-7, a8:-14, e7:-45, b2:-46 |
| 6 | early-corner-race | h8:200, d6:106, g5:106, e7:105, f4:100, c3:95, f7:94, f3:93, e2:39, d3:32 |
| 6 | midgame-normal-mobility | d8:440, c7:428, h4:428, d6:423, f4:415, g5:415, c5:321, b3:234 |
| 6 | midgame-wide-x-risk | h7:321, d3:271, b3:260, d7:259, e7:251, g5:247, d8:241, a3:240, e3:237, c7:217, c2:201, a4:190, b2:119 |
| 6 | opening-quiet-development | e6:133, c4:93, d3:93, h7:-99 |
| 6 | opening-wide-mobility | f4:184, f3:136, e3:129, d3:109, c2:106, f7:105, c7:99, b7:17, g7:4, a1:3 |
| 7 | early-balanced-mobility | a5:-94, b3:-160, d6:-172, c7:-230, c3:-244, g5:-247, g3:-299 |
| 7 | early-corner-race | h8:113, d6:32, g5:32, e7:30, f4:30, f3:27, c3:16, f7:14, e2:-31, d3:-37 |
| 7 | midgame-white-wall | f5:129, b5:122, b3:115, d3:104, g6:77 |
| 7 | midgame-wide-x-risk | h7:225, e7:175, d3:173, d7:173, g5:170, b3:167, d8:158, c7:157, e3:151, a3:141, c2:118, a4:71, b2:61 |
| 7 | midgame-x-risk | e8:156, c2:148, h2:146, b4:142, c1:118, c8:116, d8:104, b5:56, g2:54 |
| 7 | opening-a1-access | d6:29, d2:24, c2:22, f6:14, f7:7, a8:-22, e7:-39, b2:-49 |
| 7 | opening-quiet-development | e6:33, c4:8, d3:2, h7:-196 |
| 8 | early-corner-race | h8:192, e7:107, g5:107, f7:96, f4:94, c3:93, d6:87, f3:81, d3:39, e2:31 |
| 8 | midgame-white-wall | f5:165, b5:157, b3:123, g6:121, d3:120 |
| 8 | midgame-x-risk | e8:218, c1:209, c8:194, c2:193, b4:182, h2:167, d8:166, b5:103, g2:92 |
| 8 | opening-quiet-development | e6:151, d3:92, c4:91, h7:-96 |
| 9 | early-balanced-mobility | a5:-74, b3:-156, d6:-175, g5:-232, c7:-239, c3:-245, g3:-283 |
| 9 | early-corner-race | h8:156, e7:50, f4:43, f7:43, g5:43, d6:42, f3:41, c3:31, d3:-27, e2:-28 |
| 9 | midgame-normal-mobility | d8:475, d6:430, g5:430, c7:425, h4:410, f4:387, c5:281, b3:160 |
| 9 | midgame-white-wall | f5:83, d3:47, b5:44, g6:26, b3:7 |
| 9 | opening-a1-access | d2:39, c2:35, d6:15, f6:15, f7:14, a8:-14, e7:-30, b2:-38 |
| 9 | opening-quiet-development | e6:41, c4:10, d3:10, h7:-200 |

## PV-only and checksum-only rows

PV-only differences were 29 rows. In all of them, public score and best move
were unchanged, while the displayed PV changed. These are behavior-visible PV
changes, not result-score changes.

Result-checksum-only differences were 0 rows. Every changed public result
checksum in this comparison was explained by either a best-move change or a PV
change. Work-checksum-only differences were 67 rows, consistent with changed
interior ordering and cutoff paths without public best/score/PV changes.

## Latency regression rows

Hint-on was slower in 55 of 135 by-position rows. The largest strong-v1-like
regressions were:

| Depth | Position | elapsed off -> on | nodes off -> on | lazy cuts off -> on | saved moves off -> on | shallow used | PVS researches off -> on |
|---:|---|---:|---:|---:|---:|---:|---:|
| 8 | late-wide-mobility | 44.036 -> 86.755 | 116457 -> 120015 | 24 -> 8994 | 234 -> 38277 | 13563 | 9 -> 9 |
| 8 | early-corner-race | 19.128 -> 30.574 | 60093 -> 119631 | 354 -> 5103 | 2334 -> 34059 | 6483 | 36 -> 138 |
| 9 | midgame-white-wall | 25.091 -> 36.881 | 81297 -> 134979 | 537 -> 8265 | 3417 -> 58284 | 9894 | 84 -> 183 |
| 5 | late-open-corner | 5.453 -> 7.944 | 7851 -> 7743 | 24 -> 501 | 249 -> 2706 | 582 | 18 -> 18 |
| 5 | midgame-x-risk | 8.378 -> 11.372 | 12585 -> 18555 | 117 -> 588 | 1011 -> 4533 | 639 | 18 -> 39 |
| 8 | midgame-normal-mobility | 18.783 -> 25.299 | 57978 -> 86217 | 33 -> 4353 | 204 -> 24591 | 6366 | 33 -> 66 |
| 7 | midgame-pass-edge | 5.324 -> 7.137 | 5091 -> 6285 | 33 -> 357 | 210 -> 1560 | 507 | 12 -> 18 |
| 9 | midgame-normal-mobility | 49.782 -> 64.691 | 210708 -> 311676 | 78 -> 23199 | 471 -> 135210 | 27135 | 69 -> 93 |

The regression rows often still show many lazy cuts and saved scored moves, but
some also increase nodes and PVS researches. The hint can move a promising
shallow TT move earlier, but when that move is not the final best root path it
can change interior cutoff shape enough to increase total work.

## Correctness vs performance interpretation

Correctness:

- There were no benchmark score differences between hint off and on.
- Public best moves changed in 23 rows, and those changes are not supported by
  equal root-candidate scores in the diagnostic table.
- Some public benchmark best moves do not match the best move implied by the
  independently reported root-candidate scores. Example: depth 6
  `early-corner-race` reports public score 200 with `f4` or `f3` as best, while
  the root-candidate table gives `h8:200`, `f4:100`, and `f3:93`.
- This is a behavior question before it is a speed question. The current data
  does not justify treating shallow TT hint as pure speed.

Performance:

- Aggregate speed is still favorable for hint-on in the strong-v1-like profile.
- Individual latency regressions exist and correlate with changed interior
  ordering, more nodes in some rows, and more PVS researches in several of the
  largest regressions.

## Promotion decision

- Do not enable shallow TT hint in default.
- Do not enable shallow TT hint in strong-v1.
- Do not promote shallow TT hint into a strong-v2 preset yet. The speed signal is
  real, but the best-move changes are not demonstrated to be tie-order-only.
- An experimental-only preset could be useful for continued testing, but it
  should be labeled behavior-changing rather than pure performance.

## Tie behavior recommendation

Order-independent root behavior should be investigated before any non-
experimental promotion. A root-only fix is the first thing to try because the
public best move, public score, result checksum, and first PV move are root-
visible outputs. If the goal is only stable public best/result behavior, root
candidate confirmation may be sufficient.

All-node order independence would be much more expensive and is probably not the
right first step. It would risk removing much of the benefit from alpha-beta,
PVS, lazy first-move ordering, and shallow TT hints. If PV stability beyond the
root is required, a narrower canonical-PV reconstruction or root-confirmation
pass should be evaluated before full all-node tie normalization.

Potential speed risk:

- Root-only confirmation can add one or more full-window root candidate searches
  when scores are close or when a scout result is not enough to prove ordering.
- All-node canonical tie behavior would likely have high speed risk because it
  weakens cutoff dependence on move order throughout the tree.

## Fix proposal, not implemented here

Add a separate correctness-focused investigation for root result consistency:

1. Add a benchmark/debug-only way to compare the public root result with the
   independently searched root-candidate table for selected positions.
2. Add regression fixtures for the rows above where the public best does not
   match the best independently searched root candidate.
3. Evaluate a root-only confirmation rule that makes public best selection
   deterministic across ordering changes before revisiting shallow TT hint
   promotion.

