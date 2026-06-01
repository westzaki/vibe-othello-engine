# 2026-06-01 NTest Depth-26 Dataset Migration

Status: completed dataset migration report, not current evaluator guidance.

This report records the migration of the existing 2027-row NTest depth-26
teacher artifacts from worktree-local `runs/classic-pattern-v0/` storage into
the reusable external dataset-root layout. It is a reproducibility and workflow
report only. It does not train a new evaluator, does not promote the default,
does not add a C++ preset, and does not change search or exact solver
semantics.

## Decision

Migration succeeded.

The 2027-row NTest teacher split and exact-overlap artifacts were imported
under the external dataset root at `teacher/ntest-depth26-2027/`. The local
dataset root path is intentionally not committed; this worktree can resolve it
through ignored `config/datasets.local.toml`, and other worktrees can use the
same root through `VIBE_OTHELLO_DATASET_ROOT` or their own ignored local config.

Raw teacher labels, exact labels, positions, match JSONL, benchmark logs, local
external-engine paths, and local absolute dataset-root paths were not committed.

## Metadata

- import git SHA: `615210d05dfe98270671c4ad1c0f57713beb1a40`
- dataset id: `ntest-depth26-2027`
- imported dataset path below root: `teacher/ntest-depth26-2027/`
- dataset root source: local external dataset root, path redacted
- local resolver setup: ignored `config/datasets.local.toml`, not committed
- created at: `2026-06-01T10:17:50Z`
- source report: `docs/experiments/evaluation/2026-06-01-phase-broad-v0.md`
- raw outputs committed: no
- generated teacher labels committed: no
- generated exact labels committed: no
- generated positions committed: no
- local external-engine paths committed: no
- default promotion: no
- C++ preset: no

## Imported Layout

The reusable artifact now exists under the configured dataset root:

```text
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

Created external metadata:

- `manifest.json`
- `dataset_card.md`
- `labels/ntest26/manifest.json`
- `exact-overlap/manifest.json`
- `splits/train.ids`
- `splits/validation.ids`
- `splits/holdout.ids`
- `qc/summary.json`
- `qc/source_hashes.tsv`
- `qc/split_counts.tsv`

## Imported Artifacts

| Dataset reference | Rows | sha256 |
| --- | ---: | --- |
| `dataset:teacher/ntest-depth26-2027/labels/ntest26/train.jsonl` | 1234 | `88c3b16016f267152204cb023f15c8b93ec1a5406761480429492bbd9fe6a286` |
| `dataset:teacher/ntest-depth26-2027/labels/ntest26/validation.jsonl` | 384 | `8929879f49033b83a41827507c1e900a3d6465da194122342aecdd4499b2907a` |
| `dataset:teacher/ntest-depth26-2027/labels/ntest26/holdout.jsonl` | 409 | `c154615a2cb4cb06effbc0bcec7d9eaef47b2039a9180da05db107b694234099` |
| `dataset:teacher/ntest-depth26-2027/exact-overlap/teacher2000_max12_labels.jsonl` | 602 | `df443d5480ef1f038e080474c9d19c3b8d551eed7831d1d8b6363840538e17b6` |
| `dataset:teacher/ntest-depth26-2027/exact-overlap/teacher_extra30_max12_labels.jsonl` | 10 | `6f0b43ab987bd01dad261676844ec666fadf9f4f5479e659f5e1d8d453187d94` |
| `dataset:teacher/ntest-depth26-2027/exact-overlap/train_labels.jsonl` | 96 | `d0f6933f1dab0282c5c2d163ab6c226d0abdda1d7174d53c4235ff2b73468a5c` |
| `dataset:teacher/ntest-depth26-2027/exact-overlap/heldout_labels.jsonl` | 96 | `850145595ea9fb0d2200e507cd0a2a4e4144e088b6027f4e1e4ba35c41d2fd80` |

Split counts:

| Split | Rows |
| --- | ---: |
| train | 1234 |
| validation | 384 |
| holdout | 409 |
| total teacher rows | 2027 |

Exact overlap counts:

| Exact file | Rows |
| --- | ---: |
| teacher2000 max12 | 602 |
| teacher extra30 max12 | 10 |
| exact train | 96 |
| exact heldout | 96 |

All imported JSONL files parsed successfully after copy.

## Catalog References

`config/datasets.example.toml` now documents catalog fields for the imported
layout. With a local config that points `[datasets].root` to the external
dataset root, these references are usable:

```text
dataset:teacher.ntest_depth26_2027:train
dataset:teacher.ntest_depth26_2027:validation
dataset:teacher.ntest_depth26_2027:holdout
dataset:teacher.ntest_depth26_2027:exact_teacher2000
dataset:teacher.ntest_depth26_2027:exact_extra30
dataset:teacher.ntest_depth26_2027:exact_train
dataset:teacher.ntest_depth26_2027:exact_heldout
dataset:teacher.ntest_depth26_2027:qc_summary
```

The plain relative `dataset:` references above work with either
`VIBE_OTHELLO_DATASET_ROOT` or ignored `config/datasets.local.toml`.

Example phase-aware training input shape:

```sh
python3 tools/scripts/phase_pattern_table_train.py \
  --teacher-labels dataset:teacher.ntest_depth26_2027:train \
  --exact-labels dataset:teacher.ntest_depth26_2027:exact_teacher2000 \
  --eval-config data/eval/pattern_reboot_v0.eval \
  --analyze-position build/othello_analyze_position \
  --out-dir runs/pattern-training/phase-broad-v0 \
  --table-name phase_broad_v0 \
  --families broad_all \
  --update-mode rank \
  --split all \
  --depth 1
