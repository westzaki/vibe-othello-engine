# 2026-06-01 Pattern Teacher v0

Status: experimental report, not current guidance.

This report records a first minimal pattern-evaluation experiment. It is not a
default-promotion proposal, not a C++ `EvaluationPreset`, not an Elo claim, not
proof of strength, and it does not change exact solver or search semantics.

## Metadata

- base git SHA: `f87dc41fe37a910c6f709295fecbf338d93ff40d`
- report head SHA before metadata refresh: `cd46d8df9487b257faadfbc03a842c811ba7bb83`
- raw output location: `runs/pattern-teacher-v0/`
- reused teacher/exact raw output location: `runs/teacher-aggressive-v3/`
- raw outputs committed: no
- generated teacher labels committed: no
- generated exact labels committed: no
- raw match JSONL committed: no
- local NTest paths committed: no

Compared configs:

| Config | Path | sha256 |
| --- | --- | --- |
| current_default | `data/eval/current_default.eval` | `42d66384d725c55f46261624d78e4723846d63df1d2c1b41200429c4f4fa99cd` |
| classic_othello_v2_teacher_safe | `data/eval/classic_othello_v2_teacher_safe.eval` | `510e9fc66a425a3e16cef959d2405eadf091c4316799d7c13d318dbb8896891a` |
| classic_othello_v3_teacher_aggressive | `data/eval/classic_othello_v3_teacher_aggressive.eval` | `cbaec437d6dfb2efc1b6af7afb977a030bd4c7aab54386d65f4739bf0495b7be` |
| pattern_teacher_v0 | `data/eval/pattern_teacher_v0.eval` | `020fc737bbf1a5e264559e857c2f4995fcc23def8b56d28113dc3b00a5290743` |
| pattern_teacher_v0 table | `data/eval/patterns/pattern_teacher_v0.tsv` | `1c479d0bfea5e46955e0e48a4d2e3fb3b005b1a2e731bab3e43a16e5875bff44` |

## Hypothesis

The v3 scalar candidate improved teacher and exact metrics, but many remaining
teacher misses stayed in `unknown_or_needs_manual_review`. The hypothesis here
is that a small residual pattern table over existing corner and edge pattern
indexes can represent local shapes that scalar feature totals cannot separate.

This experiment deliberately keeps the representation small:

- no default promotion
- no C++ preset
- no external-engine logic in engine core
- no neural or heavyweight ML dependency
- no large learned table

## Pattern Representation

`pattern_teacher_v0` adds an optional `.eval` key:

```text
pattern_table=patterns/pattern_teacher_v0.tsv
opening.pattern_table=10
midgame.pattern_table=10
late.pattern_table=10
```

The table is loaded by the tooling `.eval` loader into `EvaluationConfig`; the
engine core only sees deterministic in-memory arrays. If no table is configured
or the phase weight is zero, current/default behavior is unchanged.

Table shape:

| Family | Full Index Space | Nonzero Entries | Notes |
| --- | ---: | ---: | --- |
| `corner_2x3` | 729 | 64 | Existing side-relative corner 2x3 index |
| `edge_8` | 6561 | 64 | Existing side-relative full-edge index |

The committed table is sparse, text-readable TSV. Entries were generated from
validated teacher residuals against `classic_othello_v3_teacher_aggressive`.
For every teacher row where v3 missed and the teacher move was legal, the
trainer compared the teacher child board against the v3-selected child board.
It added preference counts for teacher child pattern indexes and subtracted
counts for selected child indexes. Counts were converted into small integer
weights with antisymmetric color-swap pairs, `min_abs_diff=3`, `scale=3`, and
`max_abs_weight=4`.

Training command:

```sh
python3 tools/scripts/pattern_teacher_v0_train.py \
  --teacher-labels runs/teacher-aggressive-v3/teacher/ntest-depth26-random1000/labels.jsonl,runs/teacher-aggressive-v3/teacher/ntest-depth26-diagnostic/labels.jsonl \
  --exact-labels runs/teacher-aggressive-v3/exact/teacher_random1000_max12_labels.jsonl,runs/teacher-aggressive-v3/exact/diagnostic_max12_labels.jsonl \
  --eval-config data/eval/classic_othello_v3_teacher_aggressive.eval \
  --analyze-position build/othello_analyze_position \
  --out runs/pattern-teacher-v0/training/pattern_teacher_v0.tsv \
  --corner-pairs 32 \
  --edge-pairs 32 \
  --min-abs-diff 3 \
  --scale 3 \
  --max-abs-weight 4
```

Training summary:

