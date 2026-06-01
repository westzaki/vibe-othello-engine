# Shared Dataset Artifacts

Reusable teacher and exact datasets should live outside any Codex worktree.
Treat `runs/` as temporary per-worktree output: useful while generating or
debugging an experiment, but not a stable place for future training inputs.

Raw datasets remain uncommitted. Commit only examples, manifests, reports, and
small helpers that describe how to reproduce or locate those artifacts.

## Dataset Root

Scripts that support shared datasets resolve the root in this priority order:

1. explicit `--dataset-root PATH`
2. `VIBE_OTHELLO_DATASET_ROOT`
3. `config/datasets.local.toml`

If none is available, reusable dataset references fail with a setup error. They
do not silently fall back to an old worktree-local `runs/` directory.

Create a local config when an environment variable is inconvenient:

```sh
cp config/datasets.example.toml config/datasets.local.toml
```

Then edit `[datasets].root`. The local config is ignored by git.

## References

Use `dataset:relative/path` to read a file below the dataset root:

```sh
python3 tools/scripts/pattern_teacher_v0_train.py \
  --dataset-root /path/to/vibe-othello-datasets \
  --teacher-labels dataset:teacher/ntest-depth26-2027/labels/merged.jsonl \
  --eval-config data/eval/pattern_teacher_v0.eval \
  --out runs/pattern-training/table.tsv
```

Catalog references use the local TOML entry shape:

```sh
python3 tools/scripts/pattern_teacher_v0_train.py \
  --teacher-labels dataset:teacher.ntest_depth26_2027:labels \
  --exact-labels dataset:teacher.ntest_depth26_2027:exact_overlap \
  --eval-config data/eval/pattern_teacher_v0.eval \
  --out runs/pattern-training/table.tsv
```

In the example above, `labels` resolves to
`$VIBE_OTHELLO_DATASET_ROOT/teacher/ntest-depth26-2027/labels/merged.jsonl`.

Plain paths keep their existing meaning. Use plain `runs/...` paths for fresh
temporary outputs in the current worktree, and use `dataset:...` only for
reusable artifacts.

## Suggested Layout

```text
vibe-othello-datasets/
  teacher/
    ntest-depth26-2027/
      manifest.json
      positions/
        positions.jsonl
      labels/
        merged.jsonl
      exact-overlap/
        labels.jsonl
```

Manifests should record the source commands, git SHA, build type, seeds,
teacher engine name, adapter settings, legal validator, counts, and hashes of
the raw files. Keep local NTest paths and machine-specific absolute paths out
of committed docs.

## Current Script Support

- `tools/scripts/pattern_teacher_v0_train.py` supports `--dataset-root` for
  `--teacher-labels` and `--exact-labels` entries that start with `dataset:`.
- `tools/scripts/teacher_label_mistake_mining.py` supports `--dataset-root` for
  `--teacher-labels` and `--exact-labels` entries that start with `dataset:`.
- `tools/scripts/external_teacher_label_workflow.py` supports `--dataset-root`
  for `--positions` when it starts with `dataset:`.
- `tools/scripts/teacher_dataset_build.py` builds reusable position shards,
  deterministic splits, optional teacher labels, optional exact overlap labels,
  manifests, dataset cards, and QC summaries under a dataset root.

The C++ engine, exact solver, and search behavior do not depend on external
dataset roots. External engines remain optional local tooling and are not
required in CI.

## Teacher Dataset Builder

Use `teacher_dataset_build.py` when a dataset should survive across Codex
worktrees. It writes raw artifacts under the external dataset root, not under
source-controlled `data/`.

Tiny fake-engine smoke:

```sh
python3 tools/scripts/teacher_dataset_build.py \
  --dataset-id fake-smoke \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --positions data/positions/evaluation/diagnostic_suite.txt \
  --shard-size 4 \
  --teacher-engine-name fake \
  --legal-validator build/othello_validate_move \
  --allow-failures \
  --teacher-engine-cmd -- python3 tools/scripts/external_engines/fake_engine.py --move d3
```

The builder currently consumes board9 input files. For sampled position pools,
generate a local board9 file with the C++ `build/othello_position_sampler` tool
first, then pass that file with `--positions`; keep the generated file under
the dataset root or `runs/`, not source-controlled `data/`.

Real local teacher example, when an external engine is available locally:

```sh
python3 tools/scripts/teacher_dataset_build.py \
  --dataset-id ntest-depth26-2027 \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --positions data/positions/evaluation/diagnostic_suite.txt \
  --split-seed 20260601 \
  --split-ratios 70,15,15 \
  --shard-size 1000 \
  --teacher-adapter ntest \
  --teacher-protocol nboard \
  --teacher-depth 26 \
  --teacher-timeout-ms 60000 \
  --teacher-engine-name ntest26-local \
  --legal-validator build/othello_validate_move \
  --build-exact-overlap \
  --exact-label-dump build/othello_exact_label_dump \
  --exact-max-empties 14 \
  --include-move-scores \
  --teacher-engine-cmd -- /path/to/ntest/build/ntest x
```

The command above is an orchestration example only. Do not commit generated
positions, teacher labels, exact labels, logs, local external-engine paths, or
dataset manifests containing machine-specific paths.

Recommended full-run sampling target for a future 100k teacher dataset:

| empties | count |
| --- | ---: |
| 50 | 8000 |
| 42 | 8000 |
| 36 | 9000 |
| 30 | 10000 |
| 24 | 10000 |
| 20 | 10000 |
| 16 | 10000 |
| 12 | 12000 |
| 10 | 12000 |
| 8 | 11000 |

This distribution is guidance only. CI and PR validation should use tiny fake
engine fixtures and must not require NTest or any external dataset.
