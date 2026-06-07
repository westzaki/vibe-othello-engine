# Shallow TT Hint Behavior Diff

Date: 2026-06-07

Status: diagnostic report only. No default, `strong-v1`, preset, evaluator, pattern table, training
script, public API, or behavior-fixing search change was made.

## Question

PR #322 showed that shallow TT best-move hints are fast but change best moves, PVs, result
checksums, and work checksums on several suite rows. This report classifies those changes so a later
PR can decide between:

- leaving the option as a diagnostic/experimental setting,
- promoting it as a behavior-changing `strong-v2`-style preset, or
- first implementing order-independent root/tie behavior.

## Build and Inputs

- Latest main read before measuring: `807ede6` (`perf: accelerate pairwise pattern training (#320)`)
- Worktree also contained the measurement-only #322 report so this report could reference it.
- Engine code measured from `origin/main`: `807ede68ff2132879fa869f3ac2cd5e2eea642bf`
- Build type: Release
- Raw output: `runs/perf/shallow-tt-hint-behavior-2026-06-07/*.jsonl` (gitignored)

```sh
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

Result: 358/358 tests passed.

## Commands

Plain lazy hint off/on:

```sh
./build/othello_search_bench --mode iterative --depths 5,6,7,8,9 --positions suite --repetitions 3 --tt on --pvs on --aspiration on --lazy-first-move-ordering on --shallow-tt-move-ordering-hint off --exact-endgame-threshold 0 --by-position --format jsonl
./build/othello_search_bench --mode iterative --depths 5,6,7,8,9 --positions suite --repetitions 3 --tt on --pvs on --aspiration on --lazy-first-move-ordering on --shallow-tt-move-ordering-hint on --exact-endgame-threshold 0 --by-position --format jsonl
```

Strong-v1-equivalent midgame hint off/on:

```sh
./build/othello_search_bench --mode iterative --depths 5,6,7,8,9 --positions suite --repetitions 3 --tt on --pvs on --aspiration on --aspiration-profile score-delta-aware --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --shallow-tt-move-ordering-hint off --exact-endgame-threshold 0 --by-position --format jsonl
./build/othello_search_bench --mode iterative --depths 5,6,7,8,9 --positions suite --repetitions 3 --tt on --pvs on --aspiration on --aspiration-profile score-delta-aware --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --shallow-tt-move-ordering-hint on --exact-endgame-threshold 0 --by-position --format jsonl
```

Root-candidate probe for changed-best rows:

```sh
./build/othello_analyze_position --stdin --batch-jsonl --root-candidates --mode iterative --depth N --tt on --pvs on --aspiration on --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --shallow-tt-move-ordering-hint off --exact-endgame-threshold 0
./build/othello_analyze_position --stdin --batch-jsonl --root-candidates --mode iterative --depth N --tt on --pvs on --aspiration on --tt-min-probe-depth 1 --tt-min-store-depth 1 --lazy-first-move-ordering on --shallow-tt-move-ordering-hint on --exact-endgame-threshold 0
```

`othello_analyze_position` does not currently expose `--aspiration-profile`, so the root-candidate
probe used its fixed aspiration profile. Treat that probe as diagnostic evidence about root move
scores, not as a replacement for the benchmark rows.

## Speed Summary

| profile | depth | hint | elapsed_ms | nodes | nps | shallow used | lazy cuts | scored saved |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| lazy | 5 | off | 122.764 | 115,584 | 941,517 | 0 | 855 | 6,180 |
| lazy | 5 | on | 140.529 | 114,717 | 816,325 | 7,650 | 6,276 | 39,294 |
| lazy | 6 | off | 153.709 | 301,095 | 1,958,869 | 0 | 1,953 | 13,287 |
| lazy | 6 | on | 166.781 | 285,954 | 1,714,548 | 19,941 | 14,439 | 92,406 |
| lazy | 7 | off | 299.894 | 982,443 | 3,275,972 | 0 | 4,851 | 36,219 |
| lazy | 7 | on | 303.904 | 938,532 | 3,088,250 | 68,019 | 55,437 | 364,854 |
| lazy | 8 | off | 772.023 | 2,547,831 | 3,300,203 | 0 | 11,310 | 83,544 |
| lazy | 8 | on | 739.383 | 2,439,276 | 3,299,071 | 164,976 | 118,548 | 801,627 |
| lazy | 9 | off | 2,109.899 | 8,067,810 | 3,823,790 | 0 | 31,314 | 245,343 |
| lazy | 9 | on | 2,042.962 | 7,603,752 | 3,721,925 | 540,294 | 437,979 | 3,029,934 |
| strong-v1-like | 5 | off | 148.985 | 113,436 | 761,394 | 0 | 624 | 4,578 |
| strong-v1-like | 5 | on | 144.098 | 113,196 | 785,548 | 7,680 | 6,033 | 37,755 |
| strong-v1-like | 6 | off | 183.460 | 297,996 | 1,624,308 | 0 | 1,650 | 11,346 |
| strong-v1-like | 6 | on | 176.255 | 283,491 | 1,608,415 | 19,938 | 14,055 | 90,219 |
| strong-v1-like | 7 | off | 329.668 | 977,067 | 2,963,794 | 0 | 4,179 | 31,209 |
| strong-v1-like | 7 | on | 317.481 | 933,126 | 2,939,154 | 67,494 | 53,970 | 354,765 |
| strong-v1-like | 8 | off | 807.859 | 2,540,400 | 3,144,608 | 0 | 10,578 | 78,054 |
| strong-v1-like | 8 | on | 758.199 | 2,421,288 | 3,193,474 | 163,545 | 116,337 | 786,705 |
| strong-v1-like | 9 | off | 2,141.277 | 8,008,077 | 3,739,860 | 0 | 28,173 | 218,295 |
| strong-v1-like | 9 | on | 1,810.039 | 7,426,509 | 4,102,954 | 533,730 | 425,517 | 2,932,644 |

On this run, strong-v1-like hint-on was faster at every measured depth, while plain lazy hint-on was
slower at depths 5-7 and faster at depths 8-9. This is performance evidence only; correctness and
behavior are classified below.

## Change Classification

The plain lazy comparison and the strong-v1-like comparison produced the same classification counts.
There were no score changes in either comparison.

| depth | changed rows | score changed | same score, best changed | same score, PV-only | same score, result-only | work-only |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 | 23 | 0 | 1 | 1 | 0 | 21 |
| 6 | 24 | 0 | 5 | 3 | 0 | 16 |
| 7 | 24 | 0 | 7 | 4 | 0 | 13 |
| 8 | 24 | 0 | 4 | 12 | 0 | 8 |
| 9 | 24 | 0 | 6 | 9 | 0 | 9 |
| total | 119 | 0 | 23 | 29 | 0 | 67 |

Interpretation:

- No measured row changed score.
- Every result checksum change was explained by a best-move change. There were no result-only rows.
- PV-only rows kept score, best move, and result checksum stable, so they are display/PV-line
  behavior changes rather than public best-move/score changes.
- Work-only rows changed only the work checksum, as expected when ordering and cutoffs change.

## Public Behavior Changes

Strong-v1-like public changes are listed below. The plain lazy comparison changed the same rows.

| depth | position | kind | off best | on best | score | result checksum | work checksum |
| ---: | --- | --- | --- | --- | ---: | --- | --- |
| 5 | `midgame-x-risk` | PV-only | c2 | c2 | 153 | same | changed |
| 5 | `opening-a1-access` | best+PV | b2 | c2 | 38 | changed | changed |
| 6 | `early-corner-race` | best+PV | f4 | f3 | 200 | changed | changed |
| 6 | `early-tight-mobility` | PV-only | f3 | f3 | -75 | same | changed |
| 6 | `midgame-lopsided-edge` | PV-only | g6 | g6 | -292 | same | changed |
| 6 | `midgame-normal-mobility` | best+PV | d8 | b3 | 440 | changed | changed |
| 6 | `midgame-wide-x-risk` | best+PV | h7 | b3 | 321 | changed | changed |
| 6 | `opening-central-run` | PV-only | c6 | c6 | 63 | same | changed |
| 6 | `opening-quiet-development` | best+PV | c4 | e6 | 133 | changed | changed |
| 6 | `opening-wide-mobility` | best+PV | e3 | d3 | 184 | changed | changed |
| 7 | `early-balanced-mobility` | best+PV | a5 | c3 | -94 | changed | changed |
| 7 | `early-corner-race` | best+PV | h8 | e2 | 113 | changed | changed |
| 7 | `early-tight-mobility` | PV-only | f3 | f3 | -116 | same | changed |
| 7 | `late-edge-heavy` | PV-only | h1 | h1 | -101 | same | changed |
| 7 | `midgame-lopsided-edge` | PV-only | g4 | g4 | -324 | same | changed |
| 7 | `midgame-low-mobility` | PV-only | f1 | f1 | -116 | same | changed |
| 7 | `midgame-white-wall` | best+PV | b3 | f5 | 129 | changed | changed |
| 7 | `midgame-wide-x-risk` | best+PV | b2 | h7 | 225 | changed | changed |
| 7 | `midgame-x-risk` | best+PV | c2 | b4 | 156 | changed | changed |
| 7 | `opening-a1-access` | best+PV | b2 | c2 | 29 | changed | changed |
| 7 | `opening-quiet-development` | best+PV | e6 | d3 | 33 | changed | changed |
| 8 | `early-balanced-mobility` | PV-only | b3 | b3 | -10 | same | changed |
| 8 | `early-corner-race` | best+PV | e2 | c3 | 192 | changed | changed |
| 8 | `early-tight-mobility` | PV-only | f3 | f3 | -89 | same | changed |
| 8 | `late-black-pass` | PV-only | None | None | -882 | same | changed |
| 8 | `late-corner-swing` | PV-only | g4 | g4 | 77 | same | changed |
| 8 | `midgame-balanced-count` | PV-only | e2 | e2 | 60 | same | changed |
| 8 | `midgame-lopsided-edge` | PV-only | e2 | e2 | -298 | same | changed |
| 8 | `midgame-low-mobility` | PV-only | f1 | f1 | -84 | same | changed |
| 8 | `midgame-normal-mobility` | PV-only | b3 | b3 | 464 | same | changed |
| 8 | `midgame-white-wall` | best+PV | f5 | b3 | 165 | changed | changed |
| 8 | `midgame-wide-x-risk` | PV-only | c2 | c2 | 332 | same | changed |
| 8 | `midgame-x-risk` | best+PV | b4 | b5 | 218 | changed | changed |
| 8 | `opening-a1-access` | PV-only | d6 | d6 | 91 | same | changed |
| 8 | `opening-central-run` | PV-only | c6 | c6 | 65 | same | changed |
| 8 | `opening-quiet-development` | best+PV | c4 | e6 | 151 | changed | changed |
| 8 | `opening-wide-mobility` | PV-only | a1 | a1 | 171 | same | changed |
| 9 | `early-balanced-mobility` | best+PV | g3 | c3 | -74 | changed | changed |
| 9 | `early-corner-race` | best+PV | c3 | d3 | 156 | changed | changed |
| 9 | `early-tight-mobility` | PV-only | f3 | f3 | -145 | same | changed |
| 9 | `late-corner-swing` | PV-only | g4 | g4 | 27 | same | changed |
| 9 | `midgame-balanced-count` | PV-only | h2 | h2 | 40 | same | changed |
| 9 | `midgame-lopsided-edge` | PV-only | g4 | g4 | -338 | same | changed |
| 9 | `midgame-low-mobility` | PV-only | f1 | f1 | -114 | same | changed |
| 9 | `midgame-normal-mobility` | best+PV | c7 | d8 | 475 | changed | changed |
| 9 | `midgame-white-wall` | best+PV | b3 | f5 | 83 | changed | changed |
| 9 | `midgame-wide-x-risk` | PV-only | b3 | b3 | 243 | same | changed |
| 9 | `midgame-x-risk` | PV-only | c2 | c2 | 166 | same | changed |
| 9 | `opening-a1-access` | best+PV | d2 | b2 | 39 | changed | changed |
| 9 | `opening-central-run` | PV-only | c6 | c6 | -18 | same | changed |
| 9 | `opening-quiet-development` | best+PV | e6 | d3 | 41 | changed | changed |
| 9 | `opening-wide-mobility` | PV-only | f4 | f4 | 156 | same | changed |

## Root Score Probe

For rows where the benchmark best move changed, the diagnostic root-candidate probe searched each
legal root move independently and compared the old and new best moves. It did not find old/new ties.

Important caveat: this probe is diagnostic. The analyzer does not expose score-delta-aware
aspiration, and root-candidate analysis does not share exactly the same root search context as a
single benchmark run. Still, it is useful for testing the hypothesis that changed best moves are
mostly simple equal-score tie-break differences. The probe does not support that hypothesis.

| depth | position | off best | on best | benchmark score | probe old/new with hint off | probe old/new with hint on | old/new tie? |
| ---: | --- | --- | --- | ---: | --- | --- | --- |
| 5 | `opening-a1-access` | b2 | c2 | 38 | -46/38 | -46/38 | no |
| 6 | `early-corner-race` | f4 | f3 | 200 | 100/93 | 100/93 | no |
| 6 | `midgame-normal-mobility` | d8 | b3 | 440 | 440/234 | 440/234 | no |
| 6 | `midgame-wide-x-risk` | h7 | b3 | 321 | 321/260 | 321/260 | no |
| 6 | `opening-quiet-development` | c4 | e6 | 133 | 93/133 | 93/133 | no |
| 6 | `opening-wide-mobility` | e3 | d3 | 184 | 129/109 | 129/109 | no |
| 7 | `early-balanced-mobility` | a5 | c3 | -94 | -94/-244 | -94/-244 | no |
| 7 | `early-corner-race` | h8 | e2 | 113 | 113/-31 | 113/-31 | no |
| 7 | `midgame-white-wall` | b3 | f5 | 129 | 115/129 | 115/129 | no |
| 7 | `midgame-wide-x-risk` | b2 | h7 | 225 | 61/225 | 61/225 | no |
| 7 | `midgame-x-risk` | c2 | b4 | 156 | 148/142 | 148/142 | no |
| 7 | `opening-a1-access` | b2 | c2 | 29 | -49/22 | -49/22 | no |
| 7 | `opening-quiet-development` | e6 | d3 | 33 | 33/2 | 33/2 | no |
| 8 | `early-corner-race` | e2 | c3 | 192 | 31/93 | 31/93 | no |
| 8 | `midgame-white-wall` | f5 | b3 | 165 | 165/123 | 165/123 | no |
| 8 | `midgame-x-risk` | b4 | b5 | 218 | 182/103 | 182/103 | no |
| 8 | `opening-quiet-development` | c4 | e6 | 151 | 91/151 | 91/151 | no |
| 9 | `early-balanced-mobility` | g3 | c3 | -74 | -283/-245 | -283/-245 | no |
| 9 | `early-corner-race` | c3 | d3 | 156 | 31/-27 | 31/-27 | no |
| 9 | `midgame-normal-mobility` | c7 | d8 | 475 | 425/475 | 425/475 | no |
| 9 | `midgame-white-wall` | b3 | f5 | 83 | 7/83 | 7/83 | no |
| 9 | `opening-a1-access` | d2 | b2 | 39 | 39/-38 | 39/-38 | no |
| 9 | `opening-quiet-development` | e6 | d3 | 41 | 41/10 | 41/10 | no |

The root-score probe also showed cases where a benchmark best move did not match the best
independently probed root score. For example, depth-5 `opening-a1-access` reported `b2` with score
38 when hint was off, while independent root scores were `c2=38` and `b2=-46`. This points to root
search context, alpha-beta windows, PVS/bound handling, or iterative TT/PV interaction rather than a
simple legal tie-break-order explanation.

## Shallow Hint Usage Visibility

Current stats can say how many shallow hints were probed, hit, and used, but not the exact ply, node
hash, or board where each hint became the first searched move.

From `src/search_node.cpp`:

- root nodes never collect TT best-move hints directly because `collect_tt_best_move_hint` requires
  `!is_root`;
- shallow hints are available only when a matching TT entry exists but `entry.depth < requested
  depth`;
- a shallow hint is counted as `shallow_tt_move_ordering_used` only when it becomes the lazy
  preferred move or is promoted into the full move ordering list;
- `ordering_lazy_cut_before_full_sort` and `ordering_scored_moves_saved` show when trying the
  preferred move avoided building/scoring the remaining move list.

Aggregate strong-v1-like shallow hint usage:

| depth | shallow used | lazy cuts | scored moves saved |
| ---: | ---: | ---: | ---: |
| 5 | 7,680 | 6,033 | 37,755 |
| 6 | 19,938 | 14,055 | 90,219 |
| 7 | 67,494 | 53,970 | 354,765 |
| 8 | 163,545 | 116,337 | 786,705 |
| 9 | 533,730 | 425,517 | 2,932,644 |

To answer exact ply/node questions, a future diagnostic-only trace should record at least
`ply`, remaining `depth`, `move`, shallow-entry depth, root position id, and whether the hinted move
caused a cutoff. That should be benchmark/debug-only and should not enter the public API.

## Classification

This run does not support "mostly equal-score root tie-break changes" as the primary explanation.

- Score changes: none in the measured benchmark rows.
- Best-move changes: all kept the same benchmark score, but the root-candidate probe did not find
  old/new best ties.
- PV-only changes: likely display/search-line differences caused by changed ordering and alpha-beta
  cutoffs below the root. They keep public best move and score stable.
- Work-only changes: expected pure search-work differences.
- Best/result checksum changes: behavior changes, not pure speed.

The most likely current classification is:

- `work-only`: pure search-order/performance changes.
- `PV-only`: search-order/cutoff changes that alter the displayed line but not best move or score.
- `best+PV`: root search order/context changes; not proven to be legal tie-break-only.
- `score changed`: not observed. If score changes appear later, shallow hint should stay disabled
  outside diagnostic experiments until the condition is understood.

## Promotion Decision

Default: do not promote.

The default should not absorb an option that changes public result checksums and best moves. This is
true even though no score changes were observed.

Strong-v1: do not mutate in place.

`strong-v1` is already an opt-in practical preset, but changing it would still change public
best-move behavior for existing callers. The root-score probe does not justify treating those changes
as harmless tie-only reorderings.

Strong-v2 / experimental preset: possible later, but not from this evidence alone.

The strong-v1-like timing rows are attractive at deeper depths, especially depth 9, but the behavior
classification says a separate experimental preset should come only with explicit documentation that
best moves and PVs can change. Before that, compare match/self-play or exact-label move quality on
changed rows.

Order-independent tie behavior: investigate before promotion.

A later PR should first add regression diagnostics that compare root search results with independently
searched root candidate scores on the changed positions. If the engine wants stable
order-independent public results, root candidates that can tie or beat the current best likely need a
full-window confirmation and lower-index tie handling at the root, similar in spirit to the exact
endgame root handling. That may reduce or erase part of the shallow-hint speedup because extra root
candidate re-searches are precisely the work that ordering and cutoffs currently avoid.

## Fix Proposal

No fix is included in this PR.

The clearest follow-up is a correctness-focused root-search PR, separate from performance promotion:

1. Add focused tests or a diagnostic fixture for positions where `search` best move disagrees with
   independent root-candidate scores.
2. Decide the public contract: either root `best_move` is the best exact depth-limited candidate with
   lower-index tie break, or PVS/alpha-beta bound-derived move choices are accepted as search
   behavior.
3. If the exact candidate contract wins, implement root-level full-window confirmation for candidates
   that may tie or beat the current best, then re-measure shallow hint speed with that behavior fixed.

Until that is resolved, shallow TT hint should stay out of default and `strong-v1`.