| Teacher Rows | Already Agreed | Residual Updates | Corner Entries | Edge Entries |
| ---: | ---: | ---: | ---: | ---: |
| 1006 | 464 | 542 | 64 | 64 |

## Teacher Data

The experiment reused the validated NTest depth-26 teacher data from the v3 run.
NTest raw coordinates were normalized through the workflow's `engine_move` to
`move` conversion and then validated with the C++ rule core.

Teacher workflow commands, with local paths intentionally parameterized:

```sh
./build/othello_position_sampler \
  --output runs/teacher-aggressive-v3/teacher/random1000_positions.txt \
  --count 1000 \
  --target-empties 8,10,12,16,20,24,30,36,42,50 \
  --seed 2026053106

python3 tools/scripts/external_teacher_label_workflow.py \
  --positions runs/teacher-aggressive-v3/teacher/random1000_positions.txt \
  --out runs/teacher-aggressive-v3/teacher/ntest-depth26-random1000 \
  --adapter ntest \
  --protocol nboard \
  --depth 26 \
  --timeout-ms 60000 \
  --workdir "${NTEST_BUILD}" \
  --engine-name ntest26-local \
  --legal-validator build/othello_validate_move \
  --allow-failures \
  --engine-cmd -- "${NTEST_BIN}" x
```

Diagnostic labels used the same workflow over
`data/positions/evaluation/diagnostic_suite.txt`.

Teacher acceptance filter:

- `status == "ok"`
- `legal_move_valid == true`
- `move_token_valid == true`
- `move` is not null

Teacher counts:

| Source | Requested | OK | Failed | Usable | Legal Valid | Illegal Teacher Moves | Validator Failures | Invalid Tokens | Avg elapsed ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| random1000 | 1000 | 999 | 1 | 999 | 999 | 0 | 0 | 1 | 1927.010 |
| diagnostic_suite | 8 | 7 | 1 | 7 | 7 | 0 | 0 | 1 | 1938.312 |
| total | 1008 | 1006 | 2 | 1006 | 1006 | 0 | 0 | 2 | 1928.942 |

Accepted teacher empties distribution:

| Source | Distribution |
| --- | --- |
| random1000 | 8:100, 10:100, 12:100, 16:99, 20:100, 24:100, 30:100, 36:100, 42:100, 50:100 |
| diagnostic_suite | 10:1, 21:1, 23:1, 25:1, 36:1, 53:1, 55:1 |

## Exact Data

Exact labels were reused from the v3 run.

```sh
./build/othello_exact_label_dump \
  --input runs/teacher-aggressive-v3/exact/train_positions.txt \
  --output runs/teacher-aggressive-v3/exact/train_labels.jsonl \
  --max-empties 12 \
  --include-move-scores

./build/othello_exact_label_dump \
  --input runs/teacher-aggressive-v3/exact/heldout_positions.txt \
  --output runs/teacher-aggressive-v3/exact/heldout_labels.jsonl \
  --max-empties 12 \
  --include-move-scores

./build/othello_exact_label_dump \
  --input runs/teacher-aggressive-v3/teacher/random1000_positions.txt \
  --output runs/teacher-aggressive-v3/exact/teacher_random1000_max12_labels.jsonl \
  --max-empties 12 \
  --include-move-scores
```

Exact counts:

| Dataset | Input Positions | Labeled | Skipped Too Many Empties |
| --- | ---: | ---: | ---: |
| train | 96 | 96 | 0 |
| held-out | 96 | 96 | 0 |
| teacher random1000 overlap | 1000 | 300 | 700 |
| diagnostic overlap | 8 | 1 | 7 |

Teacher-vs-exact overlap was 301 positions. The validated NTest teacher move
was in the exact-best group for all 301.

## Teacher Metrics

Teacher agreement used 1006 validated teacher rows.

| Config | Teacher Agreement | Teacher Move Rank Sum | Exact Overlap Agreement | Exact Overlap Rank Sum |
| --- | ---: | ---: | ---: | ---: |
| current_default | 417 / 1006 | 2520 | 155 / 301 | 587 |
| classic_othello_v2_teacher_safe | 429 / 1006 | 2492 | 157 / 301 | 587 |
| classic_othello_v3_teacher_aggressive | 464 / 1006 | 2303 | 182 / 301 | 536 |
| pattern_teacher_v0 | 478 / 1006 | 2301 | 189 / 301 | 518 |
| NTest teacher | n/a | n/a | 301 / 301 | n/a |

Top buckets:

