# Scalar-Free Pattern Feasibility

This report investigates scalar-free pattern-only evaluation. It does not remove
the scalar fallback implementation and does not promote a scalar-zero
configuration to `current_default.eval`.

Existing full-v2 pattern tables were trained as learned deltas over the scalar
fallback. Therefore, zeroing scalar weights under those tables is diagnostic
only.

## Hypothesis

A pattern-only project default should require retrained full pattern tables, not
just `scalar = 0` under the existing delta tables. The feasibility question is
whether scalar-free candidates can improve exact-best ranking, teacher
agreement, and match outcomes without relying on scalar hand-tuning.

The exact solver, search mode, MPC, move ordering, and built-in fallback
evaluator were not changed.

## Candidates

| id | config | purpose |
|---|---|---|
| A | `data/eval/current_default.eval` | active default: scalar fallback plus learned full-v2 delta tables |
| B | `data/eval/scalar_free_existing_delta_pattern_only.eval` | existing delta tables with scalar weights omitted through `mode=pattern_only` |
| C | `data/eval/ntest_pairwise_full_v2.eval` | previous default shape before the PR290 late scalar diagnostic blend |
| D | `runs/eval-tuning/scalar-free-pattern-feasibility/retrained-scalar-free-depth26-train/candidate.eval` | scalar-free retrained pattern candidate generated with `--no-base-margin` |

Candidate D is a local `runs/` artifact, not a source-controlled promotion
candidate. It proves the trainer can generate a scalar-free pattern candidate,
but the measured result below does not pass the promotion bar.

## Pattern-Only Diagnostic Config

This PR adds `data/eval/scalar_free_existing_delta_pattern_only.eval` as an
explicit diagnostic candidate. It references the existing full-v2 phase-specific
pattern tables and uses `mode=pattern_only`, so omitted scalar feature weights
default to zero.

This file exists to prevent accidental promotion by making the diagnostic shape
named, reproducible, and clearly marked as not a default candidate.

## Exact-Overlap Matrix

Command shape:

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build/release \
  --labels dataset:teacher/ntest-balanced300k-v0-exact-overlap-v0/exact-overlap/labels.jsonl \
  --baseline-config data/eval/current_default.eval \
  --candidates data/eval/ntest_pairwise_full_v2.eval \
               data/eval/scalar_free_existing_delta_pattern_only.eval \
  --out runs/eval-tuning/scalar-free-pattern-feasibility/matrix-existing \
  --search-depths 5,6,7 \
  --positions evaluation \
  --repetitions 1 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis
```

| metric | A current_default | C previous shape | B scalar-free existing delta |
|---|---:|---:|---:|
| exact sign agreement | 8005 / 10000 | 7641 / 10000 | 3818 / 10000 |
| wrong direction | 1650 | 2006 | 5676 |
| high-confidence wrong | 220 | 168 | 0 |
| exact-best top group | 5582 | 5121 | 2990 |
| exact-best rank sum | 18434 | 19790 | 31132 |
| avg exact-best rank | 1.843 | 1.979 | 3.113 |
| search nodes | 214792 | 212240 | 294720 |
| search node delta vs A | baseline | -1.19% | +37.21% |
| search elapsed ms | 89.19 | 88.29 | 103.22 |
| search elapsed delta vs A | baseline | -1.00% | +15.73% |

B is clearly weaker on exact-overlap move ranking and search overhead. This
supports the concern that the existing pattern tables are delta tables over the
scalar fallback.

## Scalar-Free Retraining Smoke

Command shape:

```sh
python3 tools/scripts/regularized_pairwise_pattern_train.py \
  --teacher-labels dataset:teacher.ntest_depth26_2027:train \
  --exact-labels dataset:teacher.ntest_depth26_2027:exact_train \
  --eval-config data/eval/scalar_free_existing_delta_pattern_only.eval \
  --analyze-position build/release/othello_analyze_position \
  --out-dir runs/eval-tuning/scalar-free-pattern-feasibility/retrained-scalar-free-depth26-train \
  --analysis-jobs 4 \
  --families broad_all \
  --split train \
  --pair-mode exact-aware \
  --pair-weighting exact-boost \
  --guard-mode base-agreement \
  --guard-weight 0.25 \
  --l2 0.001 \
  --epochs 5 \
  --learning-rate 0.1 \
  --output-scale 1 \
  --no-base-margin \
  --seed 20260607
