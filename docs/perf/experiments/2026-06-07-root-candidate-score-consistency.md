# Root candidate score consistency

Date: 2026-06-07

Status: diagnostics/reporting fix only. Engine search semantics, public API, defaults,
`strong-v1`, evaluator weights, pattern tables, and training scripts were not changed.

## Question

PR #322 classified 23 shallow TT hint off/on best-move changes as not simple root tie-breaks because
the benchmark public score stayed equal while `othello_analyze_position --root-candidates` reported
old/new best moves with different scores. This report checks whether that table has the same
identity as the parent search result.

## Finding

`root_scores` are root-perspective scores. The analyzer applies one legal root move, searches the
child position, and reports `-child_search.score`. The negamax sign is correct.

However, the table is an independent child-search table, not the parent root search's internal
candidate table. It does not share one root search context across siblings, and before this change
the analyzer also could not accept `--aspiration-profile score-delta-aware`, so it could not exactly
mirror the benchmark options used in #322.

Therefore:

- `root_scores` can be used as a legal-move independent probe.
- `root_scores` top score often equals the parent result score.
- `root_scores` top move is not guaranteed to equal `SearchResult::best_move`.
- A parent `best_move` / `root_scores` top mismatch is not, by itself, proof that the old/new
  benchmark best moves are score-changing behavior.

## Fix

Tooling-only changes:

- Added `--aspiration-profile fixed|score-delta-aware` to `othello_analyze_position`.
- Added batch JSONL fields:
  - `root_score_semantics`: `root_perspective_independent_child_search`
  - `root_score_best_move`
  - `root_score_best_score`
  - `root_score_for_result_best_move`
  - `root_scores_match_result`

This makes mismatches explicit instead of letting reports infer that `best_move` and `root_scores`
share one score identity.

## Reproduction positions

Boards are from the search benchmark suite.

`opening-a1-access`

```text
........
.B......
..B.B...
...BB...
.WWWWW..
..B.....
........
........
side=W
```

`early-corner-race`

```text
........
...W...W
....WBBB
...BBW..
...WW...
....W...
........
........
side=B
```

`midgame-white-wall`

```text
WWWWW.W.
WWWWWW..
BWWWWW..
..WBW...
.WWWWW..
W.W.WBBB
..W....B
..W....B
side=B
```

`midgame-normal-mobility`

```text
....WBBB
...WWWBW
....WBWW
...WWW.W
..WWW...
..B.W...
....W...
....W...
side=B
```

## Commands

Benchmark public rows:

```sh
./build/othello_search_bench --mode iterative --depths 5,6,7,9 --positions suite --repetitions 1 --tt on --pvs on --aspiration on --aspiration-profile score-delta-aware --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --shallow-tt-move-ordering-hint off --exact-endgame-threshold 0 --by-position --format jsonl
./build/othello_search_bench --mode iterative --depths 5,6,7,9 --positions suite --repetitions 1 --tt on --pvs on --aspiration on --aspiration-profile score-delta-aware --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --shallow-tt-move-ordering-hint on --exact-endgame-threshold 0 --by-position --format jsonl
```

Analyzer rows, using a JSONL file containing the four positions above:

```sh
./build/othello_analyze_position --stdin --batch-jsonl --root-candidates --mode iterative --depth N --tt on --pvs on --aspiration on --aspiration-profile score-delta-aware --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --shallow-tt-move-ordering-hint off --exact-endgame-threshold 0 < runs/perf/root-candidate-score-consistency-2026-06-07/representatives.jsonl
./build/othello_analyze_position --stdin --batch-jsonl --root-candidates --mode iterative --depth N --tt on --pvs on --aspiration on --aspiration-profile score-delta-aware --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --shallow-tt-move-ordering-hint on --exact-endgame-threshold 0 < runs/perf/root-candidate-score-consistency-2026-06-07/representatives.jsonl
```

Minimal fixed-depth mismatch check:

```sh
./build/othello_analyze_position --stdin --batch-jsonl --root-candidates --mode fixed --depth 5 --tt off --pvs off --aspiration off --lazy-first-move-ordering off --shallow-tt-move-ordering-hint off --exact-endgame-threshold 0 < runs/perf/root-candidate-score-consistency-2026-06-07/representatives.jsonl
```