| Config | Unknown/manual | Late Disc-Count Greed | X-Square Trap | Corner Access | Edge Greed |
| --- | ---: | ---: | ---: | ---: | ---: |
| current_default | 364 | 222 | 2 | 0 | 1 |
| classic_othello_v2_teacher_safe | 356 | 218 | 2 | 0 | 1 |
| classic_othello_v3_teacher_aggressive | 356 | 183 | 2 | 1 | 0 |
| pattern_teacher_v0 | 348 | 176 | 2 | 2 | 0 |

Compared with v3, pattern v0 recovered 48 teacher misses and introduced 34 new
teacher regressions. Unknown/manual misses dropped by 8, which is weak but real
evidence that pattern vocabulary is addressing some scalar-blind positions.

## Exact Metrics

Train exact labels:

| Config | Sign Agreements | Wrong Direction | High-Confidence Wrong | Exact-Best Top Group | Exact-Best Rank Sum | Exact Score Gap Sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| current_default | 71 / 96 | 22 | 6 | 51 / 96 | 197 | 436 |
| classic_othello_v2_teacher_safe | 72 / 96 | 21 | 6 | 49 / 96 | 197 | 470 |
| classic_othello_v3_teacher_aggressive | 78 / 96 | 15 | 0 | 60 / 96 | 169 | 272 |
| pattern_teacher_v0 | 79 / 96 | 14 | 0 | 61 / 96 | 171 | 266 |

Held-out exact labels:

| Config | Sign Agreements | Wrong Direction | High-Confidence Wrong | Exact-Best Top Group | Exact-Best Rank Sum | Exact Score Gap Sum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| current_default | 77 / 96 | 15 | 1 | 45 / 96 | 196 | 451 |
| classic_othello_v2_teacher_safe | 77 / 96 | 15 | 1 | 46 / 96 | 195 | 423 |
| classic_othello_v3_teacher_aggressive | 81 / 96 | 11 | 3 | 52 / 96 | 176 | 322 |
| pattern_teacher_v0 | 81 / 96 | 11 | 1 | 54 / 96 | 173 | 286 |

Held-out validation objective:

| Config | Objective | Delta vs Base |
| --- | ---: | ---: |
| current_default | 46 | 0 |
| classic_othello_v2_teacher_safe | 46 | 0 |
| classic_othello_v3_teacher_aggressive | 56 | +10 |
| pattern_teacher_v0 | 58 | +12 |

Pattern v0 did not improve held-out sign agreement over v3, but it reduced
high-confidence wrong moves, improved exact-best top-group hits, rank sum, and
exact score gap.

## Search Validation

Command shape:

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build \
  --labels runs/teacher-aggressive-v3/exact/heldout_labels.jsonl \
  --baseline-config data/eval/current_default.eval \
  --candidates data/eval/classic_othello_v2_teacher_safe.eval data/eval/classic_othello_v3_teacher_aggressive.eval data/eval/pattern_teacher_v0.eval \
  --out runs/pattern-teacher-v0/matrix/heldout \
  --search-depths 5,6,7 \
  --positions evaluation \
  --repetitions 3 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis
