# 2026-06-07 Rule Core Step 1 Baseline

Purpose: establish the current scalar rule-core baseline before changing board
kernels. This is measurement-infrastructure work only; evaluation, search, exact
solver behavior, and pattern learning were not changed.

## Build and Machine

- PR base commit: `87cc84b4be9afe44275704bdabc4dfa603bb480b`
- Measured commit: `4c30de1c8a2cc5aa49902631716e7cf20a787ab1`
- Scope: Step 1 measurement instrumentation only; rule kernel semantics were not changed
- Operation labels in the table use the post-review names; the measured raw rows
  had the same timings/checksums with less precise labels.
- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- Machine: macOS 15.7.3 arm64, Darwin 24.6.0
- Raw output: `runs/perf/2026-06-07-rule-core-step1.jsonl` (gitignored)

## Command

```sh
./build/othello_rule_core_bench \
  --positions suite \
  --iterations 100000 \
  --perft-depth 5 \
  --format jsonl
```

## Summary Rows

| operation | total calls | total elapsed ms | avg ns/call | checksum |
| --- | ---: | ---: | ---: | ---: |
| legal_moves | 1,200,000 | 15.4323 | 12.8603 | 4413538599536976906 |
| sp_legal_moves | 1,200,000 | 14.9745 | 12.4788 | 4413538599536976906 |
| legal_popcount | 1,200,000 | 14.0774 | 11.7312 | 7847810707887423853 |
| legal_move_list | 1,200,000 | 24.6804 | 20.5670 | 18121280966428229450 |
| flips_for_move | 76,800,000 | 339.461 | 4.42007 | 555462689072118337 |
| sp_flips_for_move | 76,800,000 | 242.256 | 3.15438 | 555462689072118337 |
| apply_move | 76,800,000 | 408.864 | 5.32375 | 5179957216623690840 |
| sp_flips_plus_position_after | 7,800,000 | 66.5606 | 8.53341 | 10863744931536494180 |
| pass_turn | 1,200,000 | 19.0498 | 15.8748 | 1552266616006747795 |
| is_game_over | 1,200,000 | 18.2915 | 15.2430 | 6027999293779342457 |
| disc_count | 2,400,000 | 3.39463 | 1.41443 | 14044470554203286158 |
| score | 2,400,000 | 3.39442 | 1.41434 | 10921786339960849197 |
| perft depth 5 | 312,983 | 7.23696 | 23.1225 | 7194759704887119983 |

## Perft Nodes

| position | depth | nodes |
| --- | ---: | ---: |
| initial | 5 | 1,396 |
| opening-after-d3 | 5 | 1,901 |
| midgame-normal | 5 | 17,062 |
| root-pass | 5 | 1 |
| terminal-all-black | 5 | 1 |
| opening-wide-mobility | 5 | 35,618 |
| midgame-lopsided-edge | 5 | 37,176 |
| midgame-pass-edge | 5 | 617 |
| 14-empty-high-mobility | 5 | 23,150 |
| 18-empty-corner-race | 5 | 73,036 |
| 20-empty-high-mobility-lite | 5 | 77,986 |
| 20-empty-edge-heavy-stress-lite | 5 | 45,039 |

## Verification

```sh
cmake --build build
./build/othello_tests "[rule-core]"
ctest --test-dir build --output-on-failure
```

Results:

- `[rule-core]`: 21 test cases, 908,020 assertions passed
- `ctest`: 337/337 tests passed
