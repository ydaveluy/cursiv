# Runs the generator on one random grammar. A refusal is an answer its test
# reads, and removes what an earlier run wrote; any other failure fails the
# build.
file(MAKE_DIRECTORY ${DIR})
execute_process(COMMAND ${GEN} ${DIR}
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
if(rc STREQUAL "0")
  file(REMOVE ${REFUSED})
elseif(rc STREQUAL "1")
  file(GLOB stale ${DIR}/*)
  if(stale)
    file(REMOVE ${stale})
  endif()
  file(WRITE ${REFUSED} "${err}")
else()
  message(FATAL_ERROR "${GEN}: ${rc}\n${out}${err}")
endif()
file(TOUCH ${STAMP})
