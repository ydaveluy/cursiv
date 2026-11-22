# Contributor guide: allocator integration

This directory contains the WASI patch and license for Cursiv's mimalloc
dependency. To configure a language project's allocator, see
[CMake options](../../docs/reference/cmake.md#options).

The dependency is pinned to mimalloc v3.7.0
(`1ed67b5156a763441d2ec3c20221a7b3cd866129`). Each revision has its own source
directory so the WASI patch applies only to its matching revision.
Distributed modules include `mimalloc.LICENSE`.

WASM memory only grows, so the patch keeps large aligned blocks in recyclable
arenas and reports new pages as zero. It also adds thread exit and locks for
threaded WASI, and reads options from `environ` without allocating.
