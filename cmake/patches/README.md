# Contributor guide: allocator integration

This directory contains the WASI patch and license for Cursiv's mimalloc
dependency. To configure a language project's allocator, see
[CMake options](../../docs/reference/cmake.md#options).

The dependency is pinned to mimalloc v3.6.1
(`56dc5eb80145f9a3ba7ca23d47c6a248b9d8586c`). Each revision has its own source
directory so the WASI patch applies only to its matching revision.
Distributed modules include `mimalloc.LICENSE`.

WASI keeps 32 KiB allocator slices inside fully committed 64 KiB system
pages. Its memory primitives do not commit or protect subranges, so the
patch exempts unguarded WASI builds from the virtual-memory slice-size check.
