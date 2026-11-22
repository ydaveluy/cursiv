function(report generator)
  set(dir "${CMAKE_CURRENT_BINARY_DIR}/grammar_diagnostics/${generator}")
  file(MAKE_DIRECTORY "${dir}")
  execute_process(COMMAND "${${generator}}" "${dir}" COMMAND_ERROR_IS_FATAL ANY
      OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
  set(conflicts "${stdout}" PARENT_SCOPE)
  set(warnings "${stderr}" PARENT_SCOPE)
endfunction()

set(ENV{CURSIV_CONFLICTS} 1)
report(GUARDED)
if(NOT conflicts MATCHES "state [0-9]+ on [^\n]+ --\n      after   [^\n]+\n      reduce  "
    OR conflicts MATCHES "shift   -> state 0\n")
  message(FATAL_ERROR "Incorrect conflict path: ${conflicts}")
endif()
report(FACTORING)
foreach(part IN ITEMS "local LR conflict" "after   'grouped:' 'x'"
    "Factoring::A alternative 1" "Factoring::A alternative 2"
    "a=A (b=B | c=C)" "a=A b=B | {S} a=A c=C")
  string(FIND "${conflicts}" "${part}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "Missing '${part}' in conflict report")
  endif()
endforeach()

report(CONDITIONS)
if(NOT conflicts MATCHES "local LR conflict[^\n]*\n      condition [0-9]+[^\n]*\n      reduce  ")
  message(FATAL_ERROR "Missing condition entry in conflict report: ${conflicts}")
endif()

unset(ENV{CURSIV_CONFLICTS})
report(FLAGS)
if(NOT warnings MATCHES "option\\(flag<&Decl::isConst>\\(e\\)\\)"
    OR NOT warnings MATCHES "Counted::Root sets Decl::isStatic"
    OR warnings MATCHES "Turned::Root sets Decl::raw")
  message(FATAL_ERROR "Incorrect nullable flag warnings: ${warnings}")
endif()
report(INDENT)
if(warnings MATCHES "flag operand")
  message(FATAL_ERROR "Virtual tokens must consume a parser token: ${warnings}")
endif()

set(dir "${CMAKE_CURRENT_BINARY_DIR}/grammar_diagnostics/GUARDED")
file(SHA256 "${dir}/Table.hpp" before)
foreach(name IN ITEMS invalid.json Module.cpp)
  if(name STREQUAL "Module.cpp")
    # The source publication changes this editor file between its read and its replacement.
    set(original [[{"repository":{"keywords":{"match":"old"}}}]])
    set(reason "changed during generation")
  else()
    set(original "{}")
    set(reason "repository.keywords.match")
  endif()
  file(WRITE "${dir}/${name}" "${original}")
  execute_process(COMMAND "${FACTORING}" "--textmate=${dir}/${name}" "${dir}"
      RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE message)
  file(SHA256 "${dir}/Table.hpp" after)
  file(READ "${dir}/${name}" retained)
  if(result EQUAL 0 OR NOT message MATCHES "${reason}" OR NOT before STREQUAL after
      OR NOT retained STREQUAL original)
    message(FATAL_ERROR "A refused generation must preserve its inputs and outputs: ${message}")
  endif()
endforeach()
