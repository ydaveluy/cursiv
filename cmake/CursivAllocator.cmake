# One allocator for every cursiv executable: mimalloc, linked as an object so
# its malloc comes before libc's. Off by default on Windows and macOS and under
# global sanitizer flags; a target with its own sanitizer flags must opt out.
if(WIN32 OR APPLE)
  set(_allocator_default OFF)
else()
  set(_allocator_default ON)
endif()
option(CURSIV_MIMALLOC "Link mimalloc into every executable" ${_allocator_default})
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
set(_mimalloc_revision 56dc5eb80145f9a3ba7ca23d47c6a248b9d8586c) # v3.6.1
set(_mimalloc_dir "cursiv_mimalloc-${_mimalloc_revision}")
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  # A changed WASI patch gets fresh sources, never a tree an older one patched.
  file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/patches/mimalloc-wasi.patch" _patch_hash)
  string(SUBSTRING "${_patch_hash}" 0 12 _patch_hash)
  string(APPEND _mimalloc_dir "-wasi-${_patch_hash}")
endif()
FetchContent_Declare(cursiv_mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG ${_mimalloc_revision}
    # Keep version-specific WASI patches out of later dependency updates.
    SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/${_mimalloc_dir}-src"
    SOURCE_SUBDIR cursiv-source-only)
FetchContent_MakeAvailable(cursiv_mimalloc)

add_library(cursivAllocator OBJECT ${cursiv_mimalloc_SOURCE_DIR}/src/static.c)
target_include_directories(cursivAllocator SYSTEM PRIVATE
    ${cursiv_mimalloc_SOURCE_DIR}/include)
# Documents are small: huge pages would hold far more memory than they use, and
# clearing them costs more than they save. MIMALLOC_ALLOW_THP=1 restores them.
target_compile_definitions(cursivAllocator PRIVATE
    MI_STATIC_LIB MI_MALLOC_OVERRIDE MI_DEFAULT_ALLOW_THP=0)
# Third-party code: its warnings are silenced.
target_compile_options(cursivAllocator PRIVATE -w)
set_target_properties(cursivAllocator PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  # mimalloc's WASI port lacks locks, thread exit and arena reuse for large
  # alignments, and clears new memory that is already zero: the patch fixes
  # them. A tree it already patched is left as it is.
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
      # WASI cannot protect guard pages; keep debug builds on the same backend.
      MI_STATS=0 MI_PROFILE=0 MI_GUARDED=0
      # Even very large blocks need recyclable arenas: WASM cannot release an
      # OS allocation. mimalloc clamps this KiB limit to its 32-bit arena size.
      MI_DEFAULT_ARENA_MAX_OBJECT_SIZE=2097152)
endif()

# Sanitized targets and targets defining their own allocator opt out explicitly.
target_link_libraries(cursivCore INTERFACE
    $<$<NOT:$<BOOL:$<TARGET_PROPERTY:CURSIV_OWN_ALLOCATOR>>>:$<TARGET_OBJECTS:cursivAllocator>>)
