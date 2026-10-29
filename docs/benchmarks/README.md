# Language benchmarks

Every scaffolded project includes a benchmark executable, `<id>_bench`. It
measures how long your language takes to build a document and to answer
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

- **Pipeline**: the best time of each build phase, in milliseconds, with its
  throughput in MiB/s. `parse+index` parses the text and indexes exported
  names, then `scopes`, `link` and `validate` run. `build` is the whole
  document build. Each phase keeps its own best run, so the phases need not
  add up to `build`.
- **Document**: the resolved references and the diagnostics the build
  produced. Diagnostics are part of the measured work.
- **LSP providers**: the time of each editor request on the validated
  document, as minimum, average and maximum when repeated.

## Framework results

These are document builds for the four example languages on generated 4 MiB
documents, in milliseconds. They were measured on 25 October 2026 with the LR
parser, GCC 16.2 in Release mode and an Intel Core i9-13900K.

| Language | Parse + index | Scopes | Link | Validation | Build | MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Arithmetics | 29.260 | 2.588 | 12.356 | 29.853 | 74.057 | 54.0 |
| DomainModel | 9.029 | 3.806 | 5.596 | 4.184 | 22.901 | 174.7 |
| Requirements | 6.432 | 1.852 | 4.335 | 9.213 | 21.888 | 182.7 |
| Statemachine | 6.525 | 1.204 | 7.892 | 5.117 | 20.804 | 192.3 |

Workspaces of 16 MiB build in parallel. With 31 workers, 1,024 small files
build about 6 to 8 times faster than with none, and four large files about
2.5 times faster.

## Reproduce these measurements

From the Cursiv repository root:

```sh
cmake --preset gcc-release -DCURSIV_DEFAULT_ENGINE=LR -DCURSIV_MIMALLOC=ON
cmake --build build --target language_bench -j12
./build/bench/language_bench \
  --sizes 16K,256K,4M --iterations 5 \
  --multi-bytes 16M --workers 0,1,2,4,8,31
```

`language_bench` generates its own documents. `--workers` sets the worker
counts of the workspace measurements; `--no-multi` skips them.
