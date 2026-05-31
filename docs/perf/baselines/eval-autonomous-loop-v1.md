# Eval Autonomous Loop v1

## Summary

- Result: `keep_experimental`
- Experimental config added: `yes`
- Config path: `data/eval/teacher_fit_v1.eval`
- Base config: `data/eval/current_default.eval`
- Source SHA: `14e854cdc0789e27e65a26cf99a8ea67986c792b`
- Build type: `Release`
- Strength claim: `none`
- Default promotion: `none`

## Decision

This run keeps one curated experimental config, `teacher_fit_v1`, because the
smallest surviving candidate improved the held-out diagnostic objective and
completed search/match smoke validation without an obvious severe regression.
The candidate is intentionally narrow: it changes only `late.disc_difference`
from `4` to `3`.

This is not a strength claim and not a default-promotion recommendation. The
training result was weak because many candidates tied the base objective, so the
decision rests on the held-out diagnostic improvement plus smoke completion. The
search checksums changed versus base, which is semantic-change evidence, not
pure speed evidence.

## Commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m unittest discover tools/scripts/tests
git diff --check
python3 tools/scripts/exact_label_workflow.py --build-dir build --out runs/eval-autonomous-loop-v1/train-labels --count 32 --target-empties 6,8,10,12 --seed 20260531 --max-empties 12 --eval-preset default --analyze
python3 tools/scripts/exact_label_workflow.py --build-dir build --out runs/eval-autonomous-loop-v1/heldout-labels --count 32 --target-empties 6,8,10,12 --seed 20260532 --max-empties 12 --eval-preset default --analyze
python3 tools/scripts/eval_config_tuner.py --labels runs/eval-autonomous-loop-v1/train-labels/labels.jsonl --base-config data/eval/current_default.eval --build-dir build --out runs/eval-autonomous-loop-v1/tuner --rounds 2 --step 1 --max-candidates 96 --seed 20260531
python3 tools/scripts/eval_config_validate.py --validation-labels runs/eval-autonomous-loop-v1/heldout-labels/labels.jsonl --base-config data/eval/current_default.eval --candidate-dir runs/eval-autonomous-loop-v1/tuner/configs --train-summary runs/eval-autonomous-loop-v1/tuner/summary.tsv --build-dir build --out runs/eval-autonomous-loop-v1/heldout-validation --top 10
python3 tools/scripts/eval_config_search_validate.py --base-config data/eval/current_default.eval --candidate-dir runs/eval-autonomous-loop-v1/tuner/configs --heldout-summary runs/eval-autonomous-loop-v1/heldout-validation/summary.tsv --build-dir build --out runs/eval-autonomous-loop-v1/search-match-validation --top 3 --run-search-bench --run-match-smoke --depths 4,5 --games 4 --seed 20260531
python3 tools/scripts/evidence.py --profile smoke --build-dir build --out runs/eval-autonomous-loop-v1/evidence-smoke --skip-configure --skip-build
```

## Dataset

- Train count: `32`
- Held-out count: `32`
- Target empties: `6,8,10,12`
- Train seed: `20260531`
- Held-out seed: `20260532`
- Max empties: `12`
- Caveat: random playout labels are not representative.

## Tuner Result

- Base train objective: `16`
- Base train metrics: analyzed `32`, sign agreements `27`, wrong direction `5`,
  high-confidence wrong direction `1`
- Top train candidates: many candidates tied the base objective at `16`
- `candidate_0046` train objective: `16`, tied with base
- Top candidate changed keys: `candidate_0046` changed only
  `late.disc_difference`

The best train result was not a clear improvement because the top objective was
a broad tie. That makes the tuner result alone weak, and it should not be read
as a promotion signal.

## Held-Out Result

- Base held-out objective: `0`
- `candidate_0046`: objective `6`, delta vs base `+6`, analyzed `32`,
  sign agreements `23`, wrong direction `8`, high-confidence wrong direction `1`
- `candidate_0057`: objective `2`, delta vs base `+2`
- `candidate_0001`: objective `0`, delta vs base `0`

The top train candidate did not prove a broad training improvement, but
`candidate_0046` survived held-out validation with the best held-out diagnostic
objective among the evaluated candidates.

## Search Bench / Match Smoke

- Selected candidates: `candidate_0046`, `candidate_0057`, `candidate_0001`
- Search status: base and all selected candidates passed
- Match smoke status: all selected candidates passed
- Base search nodes/elapsed: `4322` nodes, `0.783` ms
- `candidate_0046` search nodes/elapsed: `4310` nodes, `0.749` ms
- `candidate_0046` match smoke: `4` games, A wins `2`, B wins `2`, draws `0`

Base search result checksum:
`fixed:4=13638507097085274603; iterative:4=13913872948554930513; fixed:5=2902611684074624550; iterative:5=6635352494286161937`

`candidate_0046` search result checksum:
`fixed:4=13638960095875918315; iterative:4=13914123637206063441; fixed:5=2902981119981557286; iterative:5=6635721930193094673`

`candidate_0046` also changed work checksums versus base. These checksum
changes are expected semantic-change evidence from using a different evaluator
config; they are not pure speed evidence and not a strength claim.

## Candidate Diff

- `late.disc_difference`: `4` -> `3`

## Caveats

- Evaluator scores are heuristic units.
- Exact labels are final disc margins.
- The tuner objective is diagnostic, not Elo.
- Held-out labels may still be biased.
- Random playout labels are not representative.
- Match smoke is too small for strength claims.
- There is no default promotion recommendation.
- Generated raw outputs remain under `runs/` and are not committed.

## Next Step

Run broader validation for `teacher_fit_v1` with larger held-out labels plus
broader search bench and match-runner comparisons before considering any
promotion-oriented PR.
