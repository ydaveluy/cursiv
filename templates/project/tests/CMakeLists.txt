# Parse the sample and check its syntax tree.
add_executable(@CURSIV_NEW_ID@_grammar_test grammar_test.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  target_link_options(@CURSIV_NEW_ID@_grammar_test PRIVATE --no-wasm-opt)
endif()
target_link_libraries(@CURSIV_NEW_ID@_grammar_test PRIVATE @CURSIV_NEW_ID@Core)
target_compile_definitions(@CURSIV_NEW_ID@_grammar_test PRIVATE
    CURSIV_SAMPLE="${PROJECT_SOURCE_DIR}/example/hello@CURSIV_NEW_EXT@")
add_test(NAME @CURSIV_NEW_ID@_grammar_test COMMAND @CURSIV_NEW_ID@_grammar_test)

# Run the CLI against the same sample.
if(TARGET @CURSIV_NEW_ID@_cli)
  add_test(NAME @CURSIV_NEW_ID@_cli_test COMMAND @CURSIV_NEW_ID@_cli
           ${PROJECT_SOURCE_DIR}/example/hello@CURSIV_NEW_EXT@)
endif()
