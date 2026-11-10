# What every WASI toolchain shares, whichever compiler it names. A toolchain
# file names its compiler and includes this one: see cmake/wasi.cmake, and
# "Use your own toolchain" in docs/recipes/wasm.md.
set(CMAKE_SYSTEM_NAME WASI)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

# The current WASI SDK drops C++ exception handlers under LTO. Disable IPO,
# including configuration-specific requests, until that path is validated.
get_cmake_property(_cursiv_wasi_ipo_settings VARIABLES)
list(FILTER _cursiv_wasi_ipo_settings INCLUDE
     REGEX "^CMAKE_INTERPROCEDURAL_OPTIMIZATION($|_)")
list(APPEND _cursiv_wasi_ipo_settings CMAKE_INTERPROCEDURAL_OPTIMIZATION)
list(REMOVE_DUPLICATES _cursiv_wasi_ipo_settings)
foreach(_cursiv_wasi_ipo IN LISTS _cursiv_wasi_ipo_settings)
  if(${_cursiv_wasi_ipo})
    message(STATUS
        "cursiv: disabling ${_cursiv_wasi_ipo} for WASI: "
        "LTO breaks C++ exceptions in the current SDK")
  endif()
  set(${_cursiv_wasi_ipo} OFF CACHE BOOL
      "Enable interprocedural optimization" FORCE)
  set(${_cursiv_wasi_ipo} OFF)
endforeach()

# Carried into try_compile, which re-runs the toolchain in a scope the cache
# does not reach.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
     CURSIV_WASI_THREADS CURSIV_WASI_MAX_MEMORY CURSIV_WASI_STACK)

# Threads are a different target, not a flag: `wasm32-wasip1-threads` has
# its own sysroot and libc++. It is the production target (vscode/node), so
# it is the default; OFF builds the threadless `wasm32-wasip1`, inline-only.
option(CURSIV_WASI_THREADS "Build for wasm32-wasip1-threads" ON)
if(CURSIV_WASI_THREADS)
  set(CURSIV_WASI_TARGET wasm32-wasip1-threads)
else()
  set(CURSIV_WASI_TARGET wasm32-wasip1)
endif()

# The _WASI_EMULATED_* defines are what lets `<csignal>` be included on a target
# with no signals.
set(_cursiv_wasi_emul "-D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS")
string(APPEND CMAKE_C_FLAGS_INIT " ${_cursiv_wasi_emul}")
string(APPEND CMAKE_CXX_FLAGS_INIT " ${_cursiv_wasi_emul}")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT
       " -lwasi-emulated-signal -lwasi-emulated-process-clocks")
if(CURSIV_WASI_THREADS)
  # Define _REENTRANT when compiling. At link time the threaded target already
  # selects libpthread and shared memory; the memory must also be imported so
  # the runner can hand it to each worker.
  add_compile_options(-pthread)
  # A shared memory declares its maximum at link time: a cap, not a
  # reservation, a multiple of 65536 up to 4 GiB. The 100,000-level LL stress
  # test needs about 1 GiB.
  set(CURSIV_WASI_MAX_MEMORY 2147483648 CACHE STRING
      "Shared-memory maximum handed to the linker (bytes)")
  add_link_options("LINKER:--import-memory,--max-memory=${CURSIV_WASI_MAX_MEMORY}")
endif()
# The linker's default stack is 64 KiB, which recursive AST walks and large
# frames overrun: past it the stack pointer wraps into data and a store traps.
set(CURSIV_WASI_STACK 1048576 CACHE STRING
    "Stack reserved for a wasm executable's main thread (bytes)")
# Directory link options are regenerated when cache tunables change, without
# rewriting the user's CMAKE_EXE_LINKER_FLAGS.
add_link_options("LINKER:-z,stack-size=${CURSIV_WASI_STACK}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# A sysroot's libraries can contain DWARF even when our own sources use Release.
add_link_options("$<$<CONFIG:Release>:LINKER:--strip-debug>")

# The project resolves the runner after locating the Cursiv source tree.
find_program(CURSIV_NODE node)
