# NTest Pairwise Full V2 Evaluator Candidate

This note records the source-controlled evaluator preset created from the NTest
300K regularized pairwise full v2 run. It is a reviewable candidate, not an
automatic default promotion and not a formal Elo claim.

## Files

- config: `data/eval/ntest_pairwise_full_v2.eval`
- opening table: `data/eval/patterns/ntest_pairwise_full_v2_opening.tsv`
- midgame table: `data/eval/patterns/ntest_pairwise_full_v2_midgame.tsv`
- late table: `data/eval/patterns/ntest_pairwise_full_v2_late.tsv`
- evidence report:
  `docs/experiments/ntest_balanced300k_regularized_pairwise_full_v2_report.md`

The preset keeps the `current_default` scalar evaluator snapshot and adds
phase-specific learned pattern-table deltas with weight `1` in each phase.
`current_default.eval` and the built-in default evaluator are unchanged.

## Evidence Summary

Teacher agreement:

| split | current_default | candidate |
| --- | ---: | ---: |
| validation | 0.4042 | 0.4341 |
| holdout | 0.4037 | 0.4339 |

Exact overlap:

| metric | current_default | candidate |
| --- | ---: | ---: |
| sign agreement | 7596 / 10000 | 7641 / 10000 |
| high-confidence wrong | 213 | 168 |
| exact-best top group | 5166 | 5121 |
| exact-best rank sum | 19602 | 19790 |

Deterministic matches against `current_default`:

| depth | games | candidate wins | current_default wins | draws | avg disc diff |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4 | 820 | 456 | 353 | 11 | +3.02 |
| 5 | 410 | 222 | 177 | 11 | +2.29 |
| 6 | 1000 | 544 | 443 | 13 | +0.06 |
| 7 | 800 | 432 | 346 | 22 | +2.46 |

Search overhead in the broader search matrix:

- nodes: +34.73%
- elapsed: +16.70%

## Known Risks

- Exact-best top-group and exact-best rank-sum metrics regress slightly.
- The candidate has measurable search overhead.
- NTest teacher agreement is teacher evidence, not exact truth.
- The deterministic matches are fixed-suite validation evidence, not a formal
  Elo estimate or proof that the candidate is stronger in all settings.

## Comparison Command

Run a cheap search smoke:

```sh
build/othello_search_bench \
  --eval-config data/eval/ntest_pairwise_full_v2.eval \
  --mode iterative \
  --depths 5,6,7,8 \
  --positions smoke \
  --repetitions 1 \
  --tt on \
  --pvs on \
  --aspiration on \
  --exact-endgame-threshold 0 \
  --format jsonl
```

Run a deterministic match comparison:

```sh
build/othello_match_runner \
  --black "search:depth=6,eval_config=data/eval/ntest_pairwise_full_v2.eval" \
  --white "search:depth=6,eval_config=data/eval/current_default.eval" \
  --games 200 \
  --swap-sides true \
  --seed 20260602 \
  --openings data/openings/eval_regression_openings.txt \
  --output runs/eval-comparison/ntest-pairwise-full-v2-depth6-200.jsonl \
  --format jsonl \
  --quiet
```

## Revert Plan

This candidate is reversible by removing:

- `data/eval/ntest_pairwise_full_v2.eval`
- `data/eval/patterns/ntest_pairwise_full_v2_opening.tsv`
- `data/eval/patterns/ntest_pairwise_full_v2_midgame.tsv`
- `data/eval/patterns/ntest_pairwise_full_v2_late.tsv`

If a future PR promotes this candidate to the default evaluator, that PR must
also update `data/eval/current_default.eval`, the built-in default evaluator,
and the corresponding default-evaluator tests in the same explicit change.
