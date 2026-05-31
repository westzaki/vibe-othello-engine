# Evaluation

## Purpose

This document describes how to improve the engine's evaluation function.

It is not a snapshot of the current evaluator, a list of current weights, or a
queue of current tuning tasks.

Current hypotheses, regressions, one-off investigation notes, raw outputs, and
candidate-specific conclusions belong in issues, PR descriptions, task prompts,
experiment reports, or `runs/`.

Evaluation work should be:

- bold enough to improve playing strength
- autonomous enough to generate and test new ideas
- measurable enough to reject weak ideas
- isolated enough to compare and roll back
- separate from exact endgame correctness

## Principles

### Isolated does not mean tiny

Do not make evaluator candidates artificially tiny.

For evaluation work, "isolated" means hypothesis-focused, named, comparable, and
reversible. It does not mean changing only one scalar, one feature, or one line.

A candidate may change:

- one feature
- a related group of features
- phase policy
- feature normalization
- a pattern family
- a weight set
- a named evaluator preset
- the evaluator architecture

The requirement is that the candidate tests one coherent idea and can be compared
as a unit.

### Stronger matters more than familiar

Do not restrict evaluation experiments to the current feature set.

Existing handcrafted features are useful starting points, not boundaries. New
features, feature groups, pattern evaluators, tuned weight sets, or named
presets are allowed when they are testable.

Prefer ideas that can plausibly improve Othello play, not only ideas that are
easy to implement.

### Correctness is separate from strength

Evaluation changes may intentionally change scores, best moves, principal
variations, benchmark checksums, and match outcomes.

Those changes are expected evidence for evaluation work, not automatic
regressions.

Correctness checks still matter. Rule behavior, legal moves, move application,
terminal scoring, and exact endgame results must not change unless the task
explicitly changes those systems.

### Exact endgame remains exact

Heuristic evaluation must not change exact endgame results.

Evaluation may guide depth-limited search, diagnostics, move ordering experiments,
or pre-endgame decisions. It must not contaminate exact disc-difference
semantics.

When measuring midgame evaluation behavior, configure the run so exact root
endgame solving does not hide the evaluator being tested.

## Candidate Types

Evaluation candidates may take many forms.

A feature candidate changes one feature.

A feature-group candidate changes related features together. This is useful when
features interact, such as corner ownership, corner access, X-square danger,
C-square danger, mobility, frontier, or stability.

A phase-policy candidate changes how evaluation depends on game phase. This may
include phase boundaries, smooth phase interpolation, phase-specific feature
activation, or phase-specific weights.

A pattern candidate introduces a pattern feature family, such as edge patterns,
corner neighborhoods, rows, columns, diagonals, or local board patterns.

A weight-set candidate changes multiple weights while keeping the evaluator
structure.

A preset candidate introduces a named evaluator preset that can be selected,
compared, and rolled back.

Large candidates are allowed when they are named, measurable, and reversible.

## Othello Evaluation Directions

This section lists durable idea families, not a checklist.

Useful evaluation directions include:

- legal mobility and opponent mobility
- potential mobility
- forced moves and pass pressure
- corner ownership and corner access
- X-square and C-square danger
- edge shape and edge stability
- stable, semi-stable, and unstable discs
- frontier discs and exposed discs
- parity and region structure
- phase-aware feature weighting
- pattern-based evaluation
- runtime-aware evaluation

These ideas often interact. Do not assume a single scalar can fix an interaction
between corners, mobility, stability, and phase.

A candidate should be large enough to test the real hypothesis.

## Candidate Batches

One experiment may evaluate multiple named candidates.

Candidate batches are useful for:

- weight tuning
- feature interaction tests
- phase tuning
- pattern-family comparison
- cheap rejection of bad ideas

Each candidate in a batch must be named, measured, and reported separately.

A PR may contain a candidate batch when the purpose is experimentation. Promoting
a new default evaluator should normally promote one selected candidate at a time.

## Autonomous Evaluation Loop

When asked to improve evaluation, an agent should normally follow this loop.

### 1. Establish the baseline

Identify the current evaluator, search settings, benchmark profile, position
suite, match profile, and constraints.

Do not treat old experiment notes as current instructions.

### 2. Generate hypotheses

Generate hypotheses from:

- evaluation breakdowns
- fixed-position analysis
- search behavior
- match divergences
- Othello strategy
- runtime bottlenecks
- active issues or task prompts
- optional external-engine evidence

Hypotheses should not be limited to existing features.

### 3. Create named candidates

Create one named candidate or a small batch of named candidates.

Do not shrink a candidate into a trivial tweak if the hypothesis concerns a
feature interaction, phase policy, pattern family, or evaluator architecture.

### 4. Run correctness checks

Run relevant correctness checks before making strength claims.

Correctness checks protect against accidental rule, search, or exact-endgame
breakage. They do not prove that the evaluator is stronger.

### 5. Run fixed-position analysis

Use representative positions to understand candidate behavior.

Useful outputs include:

- score
- best move
- principal variation
- evaluation breakdown
- root candidate ordering
- search stats
- phase and empty count
- runtime and node count

