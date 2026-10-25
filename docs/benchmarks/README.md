# Language benchmarks

Measured on **25 October 2026** with `language_bench`, the benchmark driver for
Cursiv's four example languages. This run uses the **native LR backend**.
At the 4 MiB target, document builds reached the end of validation in
**20.80–74.06 ms** on the machine below.

## Read the results

- [Document builds](#single-document-pipeline): time to parse, resolve names
  and run validation.
- [Editor features](#lsp-providers): time for completion, navigation, formatting
  and other provider calls.
- [Workspace builds](#multi-document-workspace-builds): scaling across files
  and worker counts.

To measure your own language, use the benchmark executable included in your
scaffolded project. Run it with `--help` for input and export options.

## Single-document pipeline

Each cell is the **minimum of five builds**, in milliseconds. Each build uses
a fresh workspace with its worker pool disabled. The process and allocator are
reused. Corpus generation and service setup are outside the timed interval.

- **Parse + index** includes parsing, CST/AST construction and exported-symbol indexing.
- **Scopes**, **link** and **validation** are the subsequent workspace phases;
  link also includes reference indexing.
- **Build** measures from the start of the builder call through completion of
  the validation phase.
- Throughput uses the document's actual byte count and the best build time.

Phase minima are selected independently, so adding them does not necessarily
give the reported build time. Corpus sizes are targets: generators finish
complete language constructs and can produce slightly more bytes.

### 16 KiB target

| Language | Parse + index | Scopes | Link | Validation | Build | MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Arithmetics | 0.114 | 0.009 | 0.032 | 0.099 | 0.255 | 61.4 |
| DomainModel | 0.036 | 0.010 | 0.018 | 0.015 | 0.080 | 196.3 |
| Requirements | 0.025 | 0.006 | 0.017 | 0.023 | 0.070 | 222.5 |
| Statemachine | 0.024 | 0.005 | 0.025 | 0.011 | 0.066 | 239.2 |

### 256 KiB target

| Language | Parse + index | Scopes | Link | Validation | Build | MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Arithmetics | 1.854 | 0.121 | 0.566 | 1.767 | 4.326 | 57.8 |
| DomainModel | 0.548 | 0.175 | 0.273 | 0.237 | 1.234 | 202.6 |
| Requirements | 0.368 | 0.091 | 0.244 | 0.403 | 1.110 | 225.3 |
| Statemachine | 0.364 | 0.059 | 0.473 | 0.232 | 1.136 | 220.1 |

### 4 MiB target

| Language | Parse + index | Scopes | Link | Validation | Build | MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Arithmetics | 29.260 | 2.588 | 12.356 | 29.853 | 74.057 | 54.0 |
| DomainModel | 9.029 | 3.806 | 5.596 | 4.184 | 22.901 | 174.7 |
| Requirements | 6.432 | 1.852 | 4.335 | 9.213 | 21.888 | 182.7 |
| Statemachine | 6.525 | 1.204 | 7.892 | 5.117 | 20.804 | 192.3 |

### References and diagnostics at 4 MiB

Generated inputs parse without syntax recovery, but can produce semantic
diagnostics. These include warnings and errors from linking and the installed
validators, and their cost is included in the build times.

| Language | Actual bytes | References | Resolved | Diagnostics |
| --- | ---: | ---: | ---: | ---: |
| Arithmetics | 4,194,319 | 205,380 | 205,380 | 4,840 |
| DomainModel | 4,194,336 | 147,475 | 147,475 | 0 |
| Requirements | 4,194,329 | 140,134 | 140,134 | 70,067 |
| Statemachine | 4,194,414 | 207,232 | 207,228 | 14,528 |

For example, the Requirements corpus contains requirements without matching
test documents, which exercises the coverage validator. The Statemachine
corpus also contains unresolved references. A build reaching the validated
state does not imply that it produced no diagnostics.

## LSP providers

The following values are **mean milliseconds per direct provider call** on the
4 MiB documents. Each probe is warmed once, then measured five times.

Document-wide services have five samples. Positional services use nine word
starts spread across the document plus EOF, for 50 samples. Empty replies are
included. `selectionRange` is one request containing all ten positions.
Range formatting and ranged semantic tokens cover the **whole document**.

Providers run against an already validated document. Position conversion,
request setup, result destruction, transport and JSON serialization are
outside these timings. Rename and formatting edits are returned but not applied.

| Provider | Arithmetics | DomainModel | Requirements | Statemachine |
| --- | ---: | ---: | ---: | ---: |
| `documentSymbol` | 7.505 | 23.222 | 11.538 | 7.341 |
| `workspace/symbol` | 2.787 | 8.148 | 12.127 | 6.918 |
| `foldingRange` | 26.231 | 7.243 | 2.036 | 4.872 |
| `selectionRange` | 2.620 | 0.342 | 0.568 | 0.940 |
| `semanticTokens/full` | 40.132 | 25.968 | 18.206 | 19.240 |
| `semanticTokens/range` | 43.409 | 26.327 | 17.935 | 19.070 |
| `formatting` | 182.418 | 83.305 | 52.872 | 60.963 |
| `rangeFormatting` | 206.848 | 87.325 | 54.997 | 65.188 |
| `completion` | 3.023 | 1.970 | 2.726 | 1.321 |
| `hover` | 5.090 | 1.094 | 1.433 | 1.985 |
| `definition` | 6.374 | 0.993 | 1.443 | 2.021 |
| `references` | 6.394 | 1.536 | 1.584 | 2.106 |
| `documentHighlight` | 6.096 | 1.686 | 1.502 | 2.095 |
| `prepareRename` | 6.365 | 1.576 | 1.449 | 2.093 |
| `rename` | 21.661 | 32.796 | 3.393 | 6.097 |
| `codeAction` | 0.017 | <0.001 | 0.432 | 7.414 |

Only providers installed in at least one example are shown. **—** means the
provider is absent; **<0.001** means the measured mean is below one microsecond.
An installed provider can return no result for a probe; availability does not
guarantee a nonempty reply.

## Multi-document workspace builds

Each point is the **minimum of five workspace builds through validation**, in
milliseconds, with an approximately 16 MiB corpus. Corpus generation, workspace
setup and worker-pool startup/warmup are outside the timed interval.

The worker count excludes the calling thread: **0** runs inline,
**1** allows two threads, and **31** allows up to 32 threads. Speedup compares
the 31-worker point with the serial point for the same language and corpus.

Four workspace shapes are measured:

| Shape | Composition |
| --- | --- |
| `many-small` | 1,024 files targeting 16 KiB each |
| `medium` | 16 files targeting 1 MiB each |
| `few-large` | 4 files targeting 4 MiB each |
| `mixed` | 819 small files and 3 medium files |

Actual byte counts vary slightly with the language and rounding of the mixed
shape. Compare worker counts within the same row: different languages and
workspace shapes have different syntax and reference graphs.

### DomainModel

References are resolved within each document. All references resolve and these
workspaces produce no diagnostics.

| Shape | Files | MiB | 0 workers | 1 | 2 | 4 | 8 | 31 | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `many-small` | 1,024 | 16.05 | 89.011 | 49.511 | 36.268 | 22.978 | 16.149 | 13.403 | 6.64× |
| `medium` | 16 | 16.00 | 92.197 | 55.980 | 40.614 | 28.480 | 23.136 | 23.460 | 3.93× |
| `few-large` | 4 | 16.00 | 94.544 | 64.580 | 53.323 | 39.615 | 40.629 | 38.307 | 2.47× |
| `mixed` | 822 | 15.84 | 88.521 | 50.955 | 37.666 | 27.538 | 20.491 | 17.724 | 4.99× |

### Requirements

References cross document boundaries. The corpus produces coverage diagnostics;
validation remains part of every measurement.

| Shape | Files | MiB | 0 workers | 1 | 2 | 4 | 8 | 31 | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `many-small` | 1,024 | 16.03 | 107.957 | 52.915 | 37.943 | 25.543 | 18.187 | 14.196 | 7.60× |
| `medium` | 16 | 16.00 | 87.373 | 52.577 | 41.550 | 28.626 | 26.868 | 27.811 | 3.14× |
| `few-large` | 4 | 16.00 | 93.947 | 61.841 | 48.551 | 40.824 | 39.774 | 38.982 | 2.41× |
| `mixed` | 822 | 15.82 | 90.345 | 59.364 | 38.851 | 29.535 | 21.722 | 20.708 | 4.36× |

### Statemachine

References are resolved within each document. All references resolve and these
workspaces produce no diagnostics.

| Shape | Files | MiB | 0 workers | 1 | 2 | 4 | 8 | 31 | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `many-small` | 1,024 | 16.11 | 80.464 | 46.218 | 31.159 | 21.007 | 15.977 | 13.578 | 5.93× |
| `medium` | 16 | 16.00 | 81.536 | 57.744 | 41.018 | 30.198 | 22.987 | 22.719 | 3.59× |
| `few-large` | 4 | 16.00 | 91.517 | 70.056 | 54.105 | 40.084 | 39.086 | 36.820 | 2.49× |
| `mixed` | 822 | 15.89 | 77.626 | 52.042 | 33.390 | 24.429 | 20.096 | 16.891 | 4.60× |

Arithmetics has no multi-document corpus generator registered in
`language_bench`, so it is measured only in the single-document and LSP tables.

## Measurement environment

| Setting | Value |
| --- | --- |
| CPU | Intel Core i9-13900K, 24 physical cores / 32 logical CPUs |
| Memory | 125.36 GiB available to the OS |
| System | Arch Linux, Linux 7.2.11-1-cachyos |
| Compiler | GCC 16.2.1 |
| Build | Release, `-O3 -DNDEBUG`, mimalloc enabled |
| Parser | LR |
| CPU policy | `powersave`, all 32 logical CPUs available, no affinity pinning |
| Load average before / after | 0.21 / 1.57 over one minute |
| Measured at | 2026-10-25, 09:11 CET (Europe/Paris) |
| Sources | Working tree based on `7f70cf994`, including local changes |

These results describe the source snapshot and machine listed above. Use the
reproduction commands to measure another configuration.

## Reproduce these measurements

This section builds the benchmark for Cursiv's example languages. For your own
project, use its `<id>_bench` target instead.

Run from the repository root:

```sh
cmake --preset gcc-release -DCURSIV_DEFAULT_ENGINE=LR -DCURSIV_MIMALLOC=ON
cmake --build build --target language_bench -j12
./build/bench/language_bench \
  --sizes 16K,256K,4M --iterations 5 \
  --multi-bytes 16M --workers 0,1,2,4,8,31
```

Use `--no-multi` to run only the single-document and LSP measurements, or
`--multi` to run only the workspace scaling measurements. Set `--workers`
to the worker counts appropriate for your machine. The console also reports
LSP minimum and maximum durations, and hardware counters when available.
