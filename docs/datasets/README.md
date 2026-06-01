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
  --teacher-labels dataset:teacher/ntest-depth26-2027/labels/ntest26/train.jsonl \
  --eval-config data/eval/pattern_teacher_v0.eval \
  --out runs/pattern-training/table.tsv
```

Catalog references use the local TOML entry shape:

```sh
python3 tools/scripts/pattern_teacher_v0_train.py \
  --teacher-labels dataset:teacher.ntest_depth26_2027:train \
  --exact-labels dataset:teacher.ntest_depth26_2027:exact_teacher2000 \
  --eval-config data/eval/pattern_teacher_v0.eval \
  --out runs/pattern-training/table.tsv
```

In the example above, `train` resolves to
`$VIBE_OTHELLO_DATASET_ROOT/teacher/ntest-depth26-2027/labels/ntest26/train.jsonl`.

Plain paths keep their existing meaning. Use plain `runs/...` paths for fresh
temporary outputs in the current worktree, and use `dataset:...` only for
reusable artifacts.

## Suggested Layout

```text
vibe-othello-datasets/
  teacher/
    ntest-depth26-2027/
      manifest.json
      dataset_card.md
      labels/
        ntest26/
          train.jsonl
          validation.jsonl
          holdout.jsonl
          manifest.json
      exact-overlap/
        teacher2000_max12_labels.jsonl
        teacher_extra30_max12_labels.jsonl
        train_labels.jsonl
        heldout_labels.jsonl
        manifest.json
      splits/
        train.ids
        validation.ids
        holdout.ids
      qc/
        summary.json
        source_hashes.tsv
        split_counts.tsv
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
- `tools/scripts/balanced_position_pool.py` builds balanced board9 position
  pools with the C++ position sampler and writes pool manifests/QC under
  `runs/` or an explicit external dataset root.
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

## Balanced Position Pool

Use `balanced_position_pool.py` to create a reusable board9 source file before
running a teacher dataset build. The script orchestrates
`build/othello_position_sampler`; it does not implement Othello rules in
Python and it does not require NTest.

Quick local smoke, about 1.2k positions:

```sh
python3 tools/scripts/balanced_position_pool.py \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --output teacher/ntest-depth12-300k/source/smoke-positions.txt \
  --bucket-spec smoke \
  --seed 20260601 \
  --sampler build/othello_position_sampler \
  --max-attempts-per-bucket 500000
```

Full 300k pool for the first depth-12 NTest teacher run:

```sh
python3 tools/scripts/balanced_position_pool.py \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --output teacher/ntest-depth12-300k/source/positions.txt \
  --bucket-spec default-300k \
  --seed 20260601 \
  --sampler build/othello_position_sampler \
  --max-attempts-per-bucket 20000000
```

The `default-300k` bucket spec is:

| empties | count |
| --- | ---: |
| 52 | 4000 |
| 51 | 4000 |
| 48 | 5000 |
| 47 | 5000 |
| 44 | 7000 |
| 43 | 7000 |
| 40 | 10000 |
| 39 | 10000 |
| 36 | 14000 |
| 35 | 14000 |
| 32 | 17000 |
| 31 | 17000 |
| 28 | 18000 |
| 27 | 18000 |
| 24 | 18000 |
| 23 | 18000 |
| 20 | 16000 |
| 19 | 16000 |
| 16 | 14000 |
| 15 | 14000 |
| 14 | 11000 |
| 13 | 11000 |
| 12 | 9000 |
| 11 | 9000 |
| 10 | 4500 |
| 9 | 4500 |
| 8 | 2500 |
| 7 | 2500 |

The generated `positions.txt`, `manifest.json`, `qc/`, and `commands.sh` are
local artifacts. Do not commit generated position pools.

Real local teacher example, when an external engine is available locally:

```sh
python3 tools/scripts/teacher_dataset_build.py \
  --dataset-id ntest-depth12-smoke \
  --dataset-root "$VIBE_OTHELLO_DATASET_ROOT" \
  --positions dataset:teacher/ntest-depth12-300k/source/smoke-positions.txt \
  --split-seed 20260601 \
  --split-ratios 70,15,15 \
  --shard-size 1000 \
  --teacher-adapter ntest \
  --teacher-protocol nboard \
  --teacher-depth 12 \
  --teacher-timeout-ms 60000 \
  --label-jobs 4 \
  --position-log-mode failures \
  --teacher-workdir /path/to/ntest \
  --teacher-env NTEST_HOME=/path/to/ntest \
  --teacher-engine-name ntest-depth12-local \
  --legal-validator build/othello_validate_move \
  --build-exact-overlap \
  --exact-label-dump build/othello_exact_label_dump \
  --exact-max-empties 14 \
  --include-move-scores \
  --teacher-engine-cmd -- ./ntest x
```

Always pass `--teacher-depth` explicitly for NTest NBoard runs. The legacy
adapter default is depth 26 when depth is omitted, which may be too deep for a
first 300k dataset. Run smoke builds at depth 10, 12, and 14 before starting a
full overnight run, then choose the full-run depth intentionally.

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
