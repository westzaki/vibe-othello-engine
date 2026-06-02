# NTest Balanced 300K Regularized Pairwise Full V2 Report

This report records the first full regularized pairwise pattern-table candidate
trained from `ntest-balanced300k-v0`, plus the validation and deterministic match
evidence collected before any default-promotion decision.

This is not a playing-strength claim, and it is not evidence for promoting
evaluator defaults. Generated training artifacts, match JSONL, and raw local
logs remain under `runs/` and must not be committed.

## Candidate Reference

| field | value |
| --- | --- |
| source dataset | `ntest-balanced300k-v0` |
| teacher | `ntest12-local` |
| exact overlap | `ntest-balanced300k-v0-exact-overlap-v0` |
| git sha | `9ae3553e9454e69e5fdb6b4197edddae20dd4d11` |
| base eval | `data/eval/current_default.eval` |
| candidate artifact | `runs/pattern-training/ntest-balanced300k-v0/regularized_pairwise_full_v2/candidate.eval` |
| model shape | `current_default + learned pattern-table delta` |
| learned tables | `opening.tsv`, `midgame.tsv`, `late.tsv` |
| broader match validation git sha | `887d61bc6d082cf2d516573eee14fbbe90e2e841` |

The candidate eval keeps the current default base terms and adds
phase-specific learned pattern tables with `pattern_table` phase weights set to
`1`.

## Training Setup

| field | value |
| --- | --- |
| script | `tools/scripts/regularized_pairwise_pattern_train.py` |
| train split | `train` |
| families | `broad_all` |
| pair mode | `teacher-vs-ranked-above` |
| pair weighting | `score-margin` |
| loss | `logistic` |
| L2 | `1e-3` |
| epochs | `5` |
| learning rate | `0.05` |
| max pairs per position | `8` |
| analysis cache | `read-write` |
| analysis jobs | `8` |

Training rows and pair counts:

| field | value |
| --- | ---: |
| train rows | 209390 |
| validation rows | 45331 |
| holdout rows | 45192 |
| already agreed with teacher | 84356 |
| paired positions | 123506 |
| preference pairs | 336786 |
| opening pairs | 17271 |
| midgame pairs | 200013 |
| late pairs | 119502 |
| analysis cache writes | 209390 |
| analysis elapsed seconds | 906.511 |

Pair objective diagnostics:

| metric | before | after |
| --- | ---: | ---: |
| pair accuracy | 0.000000 | 0.105551 |
| weighted pair loss | 38.571647 | 41.253470 |
| average margin | -32.478624 | -26.536626 |

The pair loss increased while margins and downstream agreement improved. Treat
the loss as diagnostic only for this run; held-out teacher agreement, exact
overlap, and match evidence are the gating signals.

## Overfit Gates

| gate | current_default | candidate | result |
| --- | ---: | ---: | --- |
| 1K train subset | 384 / 1000 | 523 / 1000 | pass |
| 5K train subset | 1993 / 5000 | 2475 / 5000 | pass |

The 1K gate exceeded the target of `> 0.50` teacher agreement, and both subset
gates beat `current_default` on the same rows.

## Full Teacher Agreement

Full-split validation used all usable rows from the completed teacher dataset.
Failed teacher rows were excluded.

| split | config | rows | agreements | rate | avg teacher rank | rank sum | rank1 or 2 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| train | `current_default` | 209390 | 84356 | 0.4029 | 2.6377 | 552300 | 129647 |
| train | candidate | 209390 | 91477 | 0.4369 | 2.4219 | 507132 | 138260 |
| validation | `current_default` | 45331 | 18321 | 0.4042 | 2.6479 | 120033 | 28053 |
| validation | candidate | 45331 | 19676 | 0.4341 | 2.4653 | 111754 | 29618 |
| holdout | `current_default` | 45192 | 18243 | 0.4037 | 2.6258 | 118667 | 28005 |
| holdout | candidate | 45192 | 19609 | 0.4339 | 2.4478 | 110623 | 29571 |

Candidate beats `current_default` on full train, validation, and holdout splits.
This avoids the `TRAINER_OVERFITS_ONLY` failure mode.

Phase breakdown:

| config | phase | rows | agreements | rate | avg rank |
| --- | --- | ---: | ---: | ---: | ---: |
| candidate | late | 113928 | 50117 | 0.4399 | 2.3333 |
| candidate | midgame | 167989 | 72859 | 0.4337 | 2.5131 |
| candidate | opening | 17996 | 7786 | 0.4327 | 2.3070 |
| `current_default` | late | 113928 | 46888 | 0.4116 | 2.5152 |
| `current_default` | midgame | 167989 | 66892 | 0.3982 | 2.7438 |
| `current_default` | opening | 17996 | 7140 | 0.3968 | 2.4185 |

Empties-bucket breakdown:

| config | empties | rows | agreements | rate | avg rank |
| --- | --- | ---: | ---: | ---: | ---: |
| candidate | 0-12 | 42951 | 20143 | 0.4690 | 2.1165 |
| candidate | 13-20 | 70977 | 29974 | 0.4223 | 2.4645 |
| candidate | 21-32 | 105991 | 46199 | 0.4359 | 2.5230 |
| candidate | 33-44 | 61998 | 26660 | 0.4300 | 2.4961 |
| candidate | 45-60 | 17996 | 7786 | 0.4327 | 2.3070 |
| `current_default` | 0-12 | 42951 | 18994 | 0.4422 | 2.2407 |
| `current_default` | 13-20 | 70977 | 27894 | 0.3930 | 2.6813 |
| `current_default` | 21-32 | 105991 | 43010 | 0.4058 | 2.7469 |
| `current_default` | 33-44 | 61998 | 23882 | 0.3852 | 2.7384 |
| `current_default` | 45-60 | 17996 | 7140 | 0.3968 | 2.4185 |