Do not overfit to one position.

The committed `data/positions/evaluation/diagnostic_suite.txt` file is a small
curated diagnostic suite for this step. It covers semantic situations such as
corner access, X-square danger, mobility pressure, frontier-heavy shapes, edge
patterns, and late-pre-endgame boards where exact root solving should be
disabled when measuring heuristic midgame evaluation. The suite is intentionally
small and is not a representative training distribution or strength proof.

Examples:

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 4,5 \
  --positions evaluation \
  --repetitions 1 \
  --tt on \
  --pvs on \
  --exact-endgame-threshold 0 \
  --by-position
```

```sh
./build/othello_analyze_position \
  --board-file data/positions/evaluation/corner_access_a1.txt \
  --depth 1 \
  --exact-endgame-threshold 0
```

For exact-label or eval-vs-exact workflows, reuse the same committed positions
with `--skip-sampling` and keep generated labels/reports under `runs/`.

Evaluator candidate matrices can use the same search-bench position selector:

```sh
python3 tools/scripts/eval_experiment_matrix.py \
  --build-dir build \
  --out runs/eval-matrix/evaluation-diagnostic \
  --presets default,phase_aware_v1 \
  --positions evaluation \
  --small-depths 4 \
  --extended-depths 5 \
  --small-games 2 \
  --extended-games 2 \
  --openings data/openings/smoke_openings.txt \
  --seed 20260531 \
  --exact-endgame-threshold 0
```

### 6. Run benchmark profiles

Use current benchmark guidance to compare runtime and search behavior.

Keep settings stable within one comparison. Do not treat old depths, repetitions,
or game counts as permanent standards.

Track score changes, best-move changes, PV changes, checksum changes, node count,
wall-clock time, evaluation calls, and relevant search stats.

### 7. Run match or base/head comparison when useful

For strength-related changes, fixed-position analysis is not enough.

Use self-play, paired openings, base/head comparison, or external-engine games
when available and appropriate.

Small match samples are smoke evidence, not Elo. Strong claims need broader
coverage.

### 8. Decide

Classify each candidate as one of:

- reject
- keep experimental
- needs more data
- promote as named preset
- promote as default

Mixed evidence should usually lead to "keep experimental" or "needs more data",
not immediate default promotion.

### 9. Report

Write a concise report with enough evidence for a reviewer or future agent to
understand the decision.

Do not turn a task-specific report into permanent guidance unless the conclusion
is durable.

## Progress Rule

An evaluation-improvement task should normally produce at least one of:

- a named evaluator candidate
- a candidate batch with a report
- a fixed-position or match report that directly informs evaluator choice
- a clearly necessary prerequisite that enables the next named candidate

Tooling-only, docs-only, or refactor-only work is valid when it makes the next
evaluation decision possible.

Repeated prerequisite-only work is not enough unless the current task explicitly
asks for cleanup, documentation, tooling, or refactoring.

## Promotion Guidance

Reject a candidate when it fails correctness checks, clearly worsens search or
match results, is slower without compensating evidence, only helps one narrow
position while hurting broader behavior, or is too hard to compare or roll back.

Keep a candidate experimental when evidence is mixed, it helps one phase but
hurts another, it improves fixed positions but not matches, it improves matches
but has unclear runtime cost, or it needs broader testing.

Promote a candidate as a named preset when it is isolated, selectable, useful for
further testing, but not ready to replace the default.

Promote a candidate as default only when correctness checks pass, evidence is
stronger than a smoke test, runtime cost is acceptable or justified, and the
change is not overfit to one position or opening set.

Default promotion should normally select one candidate, not a batch.

## Report Shape

Evaluation reports should include:

- candidate or batch name
- hypothesis
- base ref and head ref
- build type
- candidate description
- expected benefit
- expected risk
- correctness checks
- fixed-position analysis
- benchmark profile
- match or base/head profile when run
- positive evidence
- negative evidence
- runtime impact
- search behavior impact
- decision
- next action

Raw local output belongs under `runs/`. Commit only curated reports with lasting
comparison value.

## Overfitting Risks

Avoid these traps:

- optimizing for one tactical fixture
- optimizing for one opening suite
- optimizing shallow search while harming deeper search
- optimizing score agreement without checking match behavior
- optimizing NPS while weakening move choice
- trusting old historical notes as current guidance
- making a candidate too tiny to test the real hypothesis
- changing evaluation and search semantics at the same time without isolation

## Implementation Guidance

Prefer mechanisms that make candidates easy to compare or roll back:

- named presets
- explicit options
- small configuration files
- isolated feature functions
- evaluation breakdown fields
- tests for sign conventions and invariants

Do not over-engineer configuration before it is useful. A simple named preset is
often enough for early experiments.

When adding features, keep breakdowns and diagnostics useful enough to explain
which feature or feature group changed the score.

## What Not to Put Here

Do not put the following in this document:

- current evaluator weights
- current tactical regressions
- current best candidate names
- one-off benchmark results
- latest baseline links
- active investigation notes
- temporary task instructions
- raw experiment output

Put those in issues, PR descriptions, task prompts, experiment reports, or
`runs/`.
