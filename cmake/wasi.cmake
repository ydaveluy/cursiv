# The WASI toolchain used by the wasi-debug and wasi-release presets.
# wasi-sdk 34 is the pinned release. Cursiv throws: `-fwasm-exceptions` at
# both compile and link selects the `eh` half of its dual `eh`/`noeh` sysroot.
set(CMAKE_SYSTEM_NAME WASI)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

if(NOT WASI_SDK)
  set(WASI_SDK $ENV{WASI_SDK})
endif()
# The conventional spots, so a preset works with no environment at all.
if(NOT WASI_SDK)
  foreach(_cursiv_candidate "$ENV{HOME}/wasi-sdk-34.0" "/opt/wasi-sdk")
    if(EXISTS "${_cursiv_candidate}/bin/clang++")
      set(WASI_SDK "${_cursiv_candidate}")
      break()
    endif()
  endforeach()
endif()
if(NOT WASI_SDK)
  message(FATAL_ERROR
      "set -DWASI_SDK=<wasi-sdk-34+ directory> (or install it at "
      "~/wasi-sdk-34.0 or /opt/wasi-sdk). The wasm target builds ONLY with "
      "the SDK's own clang -- gcc cannot emit wasm.")
endif()

# Carried into try_compile, which re-runs this file in a scope the cache
# does not reach: without this line CMake probes the compiler with no
# compiler set.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    WASI_SDK CURSIV_WASI_THREADS CURSIV_WASI_MAX_MEMORY CURSIV_WASI_STACK)

set(CMAKE_C_COMPILER   ${WASI_SDK}/bin/clang)
set(CMAKE_CXX_COMPILER ${WASI_SDK}/bin/clang++)
set(CMAKE_AR           ${WASI_SDK}/bin/llvm-ar)
set(CMAKE_RANLIB       ${WASI_SDK}/bin/llvm-ranlib)
set(CMAKE_SYSROOT      ${WASI_SDK}/share/wasi-sysroot)
# Threads are a different target, not a flag: `wasm32-wasip1-threads` has
# its own sysroot and libc++. It is the production target (vscode/node), so
# it is the default; OFF builds the threadless `wasm32-wasip1`, inline-only.
option(CURSIV_WASI_THREADS "Build for wasm32-wasip1-threads" ON)
if(CURSIV_WASI_THREADS)
  set(_cursiv_wasi_target wasm32-wasip1-threads)
else()
  set(_cursiv_wasi_target wasm32-wasip1)
endif()
set(CMAKE_C_COMPILER_TARGET   ${_cursiv_wasi_target})
set(CMAKE_CXX_COMPILER_TARGET ${_cursiv_wasi_target})

# `-mllvm -wasm-use-legacy-eh=false` selects the EH scheme the runtime still
# accepts; the _WASI_EMULATED_* defines are what lets `<csignal>` be included
# on a target with no signals.
set(_cursiv_wasi_emul "-D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS")
if(CURSIV_WASI_THREADS)
  # Define _REENTRANT when compiling. At link time the threaded target already
  # selects libpthread and shared memory; the memory must also be imported so
  # the runner can hand it to each worker.
  add_compile_options(-pthread)
  # A shared memory must declare its maximum at link time (wasm format). It is
  # an addressing cap, not a reservation: pages are committed as the heap
  # grows. Must be a multiple of 65536; 4 GiB is the wasm32 ceiling.
  # The 100,000-level LL stress test needs about 1 GiB with mimalloc.
  set(CURSIV_WASI_MAX_MEMORY 2147483648 CACHE STRING
      "Shared-memory maximum handed to the linker (bytes)")
  # Clang already selects shared memory for the threaded target.
  add_link_options("LINKER:--import-memory,--max-memory=${CURSIV_WASI_MAX_MEMORY}")
endif()
set(_cursiv_wasi_eh "-fwasm-exceptions -mllvm -wasm-use-legacy-eh=false")
set(CMAKE_C_FLAGS_INIT   "${_cursiv_wasi_eh} ${_cursiv_wasi_emul}")
set(CMAKE_CXX_FLAGS_INIT "${_cursiv_wasi_eh} ${_cursiv_wasi_emul}")
# The linker's default stack is 64 KiB, which recursive AST walks and large
# frames overrun: past it the stack pointer wraps into data and a store traps.
set(CURSIV_WASI_STACK 1048576 CACHE STRING
    "Stack reserved for a wasm executable's main thread (bytes)")
# Directory link options are regenerated when cache tunables change, without
# rewriting the user's CMAKE_EXE_LINKER_FLAGS.
add_link_options("LINKER:-z,stack-size=${CURSIV_WASI_STACK}")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-Wno-unused-command-line-argument -lunwind -lwasi-emulated-signal -lwasi-emulated-process-clocks")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# SDK Clang runs wasm-opt automatically for optimized links when it is on
# PATH, and preserves target_features for it. Do not add another pass here.
# The SDK's libraries contain DWARF even when our own sources use Release.
add_link_options("$<$<CONFIG:Release>:LINKER:--strip-debug>")

# The project resolves the runner after locating the Cursiv source tree.
find_program(CURSIV_NODE node)
