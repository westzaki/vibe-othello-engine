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

The C++ engine, exact solver, and search behavior do not depend on external
dataset roots. External engines remain optional local tooling and are not
required in CI.
