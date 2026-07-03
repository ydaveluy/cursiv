cmake_minimum_required(VERSION 3.15)

project(pegium VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(PEGIUM_MASTER_PROJECT ON)
else()
    set(PEGIUM_MASTER_PROJECT OFF)
endif()

option(PEGIUM_PROFILE_BUILD "Enable profiling-friendly flags (no LTO, visible symbols, frame pointers)" OFF)
option(PEGIUM_OPTIMIZE_SIZE "Minimize binary size: thin-LTO, hidden visibility, --gc-sections and identical-code folding. ICF needs lld/gold/mold (auto-detected). Off by default since LTO slows compiles." OFF)
option(PEGIUM_ENABLE_ASAN "Enable AddressSanitizer for supported toolchains" OFF)
option(PEGIUM_ENABLE_TSAN "Enable ThreadSanitizer for supported toolchains" OFF)
option(PEGIUM_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for supported toolchains" OFF)
option(PEGIUM_BUILD_FUZZ_TARGETS "Build FuzzTest-based Pegium fuzz targets" OFF)
# The exhaustive language fuzz corpus sweeps (the deterministic *KeywordFuzzTest
# / *SemicolonColonFuzzTest mutation sweeps) are cheap in optimized builds
# (~6s total) but slow under sanitizers or unoptimized Debug builds. Default
# them ON only where they are cheap so a plain `cmake` build still exercises
# recovery on every mutation; override with -DPEGIUM_ENABLE_FUZZ_CORPUS=ON/OFF.
if(PEGIUM_ENABLE_ASAN OR PEGIUM_ENABLE_TSAN OR PEGIUM_ENABLE_UBSAN
   OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(PEGIUM_FUZZ_CORPUS_DEFAULT OFF)
else()
    set(PEGIUM_FUZZ_CORPUS_DEFAULT ON)
endif()
option(PEGIUM_ENABLE_FUZZ_CORPUS
    "Register exhaustive language-specific fuzz corpus tests (default ON for optimized non-sanitizer builds; cheap there, slow under Debug/ASan)"
    ${PEGIUM_FUZZ_CORPUS_DEFAULT})
option(PEGIUM_BUILD_EXAMPLES "Build pegium examples" ${PEGIUM_MASTER_PROJECT})

# pegium::testing is the reusable test-harness library. pegium's own build gets
# it through BUILD_TESTING; downstream consumers get it by default so they can
# link `pegium::testing` from their test target without extra flags.
if(PEGIUM_MASTER_PROJECT)
    set(PEGIUM_BUILD_TESTING_SUPPORT_DEFAULT OFF)
else()
    set(PEGIUM_BUILD_TESTING_SUPPORT_DEFAULT ON)
endif()
option(PEGIUM_BUILD_TESTING_SUPPORT
    "Build the pegium::testing reusable test-harness library"
    ${PEGIUM_BUILD_TESTING_SUPPORT_DEFAULT})

if(NOT PEGIUM_MASTER_PROJECT AND NOT DEFINED BUILD_TESTING)
    set(BUILD_TESTING OFF CACHE BOOL "Build the test suite")
endif()
include(CTest)

if(BUILD_TESTING)
    include(FetchContent)
    include(GoogleTest)

    set(PEGIUM_PROJECT_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
    set(PEGIUM_TEST_ROOT_DIR "${PROJECT_SOURCE_DIR}/tests")

    if(NOT TARGET GTest::gtest_main)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.17.0
        )
        # For Windows: Prevent overriding the parent project's compiler/linker settings
        if(NOT DEFINED gtest_force_shared_crt)
            set(gtest_force_shared_crt ON CACHE BOOL "")
        endif()
        if(NOT DEFINED BUILD_GMOCK)
            set(BUILD_GMOCK OFF CACHE BOOL "")
        endif()
        if(NOT DEFINED INSTALL_GTEST)
            set(INSTALL_GTEST OFF CACHE BOOL "")
        endif()
        FetchContent_MakeAvailable(googletest)
    endif()

    # In-tree test-support (header-only) libraries. Use the pegium::test_support_*
    # aliases everywhere, matching the pegium::core / pegium::testing style.
    add_library(pegium_test_support_core INTERFACE)
    add_library(pegium::test_support_core ALIAS pegium_test_support_core)
    target_include_directories(pegium_test_support_core INTERFACE
        $<BUILD_INTERFACE:${PEGIUM_TEST_ROOT_DIR}>
    )

    add_library(pegium_test_support_lsp INTERFACE)
    add_library(pegium::test_support_lsp ALIAS pegium_test_support_lsp)
    target_link_libraries(pegium_test_support_lsp INTERFACE
        pegium::test_support_core
    )

    add_library(pegium_test_support_examples INTERFACE)
    add_library(pegium::test_support_examples ALIAS pegium_test_support_examples)
    target_link_libraries(pegium_test_support_examples INTERFACE
        pegium::test_support_lsp
    )

    function(pegium_register_gtest_target target)
        set(options)
        set(oneValueArgs TEST_FILTER)
        set(multiValueArgs LABELS)
        cmake_parse_arguments(PEGIUM_GTEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

        set(discover_args DISCOVERY_MODE PRE_TEST)
        if(PEGIUM_GTEST_TEST_FILTER)
            list(APPEND discover_args TEST_FILTER "${PEGIUM_GTEST_TEST_FILTER}")
        endif()
        if(PEGIUM_GTEST_LABELS)
            list(APPEND discover_args PROPERTIES LABELS "${PEGIUM_GTEST_LABELS}")
        endif()
        gtest_discover_tests(${target} ${discover_args})

        set_property(GLOBAL APPEND PROPERTY PEGIUM_GTEST_TARGETS ${target})
    endfunction()
endif()

add_library(pegium_internal_build_options INTERFACE)
add_library(pegium_internal_coverage_options INTERFACE)
set(PEGIUM_INTERNAL_OPTION_TARGETS
    pegium_internal_build_options
    pegium_internal_coverage_options
)

set(PEGIUM_SANITIZER_FLAGS)
if(PEGIUM_ENABLE_ASAN AND PEGIUM_ENABLE_TSAN)
    message(FATAL_ERROR
        "PEGIUM_ENABLE_ASAN and PEGIUM_ENABLE_TSAN cannot be enabled together.")
endif()
if(PEGIUM_ENABLE_ASAN)
    list(APPEND PEGIUM_SANITIZER_FLAGS -fsanitize=address)
endif()
if(PEGIUM_ENABLE_TSAN)
    list(APPEND PEGIUM_SANITIZER_FLAGS -fsanitize=thread)
endif()
if(PEGIUM_ENABLE_UBSAN)
    list(APPEND PEGIUM_SANITIZER_FLAGS -fsanitize=undefined)
endif()
if(PEGIUM_SANITIZER_FLAGS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR
            "PEGIUM_ENABLE_ASAN/PEGIUM_ENABLE_TSAN/PEGIUM_ENABLE_UBSAN require GCC or Clang.")
    endif()
    target_compile_options(pegium_internal_build_options INTERFACE
        ${PEGIUM_SANITIZER_FLAGS}
        -fno-omit-frame-pointer
    )
    target_link_options(pegium_internal_build_options INTERFACE
        ${PEGIUM_SANITIZER_FLAGS}
    )
endif()

if(PEGIUM_ENABLE_ASAN)
    target_compile_definitions(pegium_internal_build_options INTERFACE
        ADDRESS_SANITIZER
    )
endif()

if(PEGIUM_ENABLE_TSAN)
    target_compile_definitions(pegium_internal_build_options INTERFACE
        THREAD_SANITIZER
    )
endif()

if(PEGIUM_PROFILE_BUILD)
    target_compile_options(pegium_internal_build_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-omit-frame-pointer>
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-optimize-sibling-calls>
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-inline>
    )
endif()

if(PEGIUM_OPTIMIZE_SIZE)
    if(PEGIUM_PROFILE_BUILD)
        message(FATAL_ERROR
            "PEGIUM_OPTIMIZE_SIZE and PEGIUM_PROFILE_BUILD are mutually exclusive "
            "(size optimization hides symbols and enables LTO, which defeat profiling).")
    endif()
    # The size levers below are GCC/Clang + GNU/Apple-linker specific. On other
    # toolchains (notably MSVC) the option is accepted but is a no-op, so build
    # scripts can pass -DPEGIUM_OPTIMIZE_SIZE=ON unconditionally.
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS
            "PEGIUM_OPTIMIZE_SIZE: no-op for ${CMAKE_CXX_COMPILER_ID} "
            "(size levers require GCC or Clang).")
    else()
        # Hide internal symbols (pegium is static / FetchContent-only, so there is
        # no exported shared-library ABI to preserve), split every function and
        # data item into its own section so the linker can drop the unreferenced
        # ones, and let the optimizer work across translation units with LTO.
        set(PEGIUM_SIZE_COMPILE_OPTIONS
            -fvisibility=hidden
            -fvisibility-inlines-hidden
            -ffunction-sections
            -fdata-sections
            $<$<CXX_COMPILER_ID:Clang>:-flto=thin>
            $<$<CXX_COMPILER_ID:GNU>:-flto>
        )
        target_compile_options(pegium_internal_build_options INTERFACE
            ${PEGIUM_SIZE_COMPILE_OPTIONS})
        # Link flags must also reach downstream executables: pegium's static
        # libraries are LTO bitcode, so a consumer linking pegium::lsp needs the
        # matching -flto (and benefits from folding/dead-stripping). These are
        # propagated through the public library targets' INTERFACE below, after
        # they are defined.
        set(PEGIUM_SIZE_LINK_OPTIONS
            $<$<CXX_COMPILER_ID:Clang>:-flto=thin>
            $<$<CXX_COMPILER_ID:GNU>:-flto>
        )
        if(APPLE)
            # Apple's linker garbage-collects unreachable code with -dead_strip and
            # folds identical code by default; the GNU --gc-sections / --icf
            # spellings do not apply here.
            list(APPEND PEGIUM_SIZE_LINK_OPTIONS -Wl,-dead_strip)
        else()
            list(APPEND PEGIUM_SIZE_LINK_OPTIONS -Wl,--gc-sections)

            # Identical-code folding collapses the duplicate template
            # instantiations the grammar DSL emits per language — the dominant
            # size cost. It needs lld, gold or mold; plain GNU ld (bfd) has no
            # --icf, so probe for a capable linker and fold only when available.
            find_program(PEGIUM_ICF_MOLD mold)
            find_program(PEGIUM_ICF_LLD ld.lld)
            find_program(PEGIUM_ICF_GOLD ld.gold)
            if(PEGIUM_ICF_MOLD)
                list(APPEND PEGIUM_SIZE_LINK_OPTIONS -fuse-ld=mold -Wl,--icf=all)
            elseif(PEGIUM_ICF_LLD)
                list(APPEND PEGIUM_SIZE_LINK_OPTIONS -fuse-ld=lld -Wl,--icf=all)
            elseif(PEGIUM_ICF_GOLD)
                list(APPEND PEGIUM_SIZE_LINK_OPTIONS -fuse-ld=gold -Wl,--icf=all)
            else()
                message(STATUS
                    "PEGIUM_OPTIMIZE_SIZE: no ICF-capable linker (mold/lld/gold) "
                    "found; building without identical-code folding.")
            endif()
        endif()
        target_link_options(pegium_internal_build_options INTERFACE ${PEGIUM_SIZE_LINK_OPTIONS})
    endif()
endif()

option(PEGIUM_ENABLE_CODECOVERAGE   "Enable code coverage testing support"            OFF)
option(PEGIUM_ENABLE_RECOVERY_TRACE "Enable verbose recovery traces"                   OFF)
option(PEGIUM_ENABLE_STEP_TRACE     "Enable step counters for recovery instrumentation" OFF)
option(PEGIUM_DISABLE_RECOVERY "Compile out the per-grammar error-recovery instantiations (opt-in; parse() of invalid input returns the strict result + an Incomplete diagnostic, same as ParseOptions::recoveryEnabled=false at runtime, but ~83% less template instantiation per rule)" OFF)

if(PEGIUM_ENABLE_RECOVERY_TRACE)
    target_compile_definitions(pegium_internal_build_options INTERFACE
        PEGIUM_ENABLE_RECOVERY_TRACE
    )
endif()

if(PEGIUM_ENABLE_STEP_TRACE)
    target_compile_definitions(pegium_internal_build_options INTERFACE
        PEGIUM_ENABLE_STEP_TRACE
    )
endif()

if(PEGIUM_DISABLE_RECOVERY)
    target_compile_definitions(pegium_internal_build_options INTERFACE
        PEGIUM_DISABLE_RECOVERY
    )
endif()

if(PEGIUM_ENABLE_CODECOVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "(Apple)?[Cc]lang")
        target_compile_options(pegium_internal_coverage_options INTERFACE
            --coverage
        )
        target_link_options(pegium_internal_coverage_options INTERFACE
            --coverage
        )
        set(GCOVR_OPTIONS --gcov-executable "llvm-cov gcov")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(pegium_internal_coverage_options INTERFACE
            --coverage
        )
        target_link_options(pegium_internal_coverage_options INTERFACE
            --coverage
        )
        set(GCOVR_OPTIONS)
    else()
        message(FATAL_ERROR
            "Code coverage is not supported for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

if(PEGIUM_BUILD_FUZZ_TARGETS)
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR
            "PEGIUM_BUILD_FUZZ_TARGETS requires BUILD_TESTING=ON.")
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR
            "PEGIUM_BUILD_FUZZ_TARGETS is only supported on Linux.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "PEGIUM_BUILD_FUZZ_TARGETS requires Clang.")
    endif()

    if(FUZZTEST_FUZZING_MODE)
        target_compile_definitions(pegium_internal_build_options INTERFACE
            ADDRESS_SANITIZER
            FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
        )
        target_compile_options(pegium_internal_build_options INTERFACE
            -g
            -UNDEBUG
            -fsanitize=address
            -fsanitize-coverage=inline-8bit-counters
            -fsanitize-coverage=trace-cmp
        )
        target_link_options(pegium_internal_build_options INTERFACE
            -fsanitize=address
        )
    endif()

    if(NOT COMMAND link_fuzztest)
        FetchContent_Declare(
            fuzztest
            GIT_REPOSITORY https://github.com/google/fuzztest.git
            GIT_TAG        2026-02-19
        )
        if(NOT DEFINED FUZZTEST_BUILD_TESTING)
            set(FUZZTEST_BUILD_TESTING OFF CACHE BOOL "")
        endif()
        FetchContent_MakeAvailable(fuzztest)
    endif()
endif()

add_subdirectory(src/pegium)

# Propagate the size-optimization link flags through the public library targets'
# INTERFACE so anything linking pegium::core / pegium::lsp / ... (pegium's own
# executables and downstream FetchContent consumers alike) links the LTO bitcode
# correctly and folds/strips its own binary.
if(PEGIUM_OPTIMIZE_SIZE AND DEFINED PEGIUM_SIZE_LINK_OPTIONS)
    foreach(_pegium_lib IN ITEMS PegiumCore PegiumConverters PegiumLsp PegiumCli PegiumTesting)
        if(TARGET ${_pegium_lib})
            target_link_options(${_pegium_lib} INTERFACE ${PEGIUM_SIZE_LINK_OPTIONS})
        endif()
    endforeach()

    # Build the one bundled dependency that has compiled code — the LSP framework
    # (taskflow is header-only and already LTO'd inside pegium's TUs) — with the
    # same size flags, so the server binary's framework code is optimized and
    # folded like pegium's own.
    if(TARGET lsp)
        target_compile_options(lsp PRIVATE ${PEGIUM_SIZE_COMPILE_OPTIONS})
    endif()
endif()

if(PEGIUM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
