# Language benchmarks

Every new project includes a benchmark executable, `<id>_bench`. It
measures how long your language takes to build a document and to handle
editor requests.

## Run the benchmark

Build in Release mode, then pass one or more documents:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mylang_bench --parallel
./build/src/mylang_bench example/hello.mylang
```

Use large, realistic documents to get meaningful times. `--help` lists the
options, such as `--iterations N`, `--no-lsp` to skip editor requests, and
`--json` or `--csv` to save the results.

## Read the output

For each document, the benchmark prints three blocks:

- **Pipeline**: the best time of each build phase, in milliseconds, and its
  speed in MiB/s. `parse+index` parses the text and indexes the exported
  names, then `scopes`, `link` and `validate` run. `build` is the whole
  document build. Each phase shows its own best run, so the phases do not
  always add up to `build`.
- **Document**: the resolved references and the diagnostics the build
  produced. Diagnostics are part of the measured work.
- **LSP providers**: the time of each editor request on the validated
  document: minimum, average and maximum over the repeated runs.

## Framework results

These are document builds for the four example languages on generated 4 MiB
documents, in milliseconds. They were measured on 29 October 2026 with the LR
parser, GCC 16.2 in Release mode and an Intel Core i9-13900K.

| Language | Parse + index | Scopes | Link | Validation | Build | MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Arithmetics | 31.093 | 2.997 | 11.129 | 29.869 | 76.064 | 52.6 |
| DomainModel | 8.923 | 3.423 | 5.244 | 3.994 | 21.708 | 184.3 |
| Requirements | 6.671 | 1.824 | 4.375 | 7.374 | 20.260 | 197.4 |
| Statemachine | 6.418 | 1.173 | 7.348 | 4.980 | 20.029 | 199.7 |

DomainModel, Requirements and Statemachine build a 4 MiB document in about
20 ms, and parse it at 450 to 620 MiB/s. Arithmetics does more work per byte:
its documents are dense expressions, and its validator evaluates them.

On these documents, hover, go-to-definition and find-references take 1 to
5 ms on average.

Workspaces of 16 MiB build in parallel. With 31 worker threads, 1,024 small
files build in under 15 ms, 6 to 7 times faster than on a single thread, and
four large files build 2.4 to 3.1 times faster.

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
counts to measure on workspaces; `--no-multi` skips the workspace
measurements.
