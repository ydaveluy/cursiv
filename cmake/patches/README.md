# Contributor guide: allocator integration

This directory contains the WASI patch and license for Cursiv's mimalloc
dependency. To configure a language project's allocator, see
[CMake options](../../docs/reference/cmake.md#options).

The dependency is pinned to mimalloc v3.7.1
(`1c842769cd1ef35f044f659804806505b58f0b48`). Each revision has its own source
directory so the WASI patch applies only to its matching revision.
Distributed modules include `mimalloc.LICENSE`.

A threaded module's memory starts at its full size and never grows, so the
patch allocates from the module's initial memory first. It also runs thread
exit through a pthread key when CMake turns pthreads on for the threaded target.
