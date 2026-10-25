# THE SCAFFOLD, RUN: `docs/cursiv-new.cmake` writes a project outside this
# tree, and that project is then configured, built and tested the way its README
# says. What only a run can prove: the templates still name an engine surface
# that exists, and a project with nothing generated in it still builds.
cmake_minimum_required(VERSION 3.30)

file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})

function(_step what)
  execute_process(COMMAND ${ARGN} WORKING_DIRECTORY ${WORK}
                  RESULT_VARIABLE _rc OUTPUT_VARIABLE _log ERROR_VARIABLE _log)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${what} failed (${_rc})\n${_log}")
  endif()
endfunction()

# The published scaffolder is packed FROM the templates: editing one without
# repacking the other is the drift this catches before anything is built.
_step("repacking the scaffolder" ${CMAKE_COMMAND} -DOUT=${WORK}/packed.cmake
      -P ${GENERATE})
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORK}/packed.cmake
                        ${SCAFFOLD} RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${SCAFFOLD} is behind templates/project: repack it with "
                      "cmake -P cmake/GenerateScaffold.cmake")
endif()

# A name can be lexically valid yet reserve the generated namespace in C++.
foreach(_name IN ITEMS Class Foo__Bar _Hidden)
  execute_process(COMMAND ${CMAKE_COMMAND} -DNAME=${_name}
      -DDIR=${WORK}/reserved-${_name} -P ${SCAFFOLD}
      RESULT_VARIABLE _rc OUTPUT_VARIABLE _log ERROR_VARIABLE _log)
  if(_rc EQUAL 0 OR NOT _log MATCHES "reserved C[+][+] identifiers")
    message(FATAL_ERROR "the scaffold accepted reserved name ${_name}: ${_log}")
  endif()
endforeach()

# No is a valid identifier even though CMake evaluates it as false.
_step("the scaffolder" ${CMAKE_COMMAND} -DNAME=No -P ${SCAFFOLD})
set(_project ${WORK}/no)

# ONE CONFIGURE AND ONE BUILD, over a tree that holds no generated source: the
# sequence the scaffolded README prints is the sequence run here.
_step("the configure" ${CMAKE_COMMAND} -S ${_project} -B ${_project}/build
      -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=${CXX}
      -DCURSIV_SOURCE_DIR=${CURSIV})
_step("the build" ${CMAKE_COMMAND} --build ${_project}/build)
_step("the tests" ${CMAKE_CTEST_COMMAND} --test-dir ${_project}/build
      --output-on-failure)
_step("the benchmark" ${_project}/build/src/no_bench
      ${_project}/example/hello.no --iterations 1
      --json ${WORK}/benchmark.json)
file(READ ${WORK}/benchmark.json _bench)
string(JSON _completion TYPE "${_bench}" rows 0 lsp completion ms)
string(JSON _stages LENGTH "${_bench}" rows 0 stages)
if(NOT _completion STREQUAL "NUMBER" OR NOT _stages EQUAL 5)
  message(FATAL_ERROR "the scaffold's benchmark does not measure LSP completion")
endif()

# Optional folders can be removed without editing the generated CMake files.
file(REMOVE_RECURSE ${_project}/src/no/bench)
_step("the configure without benchmark" ${CMAKE_COMMAND}
      -S ${_project} -B ${_project}/build)
file(REMOVE_RECURSE ${_project}/src/no/cli ${_project}/src/no/lsp)
_step("the core-only configure" ${CMAKE_COMMAND} -S ${_project} -B ${_project}/build)
_step("the core-only build" ${CMAKE_COMMAND} --build ${_project}/build)
_step("the core-only tests" ${CMAKE_CTEST_COMMAND} --test-dir ${_project}/build
      --output-on-failure)
execute_process(COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${_project}/build
    --show-only=json-v1 OUTPUT_VARIABLE _tests COMMAND_ERROR_IS_FATAL ANY)
string(JSON _count LENGTH "${_tests}" tests)
if(NOT _count EQUAL 1)
  message(FATAL_ERROR "the core-only scaffold registers ${_count} tests instead of one")
endif()

message(STATUS "scaffold: a new language configures, generates, builds and parses")
