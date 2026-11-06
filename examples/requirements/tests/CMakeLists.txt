cursiv_example_tests(requirements)

# A summary is not written over a model with errors: here a test names a
# requirement that nobody declares.
if(NOT CMAKE_CROSSCOMPILING)
  add_test(NAME requirements_cli_refuses_errors
           COMMAND ${CMAKE_COMMAND}
                   -DCLI=$<TARGET_FILE:requirements_cli>
                   -DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/with-errors/main.req
                   -DOUT=${CMAKE_CURRENT_BINARY_DIR}/with-errors
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/refuses_errors.cmake)
endif()
