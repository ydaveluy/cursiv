cmake_minimum_required(VERSION 3.30)
project(@CURSIV_NEW_ID@ LANGUAGES CXX)

include(FetchContent)
# Cursiv is downloaded at the tag below. To build against a local copy of
# Cursiv instead, configure with `-DCURSIV_SOURCE_DIR=<path>`.
if(CURSIV_SOURCE_DIR)
  FetchContent_Declare(cursiv SOURCE_DIR ${CURSIV_SOURCE_DIR})
else()
  FetchContent_Declare(cursiv
      GIT_REPOSITORY https://github.com/ydaveluy/cursiv.git
      GIT_TAG        @CURSIV_NEW_TAG@)
endif()
FetchContent_MakeAvailable(cursiv)

# WebAssembly (WASI) builds run their tests with Node.js.
if(CMAKE_SYSTEM_NAME STREQUAL "WASI" AND CURSIV_NODE)
  set(CMAKE_CROSSCOMPILING_EMULATOR ${CURSIV_NODE} --no-warnings
      "${cursiv_SOURCE_DIR}/tools/wasi-run.mjs")
endif()

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
