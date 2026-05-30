# Exact TT Sizing Diagnostics on Adaptive16 Heavy Fixtures

Date: 2026-05-31

Base commit: `05d9102`

Build: Release

## Scope

This run checks whether the PR137 `adaptive16-heavy-*` fixtures are mainly
limited by exact transposition-table pressure. The exact solver semantics are
unchanged: `solve_exact_endgame()` still returns the true final disc margin and
this PR does not add forward pruning, MPC, LMR, ProbCut, or eval-based move
skipping.

The tooling change is diagnostic only: `othello_endgame_bench` can now pass an
optional exact TT entry count to the solver with `--exact-tt-entries N`. Omitting
the option preserves the existing root-empty-count based default.

## Commands

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --target othello_tests othello_endgame_bench -j 8
ctest --test-dir build/release --output-on-failure
```

Heavy fixture matrix:

```sh
for entries in default 524288 1048576 2097152; do
  out="build/release/exact-tt-heavy-${entries}.txt"
  : > "$out"
  for pos in \
    adaptive16-heavy-g12-ply44 \
    adaptive16-heavy-g27-ply44 \
    adaptive16-heavy-g27-ply45 \
    adaptive16-heavy-g12-ply45 \
    adaptive16-heavy-g25-ply45
  do
    if [ "$entries" = default ]; then
      ./build/release/othello_endgame_bench \
        --positions endgame \
        --breakdown-position "$pos" \
        --repetitions 1 \
        --root-breakdown \
        --expand-worst-candidate >> "$out"
    else
      ./build/release/othello_endgame_bench \
        --positions endgame \
        --breakdown-position "$pos" \
        --repetitions 1 \
        --root-breakdown \
        --expand-worst-candidate \
        --exact-tt-entries "$entries" >> "$out"
    fi
  done
done
```

Standard 14/16 sanity:

```sh
for entries in default 524288 2097152; do
  out="build/release/exact-tt-standard-14-16-${entries}.txt"
  if [ "$entries" = default ]; then
    ./build/release/othello_endgame_bench \
      --positions endgame \
      --empties 14,16 \
      --repetitions 1 > "$out"
  else
    ./build/release/othello_endgame_bench \
      --positions endgame \
      --empties 14,16 \
      --repetitions 1 \
      --exact-tt-entries "$entries" > "$out"
  fi
