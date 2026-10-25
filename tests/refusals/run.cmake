cmake_minimum_required(VERSION 3.30)

# Each lane owns its build tree; only the already built engine libraries are shared.
execute_process(COMMAND "${CMAKE_COMMAND}"
    -S "${SOURCE}" -B "${WORK}" -G "${BUILD_GENERATOR}"
    "-DPARENT=${PARENT}" "-DCASE=${CASE}" "-DLANE=${LANE}"
    "-DENGINE=${ENGINE}" "-DCMAKE_BUILD_TYPE=${CONFIG}"
    RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE log)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Refusal project configuration failed (${result}):\n${log}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK}"
    --target refuse --config "${CONFIG}"
    RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE log)
# A diagnostic can come from compilation or generation, and either may fail.
if(NOT log MATCHES "${EXPECTED}")
  message(FATAL_ERROR "Expected refusal diagnostic missing (${result}):\n${log}")
endif()
message("${log}")