## Exact Overlap

Official exact metrics come from `othello_eval_vs_exact --move-rank-analysis`.
The overlap contains 10000 late/endgame positions with 7-12 empties.

| config | sign agreements | wrong direction | high-confidence wrong | exact-best top group | exact-best rank sum | avg exact-best rank |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `current_default` | 7596 / 10000 | 2045 | 213 | 5166 | 19602 | 1.960 |
| candidate | 7641 / 10000 | 2006 | 168 | 5121 | 19790 | 1.979 |

Interpretation:

- exact sign agreement improved
- wrong-direction and high-confidence wrong counts improved
- exact-best top-group hit rate regressed slightly
- exact-best rank sum regressed slightly

This exact-best rank regression is the main known risk. It blocks any default
promotion from this evidence alone, even though exact sign safety improved.

## Search Smoke

The initial evaluation candidate matrix ran search-bench smoke at depths 5, 6,
and 7.

| config | status | nodes | elapsed ms | result |
| --- | --- | ---: | ---: | --- |
| `current_default` | passed | 17467 | 12.83 | baseline |
| candidate | passed | 23870 | 15.15 | changed checksums |

Checksum changes are expected for a behavior-changing evaluation candidate and
are not regressions by themselves. No search-bench crashes or timeouts were
observed in this smoke matrix.

## Broader Search and Exact Follow-up

A follow-up evaluation candidate matrix was run at search depths 5, 6, 7, and 8
with exact-overlap move-rank analysis.

| config | eval status | search status | nodes | elapsed ms | exact root searches |
| --- | --- | --- | ---: | ---: | ---: |
| `current_default` | passed | passed | 48569 | 27.46 | 0 |
| candidate | passed | passed | 65437 | 32.05 | 0 |

Candidate search overhead versus `current_default`:

| metric | delta |
| --- | ---: |
| nodes | +16868 (+34.73%) |
| elapsed ms | +4.59 (+16.70%) |

Broader exact-overlap metrics:

| config | sign agreements | wrong direction | high-confidence wrong | exact-best top group | exact-best rank sum |
| --- | ---: | ---: | ---: | ---: | ---: |
| `current_default` | 7596 / 10000 | 2045 | 213 | 5166 | 19602 |
| candidate | 7641 / 10000 | 2006 | 168 | 5121 | 19790 |

The follow-up confirms the earlier pattern: exact sign safety improves, while
exact-best top-group and rank metrics regress slightly. No search crash,
timeout, or illegal result was observed.

## Deterministic Match Evidence

Candidate was matched against `current_default` with fixed openings, side swap,
and seed `20260602`.

| run | games | candidate wins | current_default wins | draws | errors | avg disc diff | median diff |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| depth 4 | 820 | 456 | 353 | 11 | 0 | +3.02 | +4.00 |
| depth 5 | 410 | 222 | 177 | 11 | 0 | +2.29 | +2.00 |
| depth 6 | 400 | 221 | 174 | 5 | 0 | +0.16 | +4.00 |
| depth 7 | 200 | 106 | 89 | 5 | 0 | +1.89 | +6.00 |

This is stronger evidence than teacher agreement alone: the candidate wins
these deterministic match runs without illegal moves or error games. It is
still not an Elo estimate or a default-promotion result.

## Broader Deterministic Match Follow-up

Because the depth 6 average disc margin was thin in the first broader check, two
additional deterministic match runs were collected with the same seed, opening
suite, side-swap policy, candidate, and baseline.

| run | games | candidate wins | current_default wins | draws | errors | avg disc diff | median diff | candidate avg ms | current_default avg ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| depth 6 | 1000 | 544 | 443 | 13 | 0 | +0.06 | +4.00 | 103.67 | 83.24 |
| depth 7 | 800 | 432 | 346 | 22 | 0 | +2.46 | +6.00 | 436.28 | 342.70 |

The depth 6 result remains favorable by wins but nearly even by average disc
margin. The depth 7 result is favorable by wins and average disc margin. This
supports continuing toward promotion-oriented review, while keeping the
exact-best rank regression and search overhead visible.

## Verdict

`PROMISING_FOR_PROMOTION_PR_PREP`

The candidate looks stable and favorable against `current_default` in these
deterministic match runs, including the deeper depth 7 follow-up. It also wins
full teacher validation and improves exact sign safety. The exact-best rank
regression and search overhead remain known risks, so a future promotion PR
should carry this evidence explicitly and should still be reviewed separately
from this documentation note.

## Recommended Next Steps

1. Prepare a separate promotion-oriented PR only if the project is ready to
   evaluate this candidate as a reversible evaluator preset/default change.
2. Include the depth 6 and depth 7 deterministic match evidence, exact-overlap
   sign/rank metrics, and search overhead in that PR.
3. Inspect exact-best rank regressions before further trainer changes.
4. Do not treat this report alone as automatic evaluator-default promotion.
