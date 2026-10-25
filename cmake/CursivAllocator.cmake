# One allocator for every cursiv executable: mimalloc, linked as an object so
# its malloc family stands before libc's -- an archive would not guarantee
# that. Global sanitizer flags disable it automatically; per-target sanitizer
# flags require an explicit opt-out. The threadless wasm target is unsupported.
option(CURSIV_MIMALLOC "Link mimalloc into every executable" ON)
if(NOT CURSIV_MIMALLOC OR
   (CMAKE_SYSTEM_NAME STREQUAL "WASI" AND NOT CURSIV_WASI_THREADS))
  return()
endif()

# Keep the user's option intact. Multi-config builds share this allocator, so
# a sanitizer in any configured build excludes it from every configuration.
get_directory_property(_allocator_compile_options COMPILE_OPTIONS)
get_directory_property(_allocator_link_options LINK_OPTIONS)
set(_allocator_flags
    "${CMAKE_C_FLAGS} ${CMAKE_CXX_FLAGS} ${CMAKE_EXE_LINKER_FLAGS} ${_allocator_compile_options} ${_allocator_link_options}")
set(_allocator_configs ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
foreach(_config IN LISTS _allocator_configs)
  string(TOUPPER "${_config}" _config)
  string(APPEND _allocator_flags
      " ${CMAKE_C_FLAGS_${_config}} ${CMAKE_CXX_FLAGS_${_config}} ${CMAKE_EXE_LINKER_FLAGS_${_config}}")
endforeach()
if(_allocator_flags MATCHES "[-/]fsanitize=")
  message(STATUS "cursiv: mimalloc off under a sanitizer")
  return()
endif()

enable_language(C)
include(FetchContent)
set(_mimalloc_revision b18ab4eef383f655e760881b02eff8af02800879) # v3.6.0
FetchContent_Declare(cursiv_mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG ${_mimalloc_revision}
    # Keep version-specific WASI patches out of later dependency updates.
    SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/cursiv_mimalloc-${_mimalloc_revision}-src"
    SOURCE_SUBDIR cursiv-source-only)
FetchContent_MakeAvailable(cursiv_mimalloc)

add_library(cursivAllocator OBJECT ${cursiv_mimalloc_SOURCE_DIR}/src/static.c)
target_include_directories(cursivAllocator SYSTEM PRIVATE
    ${cursiv_mimalloc_SOURCE_DIR}/include)
target_compile_definitions(cursivAllocator PRIVATE
    MI_STATIC_LIB MI_MALLOC_OVERRIDE)
# Not this tree's to keep warning-clean.
target_compile_options(cursivAllocator PRIVATE -w)
set_target_properties(cursivAllocator PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  # The upstream port lacks locks, thread exit and arena reuse for large
  # alignments: the patch adds them. Verified on a populated tree too, so a
  # changed patch never leaves an old port in use.
  find_package(Git REQUIRED)
  set(_patch "${CMAKE_CURRENT_LIST_DIR}/patches/mimalloc-wasi.patch")
  execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check "${_patch}"
      WORKING_DIRECTORY "${cursiv_mimalloc_SOURCE_DIR}"
      RESULT_VARIABLE _patched OUTPUT_QUIET ERROR_QUIET)
  if(NOT _patched EQUAL 0)
    execute_process(COMMAND ${GIT_EXECUTABLE} apply "${_patch}"
        WORKING_DIRECTORY "${cursiv_mimalloc_SOURCE_DIR}"
        COMMAND_ERROR_IS_FATAL ANY)
  endif()
  target_compile_definitions(cursivAllocator PRIVATE
      MI_FREE_USE_PAGEMAP=1
      # The compact byte-offset map cannot represent large interior alignments.
      MI_PAGE_MAP_FLAT=0
      # WASI cannot protect guard pages; keep debug builds on the same backend.
      MI_STATS=0 MI_PROFILE=0 MI_GUARDED=0
      # Even very large blocks need recyclable arenas: WASM cannot release an
      # OS allocation. mimalloc clamps this KiB limit to its 32-bit arena size.
      MI_DEFAULT_ARENA_MAX_OBJECT_SIZE=2097152)
endif()

# Sanitized targets and targets defining their own allocator opt out explicitly.
target_link_libraries(cursivCore INTERFACE
    $<$<NOT:$<BOOL:$<TARGET_PROPERTY:CURSIV_OWN_ALLOCATOR>>>:$<TARGET_OBJECTS:cursivAllocator>>)
