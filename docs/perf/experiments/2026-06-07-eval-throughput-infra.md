# Evaluation Throughput Pure Infrastructure Attempts

Date: 2026-06-07

Base: `afe341a tune: improve late evaluation quality (#290)`

Goal: improve evaluation throughput without changing evaluation values, feature
weights, pattern tables, or search semantics.

Outcome: no code change was retained. All measured code attempts preserved
search identity fields, but elapsed time did not improve for both required
evaluator configs.

## Benchmark Profile

Build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Search bench profile:

```sh
othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions suite \
  --repetitions 5 \
  --tt on \
  --pvs on \
  --exact-endgame-threshold 0 \
  --eval-config <config> \
  --format jsonl
```

Configs:

- `data/eval/current_default.eval`
- `data/eval/ntest_pairwise_full_v2.eval`

Compared identity fields:

- `result_checksum`
- `work_checksum`
- `best_move`
- `score`
- `principal_variation`
- `eval_calls`
- `nodes`

## Base Timings

| config | depth | elapsed_ms | nodes | eval_calls | nps |
| --- | ---: | ---: | ---: | ---: | ---: |
| current_default | 5 | 219.418 | 195582 | 177960 | 891291 |
| current_default | 6 | 289.703 | 513480 | 469785 | 1772390 |
| current_default | 7 | 655.563 | 1704103 | 1552835 | 2599500 |
| ntest_pairwise_full_v2 | 5 | 200.560 | 196314 | 178665 | 978934 |
| ntest_pairwise_full_v2 | 6 | 382.835 | 503040 | 459110 | 1313710 |
| ntest_pairwise_full_v2 | 7 | 608.749 | 1736466 | 1585610 | 2854300 |

## Attempt: Score-Only Accumulator Split Plus Direct Pattern Lookup

Changed:

- split score-only accumulation from breakdown accumulation
- removed score-only function pointer dispatch through `accumulate_feature`
- resolved phase weights and phase pattern table pointer together
- used direct internal pattern table access for valid generated indexes

Identity: all compared identity fields matched base for both configs and all
depths.

| config | base_total_elapsed_ms | head_total_elapsed_ms | delta |
| --- | ---: | ---: | ---: |
| current_default | 1164.684 | 1324.452 | +13.72% |
| ntest_pairwise_full_v2 | 1192.144 | 1417.742 | +18.92% |

Decision: rejected. The change kept semantics stable, but slowed both required
configs.

## Attempt: Direct Internal Pattern Lookup Only

Changed:

- retained existing `evaluation.cpp`
- used direct table access in internal score path for generated valid indexes

Identity: `current_default` `result_checksum` matched base at all measured
depths.

| config | base_total_elapsed_ms | head_total_elapsed_ms | delta |
| --- | ---: | ---: | ---: |
| current_default | 1164.684 | 1621.493 | +39.22% |

Decision: rejected before running the second config. The first required config
already failed the elapsed improvement condition.

## Attempt: Precomputed Pattern Cells

Changed:

- retained existing `evaluation.cpp`
- added constexpr precomputed pattern cells with square bit and ternary place
  contribution
- used those cells in pattern table scoring

Identity: `current_default` `result_checksum` matched base at all measured
depths.

| config | base_total_elapsed_ms | head_total_elapsed_ms | delta |
| --- | ---: | ---: | ---: |
| current_default | 1164.684 | 1575.515 | +35.27% |

Decision: rejected before running the second config. The first required config
already failed the elapsed improvement condition.

## Notes

The existing score-only path already skips zero-weight features for search-time
evaluation. The investigated rewrites were semantically stable but increased
runtime on this machine/profile, likely from code shape, register pressure, or
lost optimizer choices around the existing compact loop structure.

No evaluator, weight, pattern table, or search behavior change should be merged
from this experiment.
