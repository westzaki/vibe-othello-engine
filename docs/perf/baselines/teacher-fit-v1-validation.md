# teacher_fit_v1 Validation

## Summary

- Decision: keep_experimental
- Default promotion: none
- Strength claim: none
- Base config: `data/eval/current_default.eval`
- Candidate config: `data/eval/teacher_fit_v1.eval`
- Source SHA: `3bb2803463c727584b6c3f669b109dd791afd7d5`
- Build type: Release

## Decision

`teacher_fit_v1` remains a current experimental config. It was neutral-to-positive
on two broader held-out exact-label sets and completed search bench plus match
smoke without failures.

This is not enough evidence for default promotion. The held-out diagnostic
objective improved on both sampled sets, but the match comparison was a small
12-game smoke run that ended 6-6. Search checksums changed as expected for a
different evaluator config, so this is semantic-change evidence rather than a
pure speed comparison.

## Commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m unittest discover tools/scripts/tests
git diff --check
python3 tools/scripts/exact_label_workflow.py --build-dir build --out runs/teacher-fit-v1-validation/heldout-a --count 64 --target-empties 6,8,10,12,14 --seed 20260601 --max-empties 14 --eval-preset default --analyze
python3 tools/scripts/exact_label_workflow.py --build-dir build --out runs/teacher-fit-v1-validation/heldout-b --count 64 --target-empties 6,8,10,12,14 --seed 20260602 --max-empties 14 --eval-preset default --analyze
./build/othello_eval_vs_exact --labels runs/teacher-fit-v1-validation/heldout-a/labels.jsonl --eval-config data/eval/current_default.eval --output runs/teacher-fit-v1-validation/current-default-heldout-a.md --high-confidence-threshold 250 --phase-breakdown
./build/othello_eval_vs_exact --labels runs/teacher-fit-v1-validation/heldout-a/labels.jsonl --eval-config data/eval/teacher_fit_v1.eval --output runs/teacher-fit-v1-validation/teacher-fit-v1-heldout-a.md --high-confidence-threshold 250 --phase-breakdown
./build/othello_eval_vs_exact --labels runs/teacher-fit-v1-validation/heldout-b/labels.jsonl --eval-config data/eval/current_default.eval --output runs/teacher-fit-v1-validation/current-default-heldout-b.md --high-confidence-threshold 250 --phase-breakdown
./build/othello_eval_vs_exact --labels runs/teacher-fit-v1-validation/heldout-b/labels.jsonl --eval-config data/eval/teacher_fit_v1.eval --output runs/teacher-fit-v1-validation/teacher-fit-v1-heldout-b.md --high-confidence-threshold 250 --phase-breakdown
./build/othello_search_bench --mode both --depths 4,5,6 --positions smoke --repetitions 1 --eval-config data/eval/current_default.eval --exact-endgame-threshold 0 --format jsonl
./build/othello_search_bench --mode both --depths 4,5,6 --positions smoke --repetitions 1 --eval-config data/eval/teacher_fit_v1.eval --exact-endgame-threshold 0 --format jsonl
./build/othello_search_bench --mode both --depths 4,5,6 --positions suite --repetitions 1 --eval-config data/eval/current_default.eval --exact-endgame-threshold 0 --format jsonl
./build/othello_search_bench --mode both --depths 4,5,6 --positions suite --repetitions 1 --eval-config data/eval/teacher_fit_v1.eval --exact-endgame-threshold 0 --format jsonl
./build/othello_match_runner --black search:depth=4,tt=on,pvs=on,exact=off,eval_config=data/eval/teacher_fit_v1.eval --white search:depth=4,tt=on,pvs=on,exact=off,eval_config=data/eval/current_default.eval --games 12 --swap-sides true --seed 20260601 --openings data/openings/smoke_openings.txt --output runs/teacher-fit-v1-validation/teacher-fit-v1-vs-current-default.jsonl
python3 tools/scripts/match_summary.py --input runs/teacher-fit-v1-validation/teacher-fit-v1-vs-current-default.jsonl --by-opening
```

## Held-Out Exact Labels

Diagnostic objective used for comparison:

```text
sign_agreements - 2 * wrong_direction - high_confidence_wrong_direction
```

| Label Set | Config | Analyzed | Sign Agreements | Wrong Direction | High-Confidence Wrong Direction | Objective |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| heldout-a | `current_default` | 64 | 45 | 16 | 1 | 12 |
| heldout-a | `teacher_fit_v1` | 64 | 46 | 15 | 1 | 15 |
| heldout-b | `current_default` | 64 | 51 | 12 | 2 | 25 |
| heldout-b | `teacher_fit_v1` | 64 | 51 | 10 | 2 | 29 |

`teacher_fit_v1` improved the diagnostic objective by `+3` on heldout-a and
`+4` on heldout-b. All sampled positions were in the evaluator's late phase,
which matches the only changed key.

Caveat: these are random playout exact labels. They are useful held-out
diagnostics, not a representative Othello distribution.

## Search Bench

Both configs completed the required smoke benchmark at depths `4,5,6` with
`--exact-endgame-threshold 0`. The broader `suite` position set was also cheap
enough to run with the same depths and settings, and both configs completed it.

Smoke aggregate:

| Config | Depth | Mode | Nodes | Elapsed ms | Result Checksum | Work Checksum |
| --- | ---: | --- | ---: | ---: | --- | --- |
| `current_default` | 4 | fixed | 464 | 0.1045 | `13638507097085274603` | `7279467373092107503` |
| `teacher_fit_v1` | 4 | fixed | 464 | 0.10125 | `13638960095875918315` | `7338577118201345263` |
| `current_default` | 5 | fixed | 1399 | 0.255708 | `2902611684074624550` | `5828777889555978287` |
| `teacher_fit_v1` | 5 | fixed | 1395 | 0.25475 | `2902981119981557286` | `5781490093468581423` |
| `current_default` | 6 | fixed | 4461 | 0.820375 | `4555978845442580845` | `2035714531769247505` |
| `teacher_fit_v1` | 6 | fixed | 4295 | 0.791208 | `4556159165349536109` | `2061047279673148433` |
| `current_default` | 6 | iterative | 6098 | 1.105 | `2469013376931296458` | `11132965099432936045` |
| `teacher_fit_v1` | 6 | iterative | 5933 | 1.0715 | `2469483967907984586` | `11122269050317956333` |

Suite aggregate:

| Config | Depth | Mode | Nodes | Elapsed ms | Result Checksum | Work Checksum |
| --- | ---: | --- | ---: | ---: | --- | --- |
| `current_default` | 4 | fixed | 8884 | 1.95092 | `5507123435641987930` | `15137196865447160528` |
| `teacher_fit_v1` | 4 | fixed | 8849 | 3.62658 | `17849171102368438695` | `11183341462936235350` |
| `current_default` | 5 | fixed | 38109 | 7.19167 | `15707399572682516306` | `16925489933830840229` |
| `teacher_fit_v1` | 5 | fixed | 38097 | 7.21379 | `8308546752977821907` | `1417384199227110774` |
| `current_default` | 6 | fixed | 93226 | 20.8633 | `3514198602011639486` | `13871050226167385402` |
| `teacher_fit_v1` | 6 | fixed | 93564 | 21.0555 | `12971489557045297715` | `10619125814430527805` |
| `current_default` | 6 | iterative | 119495 | 25.6388 | `10863307703261201245` | `653618634968024512` |
| `teacher_fit_v1` | 6 | iterative | 119986 | 26.0577 | `1874274340206991824` | `4825047904018607677` |

Result and work checksums differ between configs, which is expected semantic
change evidence for an evaluator config comparison. There were no search bench
command failures.

## Match Comparison

The paired match smoke completed with 12 valid games and 0 error games.

- Games: `12`
- A (`teacher_fit_v1`) wins: `6`
- B (`current_default`) wins: `6`
- Draws: `0`
- Average disc diff from A perspective: `5.67`
- Average nodes: A `23096.83`, B `22975.33`
- Average time ms: A `15.28`, B `15.25`
- Total exact roots: A `0`, B `0`

By opening:

| Opening | Games | A Wins | B Wins | Draws | Avg Diff |
| --- | ---: | ---: | ---: | ---: | ---: |
| `initial` | 4 | 2 | 2 | 0 | -7.00 |
| `c4-c3` | 4 | 2 | 2 | 0 | 12.00 |
| `d3-c3-c4` | 4 | 2 | 2 | 0 | 12.00 |

This match sample is only smoke evidence. It is not Elo and not a strength
claim.

## Candidate Diff

- `late.disc_difference`: `4` -> `3`

## Caveats

- Evaluator scores are heuristic units.
- Exact labels are final disc margins.
- Random playout labels are not representative.
- Match sample is too small for strength claims.
- There is no default promotion recommendation.
- Raw outputs remain under `runs/` and are not committed.

## Next Step

Run base/head broader match validation for `teacher_fit_v1`.
