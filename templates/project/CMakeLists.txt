cmake_minimum_required(VERSION 3.15)
project(@PEGIUM_NEW_LANGUAGE_ID@ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    pegium
    GIT_REPOSITORY https://github.com/ydaveluy/pegium.git
    GIT_TAG @PEGIUM_NEW_PEGIUM_TAG@
)
FetchContent_MakeAvailable(pegium)

# Sources are discovered by globbing, so adding a .cpp under src/ (or a test under
# test/) needs no edit here. CONFIGURE_DEPENDS re-globs when files are added/removed.
set(_src "${CMAKE_CURRENT_SOURCE_DIR}/src/@PEGIUM_NEW_LANGUAGE_ID@")

# Each consumable library also gets a `@PEGIUM_NEW_LANGUAGE_ID@::` namespaced
# alias, mirroring pegium's own `pegium::core` / `pegium::lsp` targets. Link the
# aliases (not the raw names): a parent project that pulls this in with
# add_subdirectory(...) then consumes `@PEGIUM_NEW_LANGUAGE_ID@::core` /
# `@PEGIUM_NEW_LANGUAGE_ID@::lsp` exactly as it would any installed package.

# Core library: every source under core/.
file(GLOB_RECURSE _core_sources CONFIGURE_DEPENDS "${_src}/core/*.cpp")
if(_core_sources)
  add_library(@PEGIUM_NEW_LANGUAGE_ID@-core ${_core_sources})
  add_library(@PEGIUM_NEW_LANGUAGE_ID@::core ALIAS @PEGIUM_NEW_LANGUAGE_ID@-core)
  target_include_directories(@PEGIUM_NEW_LANGUAGE_ID@-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(@PEGIUM_NEW_LANGUAGE_ID@-core PUBLIC pegium::core)
endif()

# CLI tool: every source under cli/ (main.cpp plus any helpers).
file(GLOB_RECURSE _cli_sources CONFIGURE_DEPENDS "${_src}/cli/*.cpp")
if(_cli_sources)
  add_executable(@PEGIUM_NEW_LANGUAGE_ID@-cli ${_cli_sources})
  target_link_libraries(@PEGIUM_NEW_LANGUAGE_ID@-cli PRIVATE @PEGIUM_NEW_LANGUAGE_ID@::core pegium::cli)
endif()

# LSP: the server library is every lsp/ source except the executable entry point,
# which becomes the -lsp server.
file(GLOB_RECURSE _lsp_sources CONFIGURE_DEPENDS "${_src}/lsp/*.cpp")
set(_lsp_main "${_src}/lsp/main.cpp")
list(REMOVE_ITEM _lsp_sources "${_lsp_main}")
if(_lsp_sources)
  add_library(@PEGIUM_NEW_LANGUAGE_ID@-lsp-lib ${_lsp_sources})
  add_library(@PEGIUM_NEW_LANGUAGE_ID@::lsp ALIAS @PEGIUM_NEW_LANGUAGE_ID@-lsp-lib)
  target_include_directories(@PEGIUM_NEW_LANGUAGE_ID@-lsp-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(@PEGIUM_NEW_LANGUAGE_ID@-lsp-lib PUBLIC @PEGIUM_NEW_LANGUAGE_ID@::core pegium::lsp)
endif()

# The LSP server is built whenever lsp/main.cpp exists — even when it is the only
# lsp/ source (so there is no separate server library). Link the server library
# when it exists, otherwise link core + pegium::lsp directly.
if(EXISTS "${_lsp_main}")
  add_executable(@PEGIUM_NEW_LANGUAGE_ID@-lsp ${_lsp_main})
  if(TARGET @PEGIUM_NEW_LANGUAGE_ID@-lsp-lib)
    target_link_libraries(@PEGIUM_NEW_LANGUAGE_ID@-lsp PRIVATE @PEGIUM_NEW_LANGUAGE_ID@::lsp)
  else()
    target_include_directories(@PEGIUM_NEW_LANGUAGE_ID@-lsp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(@PEGIUM_NEW_LANGUAGE_ID@-lsp PRIVATE @PEGIUM_NEW_LANGUAGE_ID@::core pegium::lsp)
  endif()
endif()

enable_testing()
include(GoogleTest)  # provides gtest_discover_tests
# pegium already fetches GoogleTest for pegium::testing, so only declare it if a
# parent project (or pegium) has not provided it. Guard the CRT setting too
# instead of FORCE-ing it, so a parent project's choice is respected.
if(NOT TARGET GTest::gtest_main)
  FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.17.0  # keep in sync with the version pegium pins
  )
  if(NOT DEFINED gtest_force_shared_crt)
    set(gtest_force_shared_crt ON CACHE BOOL "")
  endif()
  FetchContent_MakeAvailable(googletest)
endif()

# Tests: every source under test/. The parsing smoke test needs only the core
# pipeline; the LSP feature tests (test/lsp/) use the reusable pegium::testing
# harness, so they and pegium::testing are linked only when the LSP server is built.
file(GLOB_RECURSE _test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")
if(_test_sources)
  add_executable(@PEGIUM_NEW_LANGUAGE_ID@-test ${_test_sources})
  set(_test_libs @PEGIUM_NEW_LANGUAGE_ID@::core pegium::cli GTest::gtest_main)
  if(TARGET @PEGIUM_NEW_LANGUAGE_ID@-lsp-lib)
    list(APPEND _test_libs @PEGIUM_NEW_LANGUAGE_ID@::lsp pegium::testing)
  endif()
  target_link_libraries(@PEGIUM_NEW_LANGUAGE_ID@-test PRIVATE ${_test_libs})
  target_compile_definitions(@PEGIUM_NEW_LANGUAGE_ID@-test PRIVATE
      PEGIUM_NEW_SAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/example/hello@PEGIUM_NEW_EXT@")
  # Register each GTest case as its own CTest test (IDE explorers, --gtest_filter).
  gtest_discover_tests(@PEGIUM_NEW_LANGUAGE_ID@-test DISCOVERY_MODE PRE_TEST)
endif()