## Public result vs analyzer result

After adding `--aspiration-profile`, analyzer parent `best_move`, `score`, and `nodes` matched the
benchmark public rows for the sampled positions with the same options. Examples:

| Hint | Depth | Position | Benchmark best/score/nodes | Analyzer best/score/nodes |
|---|---:|---|---|---|
| off | 5 | opening-a1-access | b2 / 38 / 3103 | b2 / 38 / 3103 |
| on | 5 | opening-a1-access | c2 / 38 / 2617 | c2 / 38 / 2617 |
| off | 6 | early-corner-race | f4 / 200 / 2963 | f4 / 200 / 2963 |
| on | 6 | early-corner-race | f3 / 200 / 2875 | f3 / 200 / 2875 |
| off | 6 | midgame-normal-mobility | d8 / 440 / 3093 | d8 / 440 / 3093 |
| on | 6 | midgame-normal-mobility | b3 / 440 / 3723 | b3 / 440 / 3723 |

## Consistency matrix

`Root top` is the top independent child-search candidate. `Match` means
`root_scores_match_result=true`.

| Profile | Depth | Position | Search result best/score | Root top best/score | Result-best root score | Match |
|---|---:|---|---|---|---:|---|
| fixed, TT off, PVS off, aspiration off, lazy off, hint off | 5 | opening-a1-access | b2 / 38 | c2 / 38 | -46 | no |
| iterative strong-like, hint off | 5 | opening-a1-access | b2 / 38 | c2 / 38 | -46 | no |
| iterative strong-like, hint on | 5 | opening-a1-access | c2 / 38 | c2 / 38 | 38 | yes |
| iterative strong-like, hint off | 6 | early-corner-race | f4 / 200 | h8 / 200 | 100 | no |
| iterative strong-like, hint on | 6 | early-corner-race | f3 / 200 | h8 / 200 | 93 | no |
| iterative strong-like, hint off | 6 | midgame-normal-mobility | d8 / 440 | d8 / 440 | 440 | yes |
| iterative strong-like, hint on | 6 | midgame-normal-mobility | b3 / 440 | d8 / 440 | 234 | no |
| iterative strong-like, hint off | 7 | midgame-white-wall | b3 / 129 | f5 / 129 | 115 | no |
| iterative strong-like, hint on | 7 | midgame-white-wall | f5 / 129 | f5 / 129 | 129 | yes |
| iterative strong-like, hint off | 9 | midgame-white-wall | b3 / 83 | f5 / 83 | 7 | no |
| iterative strong-like, hint on | 9 | midgame-white-wall | f5 / 83 | f5 / 83 | 83 | yes |

The fixed-depth opening row shows that the mismatch is not introduced only by PVS, aspiration, TT,
lazy ordering, or shallow hints. The diagnostic table and parent result simply are not the same
artifact.

## Cause

The previous reports mixed two identities:

1. Parent search result: one search over the root with its normal root ordering, windows, session
   state, history, TT policy, PV hints, and iterative state.
2. Root candidate table: each legal root move is applied, then the child is searched independently
   and negated back to root perspective.

Those are useful diagnostics, but they are not interchangeable. The old `root_scores` field name was
not enough to prevent misreading the table as the parent root candidate scores.

## Reading #322 after this fix

The 23 best-move changes should no longer be classified as proven `score-changing behavior
difference` from the old root-candidate table. A better classification is:

- benchmark public score changed: 0 rows observed
- benchmark public best move changed with equal public score: 23 rows observed
- independent root-candidate table did not prove those changes are simple equal-score root ties
- parent result vs independent table mismatch exists in several representative rows, so the table
  cannot decide tie-only vs score-changing behavior by itself

Shallow TT hints should still stay out of default and `strong-v1` from this evidence: public
best moves and result checksums changed. The reason is narrower now: #322 did not establish that the
changes are score-changing candidate differences; it established behavior-visible best/PV changes
whose root-candidate interpretation needs a parent-root candidate trace or a root confirmation
diagnostic.

## Verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/othello_tests "[analyze]"
```

Full test commands are listed in the PR/task verification.