```

Trainer summary:

| metric | value |
|---|---:|
| accepted teacher rows | 1234 |
| preference pairs | 10339 |
| model margin | delta only |
| initial weighted loss | 0.693147 |
| final weighted loss | 0.419668 |
| final weighted accuracy | 0.985198 |

The trainer supports a scalar-free run, but this small retrain is not a strength
candidate.

Exact-overlap matrix for D:

| metric | A current_default | D retrained scalar-free |
|---|---:|---:|
| exact sign agreement | 8005 / 10000 | 3023 / 10000 |
| wrong direction | 1650 | 1657 |
| high-confidence wrong | 220 | 0 |
| exact-best top group | 5582 | 7640 |
| exact-best rank sum | 18434 | 15988 |
| avg exact-best rank | 1.843 | 1.599 |
| search nodes | 214792 | 167899 |
| search node delta vs A | baseline | -21.83% |
| search elapsed ms | 92.55 | 98.15 |
| search elapsed delta vs A | baseline | +6.05% |

D improves exact-best top group, exact-best rank, and node count, but sign
agreement collapses. That score-direction failure means it should not be
promoted without a better objective, calibration, and held-out match evidence.

## NTest Teacher Agreement

Command shape:

```sh
python3 tools/scripts/teacher_label_mistake_mining.py \
  --teacher-labels dataset:teacher.ntest_depth26_2027:validation \
  --exact-labels dataset:teacher.ntest_depth26_2027:exact_heldout \
  --config current_default=data/eval/current_default.eval \
  --config pr290_previous_shape=data/eval/ntest_pairwise_full_v2.eval \
  --config scalar_free_existing_delta=data/eval/scalar_free_existing_delta_pattern_only.eval \
  --out runs/eval-tuning/scalar-free-pattern-feasibility/teacher-depth26-validation \
  --build-dir build/release \
  --depth 1 \
  --exact-endgame-threshold 0
```

| config | rows | teacher agreements | teacher rank sum |
|---|---:|---:|---:|
| A current_default | 384 | 179 | 877 |
| C previous shape | 384 | 179 | 879 |
| B scalar-free existing delta | 384 | 55 | 1956 |

Retrained D on the same validation rows:

| config | rows | teacher agreements | teacher rank sum |
|---|---:|---:|---:|
| A current_default | 384 | 179 | 877 |
| D retrained scalar-free | 384 | 83 | 753 |

D improves teacher rank sum but loses top-1 teacher agreement badly. That is
useful training signal, not promotion evidence.

## Deterministic Self-Play

Commands used depth-6 search, `swap-sides=true`, seed `20260607`, and
`data/openings/eval_regression_openings.txt`.

| match | games | A wins | candidate wins | draws | avg disc diff from A |
|---|---:|---:|---:|---:|---:|
| A current_default vs B scalar-free existing delta | 100 | 100 | 0 | 0 | +47.74 |
| A current_default vs D retrained scalar-free | 100 | 100 | 0 | 0 | +49.04 |

Neither scalar-free candidate is competitive against `current_default` in this
smoke match.

## NTest Weak No-Book Match

NTest weak no-book match evidence was attempted after B and D had already failed
the internal adoption checks. The external NTest runner was too slow for this
turn and was interrupted before producing a complete JSONL result. No NTest
match strength claim is made in this report.

The attempted NTest depth-4 run logged missing `JA_s4.book`, so the environment
appeared to be no-book, but there is no completed match summary. Follow-up work
should run smaller external smoke first or use a persistent NBoard process
lifecycle before collecting ntest4/ntest5/ntest6 comparisons.

## Failure Suite Accuracy

Command shape:

```sh
python3 tools/scripts/eval_candidate_matrix.py \
  --build-dir build/release \
  --labels data/labels/exact_label_tiny.jsonl \
  --baseline-config data/eval/current_default.eval \
  --candidates data/eval/scalar_free_existing_delta_pattern_only.eval \
               runs/eval-tuning/scalar-free-pattern-feasibility/retrained-scalar-free-depth26-train/candidate.eval \
  --out runs/eval-tuning/scalar-free-pattern-feasibility/failure-suite-tiny \
  --search-depths 5 \
  --positions smoke \
  --repetitions 1 \
  --exact-endgame-threshold 0 \
  --move-rank-analysis
```

| config | records | sign agreements | wrong direction | exact-best top group |
|---|---:|---:|---:|---:|
| A current_default | 3 | 3 | 0 | 1 / 1 |
| B scalar-free existing delta | 3 | 1 | 2 | 1 / 1 |
| D retrained scalar-free | 3 | 1 | 2 | 1 / 1 |

The tiny suite is not a strength benchmark, but it confirms the scalar-free
candidates also fail a small sign-direction smoke.

## Decision

Do not promote a pattern-only `current_default.eval` in this PR.

B fails exact-overlap ranking, teacher agreement, search overhead, self-play,
and the tiny sign-direction suite. This confirms that simply zeroing scalar
weights under existing delta tables is not viable.

D shows that scalar-free retraining is technically possible and can improve
exact-best rank metrics, but it fails sign agreement, NTest teacher top-1
agreement, and self-play. It is useful follow-up evidence, not a default
candidate.

The PR290 late scalar blend report remains a diagnostic scalar experiment. It is
not a policy direction to continue scalar hand-tuning as the default improvement
path.

## Follow-Up

- Train scalar-free full pattern tables on a larger dataset with a held-out
  objective that preserves sign direction and exact-best rank.
- Add explicit score calibration or objective terms so scalar-free tables do not
  optimize move rank while losing score-direction semantics.
- Re-run NTest weak no-book matches after the candidate passes internal
  current-default self-play and teacher validation.
- Keep scalar fallback as a baseline and public-library fallback while pattern
  candidates mature.
