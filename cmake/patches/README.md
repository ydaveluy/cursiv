# Contributor guide: allocator integration

This directory contains the WASI patch and license for Cursiv's mimalloc
dependency. To configure a language project's allocator, see
[CMake options](../../docs/reference/cmake.md#options).

The dependency is pinned to mimalloc v3.6.0
(`b18ab4eef383f655e760881b02eff8af02800879`). Each revision has its own source
directory so the WASI patch applies only to its matching revision.
Distributed modules include `mimalloc.LICENSE`.
