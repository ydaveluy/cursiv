cmake_minimum_required(VERSION 3.30)

function(_step what)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE _rc
      OUTPUT_VARIABLE _log ERROR_VARIABLE _log)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${what} failed (${_rc})\n${_log}")
  endif()
endfunction()

foreach(_allocator IN ITEMS ON OFF)
  set(_build "${WORK}/${_allocator}")
  _step("allocator consumer configure (${_allocator})" "${CMAKE_COMMAND}"
      -S "${CMAKE_CURRENT_LIST_DIR}/allocator" -B "${_build}" -G Ninja
      -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=${CXX}
      -DCURSIV=${CURSIV} -DCORE=${CORE} -DMIMALLOC_SOURCE=${MIMALLOC_SOURCE}
      -DCURSIV_MIMALLOC=${_allocator})
  _step("allocator consumer build (${_allocator})" "${CMAKE_COMMAND}"
      --build "${_build}")
  _step("allocator consumer tests (${_allocator})" "${CMAKE_CTEST_COMMAND}"
      --test-dir "${_build}" --output-on-failure)
endforeach()
