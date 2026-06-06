# Exact endgame TT pressure sizing

Date: 2026-06-06

Baseline: `origin/main` at `b570c22`

Hypothesis: 18/20-empty exact endgame searches are losing useful transposition
data to bucket pressure. Moving the exact-only TT from 4-way buckets at `1<<20`
entries to 8-way buckets at `1<<21` entries should reduce collisions and rejected
stores without changing exact best moves or disc margins.

The exact solver still bypasses the TT for the last 0-4 empties through the
specialized tail solver.

## Commands

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j 8
ctest --test-dir build/release --output-on-failure

./build/release/othello_endgame_bench \
  --positions endgame --empties 14,16,18,20 --format jsonl

./build/release/othello_endgame_bench \
  --positions endgame --empties 14,16,18,20 \
  --exact-tt-entries 0 --format jsonl

./build/release/othello_endgame_bench \
  --positions endgame --empties 14,16,18,20 \
  --exact-tt-entries 2097152 --format jsonl

./build/release/othello_endgame_bench \
  --positions endgame --empties 14,16,18,20 \
  --exact-tt-entries 4194304 --format jsonl

./build/release/othello_endgame_bench \
  --breakdown-position 20-empty-high-mobility-lite \
  --root-breakdown --format jsonl
```

Note: this local `ctest` run needed `build/release/data -> ../../data` so tools
run from the build directory could resolve `data/eval/current_default.eval`.

## 18/20-empty summary

| build / exact TT | bucket | entries | memory | time ms | nodes | hit rate | collisions | rejected stores |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| baseline default | 4-way | `1<<20` | 16 MiB | 23,244.599 | 337,524,877 | 9.322% | 8,545,100 | 3,788,460 |
| head default | 8-way | `1<<21` | 32 MiB | 21,764.905 | 329,035,460 | 10.357% | 3,591,627 | 771,521 |
| head max diagnostic | 8-way | `1<<22` | 64 MiB | 24,706.358 | 327,311,444 | 10.613% | 945,662 | 74,681 |
| head TT off | off | 0 | 0 MiB | 67,487.528 | 1,131,895,612 | 0% | 0 | 0 |

`1<<21` was chosen as the default: compared with baseline it reduced nodes by
2.5%, collisions by 58.0%, and rejected stores by 79.6%. The final new-default
run was 6.4% faster than the two-sample baseline average for 18/20-empty
positions, while `1<<22` reduced pressure further but cost another 32 MiB and
was slower in this local run.

## Capacity and bucket-width comparison

| build | bucket | entries | 18/20 time ms | 18/20 collisions | 18/20 rejected stores |
|---|---:|---:|---:|---:|---:|
| baseline | 4-way | `1<<20` | 23,866.170 | 8,545,100 | 3,788,460 |
| baseline | 4-way | `1<<21` | 24,528.308 | 4,273,095 | 1,174,337 |
| baseline | 4-way | `1<<22` | 24,250.717 | 1,410,865 | 235,047 |
| head | 8-way | `1<<20` | 24,478.279 | 8,671,355 | 3,005,949 |
| head | 8-way | `1<<21` | 23,987.867 | 3,591,627 | 771,521 |
| head | 8-way | `1<<22` | 24,706.358 | 945,662 | 74,681 |

At equal `1<<21` capacity, 8-way buckets reduced collisions by 16.0% and
rejected stores by 34.3% versus 4-way buckets.

## Exact-result check

Best move and disc margin matched across baseline default, head default,
head `--exact-tt-entries 0`, head `--exact-tt-entries 2097152`, and head
`--exact-tt-entries 4194304`.

Result checksum over `(position, empties, best_move, disc_margin)`:

```text
58a8fad5a13f8da2d48557777efe1824f78af6360a4903646ab6c9c82113b309
```

Principal variations can differ when the TT is disabled because move-ordering
hints are absent; exact best moves and final disc margins did not change.

## Root breakdown

`20-empty-high-mobility-lite`, normal root solve:

| build | bucket | entries | time ms | nodes | hit rate | collisions | rejected stores |
|---|---:|---:|---:|---:|---:|---:|---:|
| baseline | 4-way | `1<<20` | 4,368.270 | 57,361,982 | 6.504% | 1,964,473 | 2,136,610 |
| head | 8-way | `1<<21` | 3,855.900 | 53,588,976 | 8.891% | 2,049,164 | 710,528 |

Root-candidate aggregate for the same position:

| build | bucket | entries | time ms | nodes | hit rate | collisions | rejected stores |
|---|---:|---:|---:|---:|---:|---:|---:|
| baseline | 4-way | `1<<20` | 11,046.645 | 147,183,239 | 9.346% | 3,716,659 | 1,276,898 |
| head | 8-way | `1<<21` | 10,585.084 | 144,168,321 | 10.146% | 1,130,191 | 82,475 |