```

## Commands Run

Dataset root discovery and source validation:

```sh
printenv VIBE_OTHELLO_DATASET_ROOT
ls -la config
wc -l runs/classic-pattern-v0/splits-2027/teacher_train.jsonl \
  runs/classic-pattern-v0/splits-2027/teacher_validation.jsonl \
  runs/classic-pattern-v0/splits-2027/teacher_holdout.jsonl \
  runs/classic-pattern-v0/exact/teacher2000_max12_labels.jsonl \
  runs/classic-pattern-v0/exact/teacher_extra30_max12_labels.jsonl \
  runs/classic-pattern-v0/exact/train_labels.jsonl \
  runs/classic-pattern-v0/exact/heldout_labels.jsonl
shasum -a 256 runs/classic-pattern-v0/splits-2027/teacher_train.jsonl \
  runs/classic-pattern-v0/splits-2027/teacher_validation.jsonl \
  runs/classic-pattern-v0/splits-2027/teacher_holdout.jsonl \
  runs/classic-pattern-v0/exact/teacher2000_max12_labels.jsonl \
  runs/classic-pattern-v0/exact/teacher_extra30_max12_labels.jsonl \
  runs/classic-pattern-v0/exact/train_labels.jsonl \
  runs/classic-pattern-v0/exact/heldout_labels.jsonl
```

Import command shape, with the local external root path redacted:

```sh
DATASET_ROOT=<redacted-external-dataset-root> python3 <one-off-importer>
```

The importer copied the source JSONL files into
`$DATASET_ROOT/teacher/ntest-depth26-2027/`, parsed every JSONL row, computed
sha256 for every imported file, wrote split ID files, and created manifests,
dataset card, and QC summaries.

Dataset reference smoke:

```sh
python3 - <<'PY'
from pathlib import Path
import sys

sys.path.insert(0, str(Path("tools/scripts").resolve()))
from dataset_paths import resolve_dataset_reference

refs = [
    "dataset:teacher.ntest_depth26_2027:train",
    "dataset:teacher.ntest_depth26_2027:validation",
    "dataset:teacher.ntest_depth26_2027:holdout",
    "dataset:teacher.ntest_depth26_2027:exact_teacher2000",
    "dataset:teacher.ntest_depth26_2027:exact_train",
    "dataset:teacher.ntest_depth26_2027:exact_heldout",
]
for ref in refs:
    resolved = resolve_dataset_reference(ref, require_root_exists=True)
    assert resolved.is_file(), ref
PY
```

## Validation

- Source artifacts: present
- Source hashes: recorded above
- Source row counts: recorded above
- Source JSONL parse validation: passed
- Imported raw dataset: created under external dataset root
- Imported JSONL parse validation: passed
- Dataset manifests/cards/QC files: created under external dataset root
- Dataset catalog references: resolved through ignored local config
- Raw data committed: no

Repository validation:

```sh
python3 -m py_compile tools/scripts/*.py tools/scripts/tests/*.py
python3 -m unittest discover tools/scripts/tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

All repository validation commands above passed.

## Next Step

Use the reusable 2027-row NTest teacher dataset for the next trainer objective /
regularization PR. Do not treat this migration as evaluator strength evidence,
and do not start more pattern-family expansion from this workflow PR.
