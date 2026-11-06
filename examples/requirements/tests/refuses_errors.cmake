# Runs `requirements_cli generate` on a model with errors: the command must
# answer 1 and say the error, and write no summary.
execute_process(
  COMMAND ${CLI} generate ${MODEL} -d ${OUT}
  RESULT_VARIABLE status
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)
if(NOT status EQUAL 1)
  message(FATAL_ERROR "expected exit code 1, got '${status}'\n${out}${err}")
endif()
if(NOT err MATCHES "Could not resolve reference to Requirement named 'R9'")
  message(FATAL_ERROR "the unresolved requirement is not reported:\n${err}")
endif()
if(EXISTS ${OUT}/main.html)
  message(FATAL_ERROR "a summary was written: ${OUT}/main.html")
endif()
