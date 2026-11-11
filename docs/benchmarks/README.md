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
`--help` lists the options, such as `--iterations N`, `--no-lsp` to skip
editor requests, and `--json` or `--csv` to save the results.

## Read the output

For each document, the benchmark prints:

- **Pipeline**: the best time of each build phase, in milliseconds, and its
  speed in MiB/s. `parse+index` parses the text and indexes the exported
  names, then `scopes`, `link` and `validate` run. `build` is the whole
  document build. Each phase shows its own best run, so the phases do not
  always add up to `build`.
- **Document**: the resolved references and the diagnostics the build
  produced. Diagnostics are part of the measured work.
- **LSP providers**: the time of each editor request on the validated
  document: minimum, average and maximum over the repeated runs.

On Linux, when the processor's counters can be read, the pipeline also shows
cycles and instructions per byte, and a **Hardware** block gives the counters
of the whole build.

## Framework results

`language_bench` runs the same measurements on the four example languages,
with documents it generates. The results below were obtained with:

- **Parser**: LR
- **Compiler**: GCC 16.2, Release mode
- **Processor**: Intel Core i9-13900K

### One document

Best time of each phase on a 4 MiB document, in milliseconds:

| Language | Parse + index | Scopes | Link | Validation | Build | MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Arithmetics | 34.6 | 3.4 | 4.7 | 26.2 | 69.0 | 58 |
| DomainModel | 9.1 | 2.9 | 1.8 | 2.5 | 16.3 | 245 |
| Requirements | 7.7 | 1.6 | 1.6 | 9.1 | 20.3 | 197 |
| Statemachine | 7.3 | 1.2 | 2.2 | 4.9 | 15.8 | 253 |

DomainModel, Requirements and Statemachine build a 4 MiB document in 16 to
20 ms, and parse it at 440 to 550 MiB/s. Arithmetics does more work per byte:
its documents are dense expressions, and its validator evaluates them.

The build time follows the size of the document:

| Language | 16 KiB | 256 KiB | 4 MiB |
| --- | ---: | ---: | ---: |
| Arithmetics | 0.32 | 3.68 | 69.0 |
| DomainModel | 0.06 | 1.07 | 16.3 |
| Requirements | 0.06 | 0.97 | 20.3 |
| Statemachine | 0.06 | 0.99 | 15.8 |

### Editor requests

On the 4 MiB documents, hover, go-to-definition and find-references take
less than 0.3 ms on average, and completion less than 3 ms. Requests that
read the whole document take longer: 5 to 19 ms for document symbols and
13 to 33 ms for semantic tokens.

### Workspaces

The documents of a workspace build in parallel. These are the build times of
a 16 MiB workspace, in milliseconds, for DomainModel, Requirements and
Statemachine:

| Workspace | One thread | 31 worker threads | Times faster |
| --- | ---: | ---: | ---: |
| 1,024 small files | 78 to 85 | 12 to 14 | 6.2 to 6.6 |
| 16 medium files | 83 to 85 | 18 to 26 | 3.1 to 4.5 |
| 4 large files | 87 to 98 | 25 to 40 | 2.3 to 3.4 |

## Reproduce these measurements

From the Cursiv repository root:

```sh
cmake --preset gcc-release -DCURSIV_DEFAULT_ENGINE=LR -DCURSIV_MIMALLOC=ON
cmake --build build --target language_bench --parallel
./build/bench/language_bench \
  --sizes 16K,256K,4M --iterations 5 \
  --multi-bytes 16M --workers 0,1,2,4,8,31
```

`language_bench` generates its own documents. `--workers` lists the worker
counts to measure on workspaces, where `0` builds on one thread; `--no-multi`
skips the workspace measurements.
