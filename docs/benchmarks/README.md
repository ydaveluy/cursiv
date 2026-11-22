# Language benchmarks

Every new project includes a benchmark executable, `<id>_bench`. It measures
how long your language takes to build a document and to answer editor
requests.

## Run the benchmark

Build in Release mode, then pass one or more documents:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mylang_bench --parallel
./build/src/mylang_bench example/hello.mylang
```

Use large, realistic documents: small ones finish too fast to compare.
Each document is first built for 100 ms without timing, so that the measured
builds run warm, and `--iterations 5` repeats each measurement five times.
`--pause 200` waits 200 ms before each measured build, as between two edits in
an editor: the times then show the latency of one edit rather than the speed
of back-to-back builds. Use `--no-lsp` to skip editor requests, or
`--json bench.json` or `--csv bench.csv` to save the results. Run `--help` for
the full option list.

## Read the output

For each document, the benchmark prints:

- **Pipeline**: the time of each build phase, in milliseconds, and its speed
  in MiB/s, all taken from the fastest build, or from the median build with
  `--pause`. `parse+index` parses the text and indexes the exported names,
  then `scopes`, `link` and `validate` run. `build` is the whole document
  build.
- **Document**: the resolved references and the diagnostics the build
  produced. Diagnostics are part of the measured work.
- **LSP providers**: the time of each editor request on the validated
  document: minimum, average and maximum over the repeated runs.

On Linux, when the processor's counters can be read, the pipeline also shows
cycles and instructions per byte, and a **Hardware** block gives the counters
of the whole build.

## Framework results

Measured with GCC 16.2.1 in Release mode, mimalloc enabled, on an Intel Core
i9-13900K. The benchmark generated the documents and ran each case five times.

### One document

Phase times of the fastest build of a 4 MiB document, in milliseconds. Cycles
per byte cover parsing and indexing. Throughput covers the whole build.

| Language | Parse + index | Cycles/byte | Scopes | Link | Validation | Build | MiB/s |
|---|---:|---:|---:|---:|---:|---:|---:|
| Arithmetics | 31.9 | 35.62 | 3.2 | 4.5 | 25.3 | 64.9 | 62 |
| DomainModel | 5.8 | 8.76 | 3.0 | 1.8 | 3.1 | 13.7 | 291 |
| Requirements | 5.1 | 6.85 | 1.7 | 1.6 | 3.0 | 11.4 | 350 |
| Statemachine | 4.8 | 6.31 | 1.2 | 2.1 | 2.7 | 10.7 | 372 |

These builds produced 4,984 diagnostics for Arithmetics, none for DomainModel,
70,067 for Requirements and 14,528 for Statemachine.

Best whole-document build time in milliseconds.

| Language | 16 KiB | 256 KiB | 4 MiB |
|---|---:|---:|---:|
| Arithmetics | 0.24 | 3.36 | 64.93 |
| DomainModel | 0.06 | 0.77 | 13.73 |
| Requirements | 0.05 | 0.61 | 11.42 |
| Statemachine | 0.05 | 0.65 | 10.75 |

### Editor requests

Average time per request on a validated 4 MiB document, in milliseconds.
Calls go directly to the provider. Hover, definition, references and completion
sample several cursor positions.

| Language | Hover | Definition | References | Completion | Document symbols | Semantic tokens (full) |
|---|---:|---:|---:|---:|---:|---:|
| Arithmetics | 0.118 | 0.076 | 0.141 | 11.248 | 6.755 | 35.804 |
| DomainModel | 0.022 | 0.017 | 0.183 | 1.363 | 17.363 | 20.487 |
| Requirements | <0.001 | <0.001 | 0.042 | 1.212 | 8.359 | 15.477 |
| Statemachine | 0.044 | 0.028 | 0.045 | 1.386 | 5.207 | 16.641 |

### Workspaces

Best build time in milliseconds for a 16 MiB workspace budget. Each cell gives
one thread → 31 worker threads plus the calling thread, followed by the speedup.
Requirements resolves references across files. DomainModel and Statemachine
resolve them within each file.

| Workspace | DomainModel | Requirements | Statemachine |
|---|---:|---:|---:|
| 1,024 small files | 70.4 → 12.3 (5.72×) | 76.9 → 13.5 (5.69×) | 69.8 → 11.8 (5.90×) |
| 16 medium files | 67.5 → 17.1 (3.94×) | 70.6 → 19.1 (3.69×) | 69.9 → 17.8 (3.92×) |
| 4 large files | 69.6 → 22.4 (3.11×) | 70.8 → 25.2 (2.82×) | 78.8 → 24.6 (3.20×) |
| 822 mixed-size files | 68.6 → 13.8 (4.96×) | 73.6 → 16.4 (4.48×) | 68.6 → 13.1 (5.26×) |

## Reproduce these measurements

From the Cursiv repository root:

```sh
cmake --preset gcc-release -DCURSIV_MIMALLOC=ON
cmake --build build --target language_bench --parallel
./build/bench/language_bench \
  --sizes 16K,256K,4M --iterations 5 \
  --multi-bytes 16M --workers 0,1,2,4,8,31
```

`language_bench` generates its own documents. `--workers` lists the worker
counts to measure on workspaces, where `0` builds on one thread; `--no-multi`
skips the workspace measurements.
