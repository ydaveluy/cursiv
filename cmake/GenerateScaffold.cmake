# Run: cmake -DCURSIV_TAG=<tag> -P cmake/GenerateScaffold.cmake
# Packs the project templates and shared configuration into docs/cursiv-new.cmake.
cmake_minimum_required(VERSION 3.14)

if(NOT DEFINED CURSIV_TAG)
  set(CURSIV_TAG "main")
endif()

set(_root "${CMAKE_CURRENT_LIST_DIR}/..")
set(_tpl_dir "${_root}/templates/project")
# -DOUT=<path> writes elsewhere, which is how the freshness lane compares the
# committed scaffolder against the templates without touching it.
if(NOT DEFINED OUT)
  set(OUT "${_root}/docs/cursiv-new.cmake")
endif()

file(GLOB_RECURSE _files RELATIVE "${_tpl_dir}" "${_tpl_dir}/*")

set(_body "")
foreach(_rel IN LISTS _files)
  file(READ "${_tpl_dir}/${_rel}" _content)
  string(APPEND _body
    "_cursiv_new_emit([==[${_rel}]==] [==[${_content}]==])\n")
endforeach()
# Files the project shares with this repository. Its .clang-format is not one
# of them: templates/project/ carries its own.
foreach(_rel IN ITEMS CMakePresets.json cmake/wasi.cmake)
  file(READ "${_root}/${_rel}" _content)
  string(APPEND _body
      "_cursiv_new_emit([==[${_rel}]==] [==[${_content}]==])\n")
endforeach()

file(READ "${CMAKE_CURRENT_LIST_DIR}/cursiv-new.cmake.in" _head)
string(CONFIGURE "${_head}" _head @ONLY)
set(_banner
"# =============================================================================
# GENERATED FILE -- DO NOT EDIT.
# Produced by cmake/GenerateScaffold.cmake from cmake/cursiv-new.cmake.in and
# templates/project/, CMakePresets.json and cmake/wasi.cmake. Edit those
# and regenerate:  cmake -DCURSIV_TAG=main -P cmake/GenerateScaffold.cmake
# =============================================================================
")
file(WRITE "${OUT}" "${_banner}${_head}\n${_body}\n_cursiv_new_finish()\n")
message(STATUS "Wrote ${OUT} (${CURSIV_TAG})")