```

Search used iterative mode, TT on, PVS on, aspiration on, and exact-endgame
threshold 0.

| Config | Nodes | Elapsed ms | Exact Root Searches | Best Move | Score | PV |
| --- | ---: | ---: | ---: | --- | ---: | --- |
| current_default | 613647 | 154.503 | 0 | d6 | 69 | d6 a5 b6 b5 a4 |
| classic_othello_v2_teacher_safe | 553383 | 157.756 | 0 | b2 | 101 | b2 a1 c2 a4 c5 |
| classic_othello_v3_teacher_aggressive | 633834 | 156.617 | 0 | c2 | 135 | c2 d3 f6 b3 a4 |
| pattern_teacher_v0 | 565020 | 157.128 | 0 | f7 | 154 | f7 e7 c2 g8 f5 |

Search result checksums changed as expected for evaluator changes. This is not
itself a regression.

## Match Smoke

Exact was disabled for all match smoke runs. Games were side-swapped and used
`data/openings/eval_regression_openings.txt`. These are not Elo estimates and
not proof of strength.

100-game smoke:

| Depth | Player A | Player B | Games | A Wins | B Wins | Draws | Avg Diff A | Errors | Exact Roots A/B |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 5 | pattern_teacher_v0 | current_default | 100 | 45 | 54 | 1 | -1.92 | 0 | 0 / 0 |
| 5 | pattern_teacher_v0 | v2_teacher_safe | 100 | 47 | 49 | 4 | -2.20 | 0 | 0 / 0 |
| 5 | pattern_teacher_v0 | v3_teacher_aggressive | 100 | 47 | 51 | 2 | -0.94 | 0 | 0 / 0 |
| 6 | pattern_teacher_v0 | current_default | 100 | 54 | 42 | 4 | -1.16 | 0 | 0 / 0 |
| 6 | pattern_teacher_v0 | v2_teacher_safe | 100 | 46 | 52 | 2 | -0.84 | 0 | 0 / 0 |
| 6 | pattern_teacher_v0 | v3_teacher_aggressive | 100 | 72 | 26 | 2 | +5.98 | 0 | 0 / 0 |

The smoke signal is mixed. Depth 6 versus v3 is strongly positive, but depth 5
is slightly negative and v2 remains competitive. Treat this as a reason to keep
the candidate experimental, not as a strength claim.

## Representatives

Pattern recoveries and regressions versus v3:

| Kind | Position | Teacher | Pattern | v3 | Exact Best | Teacher Rank | Board |
| --- | --- | --- | --- | --- | --- | ---: | --- |
| help | teacher-30 | h8 | h8 | g1 | h8 | 1 | ..WWWWW.<br>W.WWBBBW<br>BWWWWBBB<br>B.WBWWBB<br>BBBWWWBB<br>..BBWWBB<br>WWWWBWWB<br>BWWWWW.B<br>side=B |
| help | teacher-31 | f2 | f2 | a7 | f2 | 1 | BBBBBBBW<br>.BBBBBBB<br>.WWWWWBB<br>.WWWWWBB<br>BWWBBBB.<br>.WWWWBBB<br>WWWWW.BB<br>..WWW..B<br>side=B |
| help | teacher-74 | f1 | f1 | a8 | - | 1 | ....W...<br>BWWW.WW.<br>BWW.BBWB<br>BWWBWWWW<br>BWBWWWWB<br>BBWWWWW.<br>B.BW....<br>.BBBW...<br>side=B |
| help | teacher-81 | d1 | d1 | g8 | d1 | 1 | WWWWB..B<br>.WWWWWB.<br>BBWWWW..<br>BBWWWWW.<br>BWBBBWBB<br>BBWWWBBB<br>BB.WBBBW<br>.BW.WBBB<br>side=B |
| regress | teacher-1 | g8 | h1 | g8 | h1 d8 g8 | 2 | WW..WB.B<br>W.WWWWWB<br>WWWBWBWB<br>WWBWBWBB<br>WBWBWWBB<br>W.BWWWB.<br>.BBB..WB<br>BWWWWWW.<br>side=B |
| regress | teacher-58 | e2 | f2 | e2 | - | 2 | ........<br>.....WB.<br>B.WWWW..<br>B..WWW..<br>BWBBWW..<br>..BWW...<br>.B.W....<br>........<br>side=B |
| regress | teacher-67 | c5 | f1 | c5 | - | 2 | ........<br>..WB.B..<br>..BWBB..<br>WB.WWB..<br>.W.BBW..<br>.WWWBBW.<br>..WBBB..<br>...BW...<br>side=B |
| regress | teacher-68 | c6 | c7 | c6 | - | 2 | ........<br>...W.B..<br>.B.WWB..<br>..BBWB..<br>.WBWWB..<br>WBW.WB..<br>.W......<br>W.......<br>side=B |

## Evidence

Positive:

- Pattern v0 improves teacher agreement over v3 by +14 rows and exact overlap
  by +7 rows.
- Teacher move rank sum slightly improves over v3: 2301 vs 2303.
- Unknown/manual teacher misses fall from 356 to 348.
- Held-out exact top group improves from 52 to 54 and exact score gap improves
  from 322 to 286.
- The implementation is config-gated and current/default parsing remains
  unchanged when no table is configured.

Negative:

- Pattern v0 introduces 34 teacher regressions versus v3.
- Teacher rank improvement is tiny despite top-1 gains.
- Match smoke is mixed; depth 5 is slightly negative against all compared
  baselines, and depth 6 is not uniformly positive.
- The table is trained from the same teacher sample used for teacher agreement,
  so teacher top-1 improvement is at risk of overfitting.
- Many mistakes remain `unknown_or_needs_manual_review`, even after the drop.

## Risks

- Sparse table values may memorize local teacher residuals rather than learn
  durable Othello shape.
- The first table uses only corner and edge indexes; central and mobility-like
  pattern misses remain unrepresented.
- Pattern weights are a single scalar per phase, not a learned phase-dependent
  table.
- Match smoke can be noisy and must not be interpreted as Elo.

## Decision

Keep `pattern_teacher_v0` experimental for deeper validation and continue the
pattern architecture. Do not promote default and do not add a C++ preset.

This v0 is useful because it proves a small pattern table can reduce
unknown/manual teacher misses and improve exact held-out move-rank metrics
without changing exact or search semantics. It is not yet good enough to treat
as a stronger evaluator.