done
```

## Heavy Fixture Aggregate

All capacities returned the same best move and disc margin for all five heavy
fixtures.

| capacity | total ms | max ms | total nodes | TT hits | TT collisions | TT rejects | TT order used |
|---|---:|---:|---:|---:|---:|---:|---:|
| default | 1,303.332 | 349.002 | 10,448,419 | 233,767 | 21,244 | 1,953 | 61,979 |
| `1<<19` | 1,246.041 | 337.201 | 10,475,500 | 231,551 | 128,708 | 17,988 | 61,314 |
| `1<<20` | 1,254.638 | 342.576 | 10,448,419 | 233,767 | 21,244 | 1,953 | 61,979 |
| `1<<21` | 1,446.638 | 460.226 | 10,444,810 | 234,059 | 2,350 | 159 | 62,067 |

The current default for these `>12` empty roots is effectively `1<<20`, so the
default and explicit `1<<20` node/stat rows match. Wall-clock variance differs
slightly because each row is a fresh process run.

## Heavy Fixture Rows

| fixture | cap | best | margin | ms | nodes | TT hits | collisions | rejects | order used |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| `adaptive16-heavy-g12-ply44` | default | a2 | 10 | 349.002 | 2,805,785 | 67,341 | 14,401 | 1,416 | 12,424 |
| `adaptive16-heavy-g27-ply44` | default | h2 | -14 | 193.823 | 1,567,030 | 30,391 | 649 | 33 | 5,517 |
| `adaptive16-heavy-g27-ply45` | default | h2 | 16 | 222.643 | 1,716,640 | 44,399 | 963 | 53 | 16,210 |
| `adaptive16-heavy-g12-ply45` | default | b1 | -6 | 277.378 | 2,321,925 | 65,746 | 3,707 | 387 | 21,093 |
| `adaptive16-heavy-g25-ply45` | default | g5 | 32 | 260.486 | 2,037,039 | 25,890 | 1,524 | 64 | 6,735 |
| `adaptive16-heavy-g12-ply44` | `1<<19` | a2 | 10 | 337.201 | 2,823,605 | 65,873 | 75,546 | 12,669 | 12,098 |
| `adaptive16-heavy-g27-ply44` | `1<<19` | h2 | -14 | 180.090 | 1,567,591 | 30,349 | 5,787 | 364 | 5,508 |
| `adaptive16-heavy-g27-ply45` | `1<<19` | h2 | 16 | 217.746 | 1,717,974 | 44,264 | 8,597 | 707 | 16,128 |
| `adaptive16-heavy-g12-ply45` | `1<<19` | b1 | -6 | 282.339 | 2,328,367 | 65,235 | 26,358 | 3,543 | 20,862 |
| `adaptive16-heavy-g25-ply45` | `1<<19` | g5 | 32 | 228.665 | 2,037,963 | 25,830 | 12,420 | 705 | 6,718 |
| `adaptive16-heavy-g12-ply44` | `1<<21` | a2 | 10 | 460.226 | 2,803,353 | 67,551 | 1,685 | 114 | 12,486 |
| `adaptive16-heavy-g27-ply44` | `1<<21` | h2 | -14 | 204.486 | 1,566,962 | 30,397 | 53 | 1 | 5,517 |
| `adaptive16-heavy-g27-ply45` | `1<<21` | h2 | 16 | 246.575 | 1,716,533 | 44,409 | 99 | 6 | 16,216 |
| `adaptive16-heavy-g12-ply45` | `1<<21` | b1 | -6 | 289.277 | 2,321,035 | 65,806 | 388 | 31 | 21,113 |
| `adaptive16-heavy-g25-ply45` | `1<<21` | g5 | 32 | 246.074 | 2,036,927 | 25,896 | 125 | 7 | 6,735 |

## Root Breakdown Signal

The root-breakdown / expand-worst-candidate runs show mixed pressure:

| cap | fixture | root total ms | root total nodes | worst | worst ms | worst nodes | worst rank | worst best? |
|---|---|---:|---:|---|---:|---:|---:|---|
| default | `adaptive16-heavy-g12-ply44` | 2,948.260 | 20,857,696 | a2 | 490.228 | 3,508,419 | 1 | yes |
| default | `adaptive16-heavy-g27-ply44` | 1,774.478 | 14,170,789 | g7 | 472.871 | 3,897,313 | 4 | no |
| default | `adaptive16-heavy-g27-ply45` | 998.901 | 6,250,692 | h6 | 214.054 | 657,337 | 3 | no |
| `1<<19` | `adaptive16-heavy-g12-ply44` | 2,442.911 | 20,929,017 | a2 | 399.311 | 3,536,778 | 1 | yes |
| `1<<19` | `adaptive16-heavy-g27-ply44` | 1,657.196 | 14,210,810 | g7 | 457.828 | 3,921,078 | 4 | no |
| `1<<19` | `adaptive16-heavy-g27-ply45` | 752.761 | 6,250,752 | g5 | 99.053 | 852,971 | 5 | no |
| `1<<21` | `adaptive16-heavy-g12-ply44` | 3,111.281 | 20,848,663 | a2 | 611.029 | 3,504,642 | 1 | yes |
| `1<<21` | `adaptive16-heavy-g27-ply44` | 2,809.558 | 14,165,495 | g7 | 1,390.372 | 3,893,726 | 4 | no |
| `1<<21` | `adaptive16-heavy-g27-ply45` | 874.798 | 6,250,675 | g5 | 116.148 | 852,943 | 5 | no |

`1<<21` sharply reduces collisions and rejected stores, but it does not reduce
nodes and it worsens wall-clock tail in this run. That points away from a simple
"always use a larger exact TT" fix. The `1<<19` run had the lowest aggregate
time in this sample, but it did so while increasing collisions/rejects by a lot,
so the result is not a good basis for a constant-capacity policy.

## Standard 14/16 Sanity

Best move and margin matched default for all 23 selected 14/16 positions under
`1<<19` and `1<<21`.

| capacity | empties | count | total ms | avg ms | p95/max ms | avg nodes | p95/max nodes | collisions | rejects |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| default | 14 | 13 | 692.698 | 53.284 | 248.750 | 632,661 | 3,355,360 | 1,192 | 72 |
| default | 16 | 10 | 2,319.855 | 231.986 | 599.829 | 2,449,996 | 6,838,892 | 132,873 | 13,214 |
| `1<<19` | 14 | 13 | 801.245 | 61.634 | 321.029 | 633,075 | 3,359,923 | 10,936 | 886 |
| `1<<19` | 16 | 10 | 2,319.112 | 231.911 | 601.541 | 2,472,466 | 7,013,170 | 510,463 | 93,944 |
| `1<<21` | 14 | 13 | 873.921 | 67.225 | 301.133 | 632,610 | 3,354,760 | 114 | 3 |
| `1<<21` | 16 | 10 | 2,492.381 | 249.238 | 633.862 | 2,445,665 | 6,802,269 | 19,873 | 1,269 |

## Conclusion

The heavy fixtures do show TT pressure, especially `adaptive16-heavy-g12-ply44`
under default capacity, but larger capacity alone is not a clear win. `1<<21`
removes most collision/rejected-store counters, yet it makes the heavy max and
the 14/16 standard timing worse in this sample. The best next candidate is not a
global constant-capacity increase.

Recommended next steps:

- Keep the current default exact TT capacity.
- If we pursue capacity changes, make them adaptive/opt-in and gated by root
  empties plus measured pressure, not unconditional.
- Compare replacement policy before capacity. A bucket policy that preserves a
  useful lower-bound best-move hint may matter more than raw table size for the
  heavy roots.
- Keep using the `adaptive16-heavy-*` fixtures as regression probes whenever
  testing exact TT or ordering changes.

