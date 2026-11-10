# The default toolchain of the wasi presets: the Clang of the pinned wasi-sdk
# 34. Another compiler shares the rest through wasi-platform.cmake. Cursiv
# throws, so `-fwasm-exceptions` selects the SDK's `eh` sysroot.
if(NOT WASI_SDK)
  set(WASI_SDK $ENV{WASI_SDK})
endif()
if(CMAKE_HOST_WIN32)
  set(_cursiv_exe .exe)
  file(TO_CMAKE_PATH "${WASI_SDK}" WASI_SDK)
  file(TO_CMAKE_PATH "$ENV{USERPROFILE}" _cursiv_home)
else()
  set(_cursiv_exe "")
  set(_cursiv_home "$ENV{HOME}")
endif()
# The conventional spots, so a preset works with no environment at all.
if(NOT WASI_SDK)
  foreach(_cursiv_candidate "${_cursiv_home}/wasi-sdk-34.0" "/opt/wasi-sdk")
    if(EXISTS "${_cursiv_candidate}/bin/clang++${_cursiv_exe}")
      set(WASI_SDK "${_cursiv_candidate}")
      break()
    endif()
  endforeach()
endif()
if(NOT WASI_SDK)
  message(FATAL_ERROR
      "run cmake -P tools/fetch-wasi-sdk.cmake, or set "
      "-DWASI_SDK=<wasi-sdk-34+ directory> (the presets also look in "
      "~/wasi-sdk-34.0 and /opt/wasi-sdk), or pass your own toolchain with "
      "--toolchain.")
endif()

# Carried into try_compile, which re-runs this file in a scope the cache does
# not reach: without this line CMake probes the compiler with no compiler set.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES WASI_SDK)

set(CMAKE_C_COMPILER   ${WASI_SDK}/bin/clang${_cursiv_exe})
set(CMAKE_CXX_COMPILER ${WASI_SDK}/bin/clang++${_cursiv_exe})
set(CMAKE_AR           ${WASI_SDK}/bin/llvm-ar${_cursiv_exe})
set(CMAKE_RANLIB       ${WASI_SDK}/bin/llvm-ranlib${_cursiv_exe})
set(CMAKE_SYSROOT      ${WASI_SDK}/share/wasi-sysroot)

# `-mllvm -wasm-use-legacy-eh=false` selects the EH scheme the runtime still
# accepts.
set(_cursiv_wasi_eh "-fwasm-exceptions -mllvm -wasm-use-legacy-eh=false")
set(CMAKE_C_FLAGS_INIT   "${_cursiv_wasi_eh}")
set(CMAKE_CXX_FLAGS_INIT "${_cursiv_wasi_eh}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wno-unused-command-line-argument -lunwind")

include(${CMAKE_CURRENT_LIST_DIR}/wasi-platform.cmake)

set(CMAKE_C_COMPILER_TARGET   ${CURSIV_WASI_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${CURSIV_WASI_TARGET})

# SDK Clang runs wasm-opt automatically for optimized links when it is on
# PATH, and preserves target_features for it. Do not add another pass here.
