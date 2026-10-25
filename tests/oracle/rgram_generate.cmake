# One random grammar's generator run. A refusal is an answer its lane reads,
# and it removes the tree an earlier generator wrote, which the lane would
# otherwise read; anything else that fails is the build's.
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
