# A project outside this tree, built and run as a consumer would: its tree
# holds no generated source, it names no engine header, and touching its own
# unit rebuilds only that unit and the link.
cmake_minimum_required(VERSION 3.30)

file(READ ${SOURCE}/main.cpp _app)
if(_app MATCHES "#include \"cursiv/")
  message(FATAL_ERROR "the consumer names an engine header: the module exists "
                      "so the application does not have to")
endif()

file(REMOVE_RECURSE ${WORK})
execute_process(
    COMMAND ${CMAKE_COMMAND} -S ${SOURCE} -B ${WORK} -G Ninja
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_CXX_COMPILER=${CXX}
            -DCMAKE_CXX_FLAGS=-fstack-protector-strong
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
            -DCURSIV_SOURCE_DIR=${CURSIV}
            -DCURSIV_BUILD_TESTS=OFF
            -DCURSIV_WARNINGS_AS_ERRORS=ON
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "consumer configure failed (${_rc})")
endif()

# Strict Cursiv builds must leave the consumer, generator and generated code
# in control of their own warnings, including flags inherited through linking.
file(READ "${WORK}/compile_commands.json" _commands)
string(JSON _count LENGTH "${_commands}")
math(EXPR _last "${_count} - 1")
set(_checked "")
foreach(_i RANGE ${_last})
  string(JSON _command GET "${_commands}" ${_i} command)
  if(_command MATCHES "-fno-stack-protector")
    message(FATAL_ERROR "Cursiv disabled the consumer's stack protection: ${_command}")
  endif()
  if(_command MATCHES "CMakeFiles[/\\](consumer|tiny_gen|tinyGenerated)[.]dir[/\\]")
    list(APPEND _checked "${CMAKE_MATCH_1}")
    if(_command MATCHES "(^|[ \t])(-Wall|-Wextra|-Werror)([ \t]|$)")
      message(FATAL_ERROR "Cursiv imposed warning flags on its consumer: ${_command}")
    endif()
  endif()
endforeach()
list(REMOVE_DUPLICATES _checked)
list(LENGTH _checked _count)
if(NOT _count EQUAL 3)
  message(FATAL_ERROR "Consumer warning policy checked ${_count} targets instead "
                      "of 3. The last command:\n${_command}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build ${WORK} -- -n
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _plan ERROR_VARIABLE _plan)
if(NOT _rc EQUAL 0 OR _plan MATCHES "CMakeFiles[/\\](cursivCli|cursivLsp|cursivMcp|cursivTest|lsp|lspgen)[.]dir[/\\]")
  message(FATAL_ERROR "A core-only consumer builds unused services:\n${_plan}")
endif()

string(TIMESTAMP _b0 "%s")
execute_process(COMMAND ${CMAKE_COMMAND} --build ${WORK}
                        --target consumer tiny_bench tiny_generated_bench tiny_services_bench
                RESULT_VARIABLE _rc)
string(TIMESTAMP _b1 "%s")
math(EXPR _built "${_b1} - ${_b0}")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "consumer build failed (${_rc})")
endif()

execute_process(COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${WORK}
                        --output-on-failure -R consumer_benchmark
                RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "consumer benchmark failed (${_rc})")
endif()

set(_consumer ${WORK}/consumer)
if(EXISTS ${_consumer}.exe)
  string(APPEND _consumer .exe)
endif()
execute_process(COMMAND ${_consumer} RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "the consumer did not read its own language (${_rc})")
endif()

# A transitive grammar include must rebuild the generator and rerun generation.
file(TOUCH ${SOURCE}/core/tiny/ast.hpp)
execute_process(COMMAND ${CMAKE_COMMAND} --build ${WORK} --target consumer
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _log ERROR_VARIABLE _log)
if(NOT _rc EQUAL 0 OR NOT _log MATCHES "tiny: writing")
  message(FATAL_ERROR "a grammar include did not trigger generation\n${_log}")
endif()

# Touching the application's own unit plans that unit and the link, nothing
# of the module; its time is judged against the whole build's.
file(TOUCH ${SOURCE}/main.cpp)
execute_process(COMMAND ${CMAKE_COMMAND} --build ${WORK} --target consumer
                        -- -n
                OUTPUT_VARIABLE _plan RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "consumer plan failed (${_rc})")
endif()
string(REGEX MATCHALL "\\[[0-9]+/[0-9]+\\] [^\n]*" _edges "${_plan}")
list(LENGTH _edges _planned)
if(NOT _planned EQUAL 2)
  message(FATAL_ERROR "touching the application's unit plans ${_planned} steps, "
                      "not its compile and its link: the module is not holding "
                      "the metaprogram\n${_plan}")
endif()
string(TIMESTAMP _t0 "%s")
execute_process(COMMAND ${CMAKE_COMMAND} --build ${WORK} --target consumer
                RESULT_VARIABLE _rc)
string(TIMESTAMP _t1 "%s")
math(EXPR _dt "${_t1} - ${_t0}")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "consumer rebuild failed (${_rc})")
endif()
math(EXPR _share "${_dt} * 5")
if(_dt GREATER 2 AND _share GREATER _built)
  message(FATAL_ERROR "the application's own unit took ${_dt}s of a ${_built}s "
                      "build: the module is not holding the metaprogram")
endif()

# Cross compiled, the host's generator still reads the grammar with its
# directory's definitions. Configured only.
set(_cross ${WORK}-cross)
file(REMOVE_RECURSE ${_cross})
execute_process(
    COMMAND ${CMAKE_COMMAND} -S ${SOURCE} -B ${_cross} -G Ninja
            -DCMAKE_SYSTEM_NAME=${CMAKE_HOST_SYSTEM_NAME}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_CXX_COMPILER=${CXX}
            -DCURSIV_SOURCE_DIR=${CURSIV}
            -DCURSIV_BUILD_TESTS=OFF
            -DCURSIV_MIMALLOC=OFF
            -DFETCHCONTENT_SOURCE_DIR_LSP_FRAMEWORK=${WORK}/_deps/lsp_framework-src
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _log ERROR_VARIABLE _log)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "cross consumer configure failed (${_rc})\n${_log}")
endif()
file(READ ${_cross}/host-tools/build.ninja _host)
string(REGEX MATCHALL "[^\n]*CURSIV_GEN_NAME=[^\n]*" _generators "${_host}")
if(NOT _generators MATCHES "-DTINY_CONSUMER=1")
  message(FATAL_ERROR "the cross build's generator misses its directory's definitions")
endif()

message(STATUS "consumer: builds, reads, and rebuilds its own unit in ${_dt}s "
               "of ${_built}s")
