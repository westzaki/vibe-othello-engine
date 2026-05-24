# Main Search Midgame Baseline

Status: template; numbers not collected.

## Environment

- Date: 2026-05-24
- Commit SHA: 82e7f88209e117ce73437395e1c0ce5499685d0a
- Machine: TODO
- OS: TODO
- Compiler: TODO
- Build type: Release
- Exact endgame threshold: 0

## Commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

```sh
./build/othello_search_bench \
  --mode both \
  --depths 3,4,5,6,7 \
  --positions suite \
  --repetitions 3 \
  --exact-endgame-threshold 0
```

```sh
./build/othello_search_bench \
  --mode iterative \
  --depths 5,6,7 \
  --positions suite \
  --repetitions 3 \
  --tt on \
  --pvs on \
  --exact-endgame-threshold 0 \
  --by-position
```

## Results

| depth | mode | tt | pvs | nodes | time ms | nps | tt hit % | PVS scouts | PVS researches | beta cut first move % |
| ---: | :--- | :---: | :---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO | TODO |

## Notes

- Use this file as the starting template for the first reliable local midgame
  baseline.
- Keep raw command output and experimental logs under `runs/`; that directory is
  intentionally gitignored.
- If best move, score, result checksum, or work checksum changes between runs,
  treat the comparison as behavioral first and performance-only second.
