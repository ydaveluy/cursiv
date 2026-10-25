cmake_minimum_required(VERSION 3.30)
project(@CURSIV_NEW_ID@ LANGUAGES CXX)

include(FetchContent)
# A local checkout wins over the tag: `-DCURSIV_SOURCE_DIR=<path>` is how you
# build this language against an engine you are also editing.
if(CURSIV_SOURCE_DIR)
  FetchContent_Declare(cursiv SOURCE_DIR ${CURSIV_SOURCE_DIR})
else()
  FetchContent_Declare(cursiv
      GIT_REPOSITORY https://github.com/ydaveluy/cursiv.git
      GIT_TAG        @CURSIV_NEW_TAG@)
endif()
FetchContent_MakeAvailable(cursiv)

# FetchContent may use a local override or a shared download directory.
if(CMAKE_SYSTEM_NAME STREQUAL "WASI" AND CURSIV_NODE)
  set(CMAKE_CROSSCOMPILING_EMULATOR ${CURSIV_NODE} --no-warnings
      "${cursiv_SOURCE_DIR}/tools/wasi-run.mjs")
endif()

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
