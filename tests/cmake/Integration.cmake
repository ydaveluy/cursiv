# The wasm cross runs the same lanes; only the threadless target loses the
# ones that construct raw std::thread.
if(CMAKE_CROSSCOMPILING AND NOT CURSIV_WASI_THREADS)
  set_tests_properties(threads_test PROPERTIES DISABLED TRUE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "WASI" AND CURSIV_WASI_THREADS)
  cursiv_test(WasiThreads_test integration/WasiThreads_test.cpp)
  set_tests_properties(WasiThreads_test PROPERTIES TIMEOUT 30)
  foreach(_mode IN ITEMS worker-exit7 worker-trap)
    add_test(NAME WasiThreads_${_mode} COMMAND WasiThreads_test ${_mode})
    set_tests_properties(WasiThreads_${_mode} PROPERTIES WILL_FAIL TRUE TIMEOUT 5)
  endforeach()
endif()

# Every shipped language server keeps each stack frame far under the wasm
# stack, a fixed reservation that a larger frame traps past.
if(CMAKE_CROSSCOMPILING AND CURSIV_NODE)
  set(CURSIV_WASM_FRAME_LIMIT 16384)
  get_filename_component(_cursiv_sdk_bin ${CMAKE_CXX_COMPILER} DIRECTORY)
  find_program(CURSIV_WASM_OBJDUMP llvm-objdump HINTS ${_cursiv_sdk_bin})
  if(NOT CURSIV_WASM_OBJDUMP)
    message(STATUS "cursiv: no llvm-objdump, the stack-frame lane is skipped")
  endif()
  set(_cursiv_servers "")
  set(_cursiv_dirs ${CURSIV_SOURCE_ROOT}/examples)
  while(_cursiv_dirs)
    list(POP_FRONT _cursiv_dirs _cursiv_dir)
    get_property(_cursiv_sub DIRECTORY ${_cursiv_dir} PROPERTY SUBDIRECTORIES)
    list(APPEND _cursiv_dirs ${_cursiv_sub})
    get_property(_cursiv_targets DIRECTORY ${_cursiv_dir}
        PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_cursiv_target IN LISTS _cursiv_targets)
      if(_cursiv_target MATCHES "_lsp$")
        list(APPEND _cursiv_servers $<TARGET_FILE:${_cursiv_target}>)
      endif()
    endforeach()
  endwhile()
  add_test(NAME wasm_stack_frames COMMAND ${CURSIV_NODE}
      ${CURSIV_SOURCE_ROOT}/tools/wasm-frames.mjs "${CURSIV_WASM_OBJDUMP}"
      ${CURSIV_WASM_FRAME_LIMIT} ${_cursiv_servers})
  set_tests_properties(wasm_stack_frames PROPERTIES SKIP_RETURN_CODE 77
      TIMEOUT 120)
endif()

if(TARGET cursivAllocator)
  cursiv_test(Allocator_test integration/Allocator_test.cpp)
  target_include_directories(Allocator_test SYSTEM PRIVATE
      $<TARGET_PROPERTY:cursivAllocator,INCLUDE_DIRECTORIES>)
  set_tests_properties(Allocator_test PROPERTIES TIMEOUT 60)
  if(NOT CMAKE_CROSSCOMPILING)
    FetchContent_GetProperties(cursiv_mimalloc SOURCE_DIR _allocator_source)
    add_test(NAME allocator_consumer_test COMMAND ${CMAKE_COMMAND}
        -DCURSIV=${CURSIV_SOURCE_ROOT}
        -DCORE=$<TARGET_FILE:cursivCore>
        -DMIMALLOC_SOURCE=${_allocator_source}
        -DCXX=${CMAKE_CXX_COMPILER}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/integration/allocator-build
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/allocator_test.cmake)
    set_tests_properties(allocator_consumer_test PROPERTIES TIMEOUT 120
        RESOURCE_LOCK cursiv_build)
  endif()
endif()

# What a consumer actually does: a project outside this tree, fetched, built and
# run. Not cross compiled -- the lane configures a second CMake project, and the
# host is the only machine that can build one here.
if(NOT CMAKE_CROSSCOMPILING)
  add_test(NAME fetchcontent_test COMMAND ${CMAKE_COMMAND}
      -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/integration/consumer
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/integration/consumer-build
      -DCURSIV=${CURSIV_SOURCE_ROOT}
      -DCXX=${CMAKE_CXX_COMPILER}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/fetchcontent_test.cmake)
  # It compiles a consumer project and times one of its units: a wall budget
  # only means something when nothing else is compiling beside it.
  set_tests_properties(fetchcontent_test PROPERTIES TIMEOUT 600
      RESOURCE_LOCK cursiv_build RUN_SERIAL TRUE)

  # The scaffold, run: a language nobody wrote, made by `docs/cursiv-new.cmake`
  # and taken through its own README -- configure, generate, configure, build,
  # parse. An unrun scaffold rots silently; this is the only thing that says so.
  add_test(NAME scaffold_test COMMAND ${CMAKE_COMMAND}
      -DSCAFFOLD=${CURSIV_SOURCE_ROOT}/docs/cursiv-new.cmake
      -DGENERATE=${CURSIV_SOURCE_ROOT}/cmake/GenerateScaffold.cmake
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/integration/scaffold
      -DCURSIV=${CURSIV_SOURCE_ROOT}
      -DCXX=${CMAKE_CXX_COMPILER}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/scaffold_test.cmake)
  set_tests_properties(scaffold_test PROPERTIES TIMEOUT 900
      RESOURCE_LOCK cursiv_build)

  # Refused at configure, before any compile could fail on it.
  add_test(NAME refused_grammar_name_test COMMAND ${CMAKE_COMMAND}
      -S ${CMAKE_CURRENT_SOURCE_DIR}/integration/refused_grammar_name
      -B ${CMAKE_CURRENT_BINARY_DIR}/integration/refused_grammar_name
      -DCURSIV_SOURCE_DIR=${CURSIV_SOURCE_ROOT})
  set_tests_properties(refused_grammar_name_test PROPERTIES
      PASS_REGULAR_EXPRESSION
      "GRAMMAR lr_build.hpp is named like a source of the LR generator")
endif()
