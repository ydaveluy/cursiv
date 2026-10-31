# The report is well formed: the run answers 0, the text names every class,
# and the JSON parses with every count a class owes. What the counts say is
# not judged here.
execute_process(
    COMMAND ${EMULATOR} ${METRIC} --docs 3 --reps 1 --json ${OUT}.json
    OUTPUT_VARIABLE text
    RESULT_VARIABLE code)
if(NOT code EQUAL 0)
  message(FATAL_ERROR "recovery_metric answered ${code}\n${text}")
endif()
foreach(cls byte multi:1 multi:3 multi:10 sep-series eof:1 eof:2-3 eof:4-7
            eof:8-20 lex:quote lex:esc lex:comment hole typo)
  string(FIND "${text}" "\n${cls} " at)
  if(at EQUAL -1)
    message(FATAL_ERROR "the text report has no row for ${cls}\n${text}")
  endif()
endforeach()
file(READ ${OUT}.json json)
string(JSON grammars ERROR_VARIABLE err LENGTH "${json}" grammars)
if(err OR grammars LESS 1)
  message(FATAL_ERROR "the JSON report names no grammar: ${err}")
endif()
math(EXPR last "${grammars} - 1")
foreach(g RANGE ${last})
  string(JSON classes ERROR_VARIABLE err LENGTH "${json}" grammars ${g} classes)
  if(err OR NOT classes EQUAL 14)
    message(FATAL_ERROR "grammar ${g}: ${classes} classes, not 14: ${err}")
  endif()
  math(EXPR lastClass "${classes} - 1")
  foreach(c RANGE ${lastClass})
    foreach(key docs faults diagnostics found missed parasites silent noop
                refused treeless machine)
      string(JSON v ERROR_VARIABLE err GET "${json}" grammars ${g} classes ${c}
             ${key})
      if(err OR NOT v MATCHES "^[0-9]+$")
        message(FATAL_ERROR "grammar ${g} class ${c}: ${key} = '${v}' ${err}")
      endif()
    endforeach()
    foreach(key eligible kept deleted abandoned other restored invented)
      string(JSON v ERROR_VARIABLE err GET "${json}" grammars ${g} classes ${c}
             text ${key})
      if(err OR NOT v MATCHES "^[0-9]+$")
        message(FATAL_ERROR "grammar ${g} class ${c}: text.${key} = '${v}'")
      endif()
    endforeach()
    foreach(key total kept whole)
      string(JSON v ERROR_VARIABLE err GET "${json}" grammars ${g} classes ${c}
             ast ${key})
      if(err OR NOT v MATCHES "^[0-9]+$")
        message(FATAL_ERROR "grammar ${g} class ${c}: ast.${key} = '${v}'")
      endif()
    endforeach()
    string(JSON n ERROR_VARIABLE err LENGTH "${json}" grammars ${g} classes ${c}
           per_fault)
    if(err OR NOT n EQUAL 6)
      message(FATAL_ERROR "grammar ${g} class ${c}: per_fault has ${n} bins")
    endif()
  endforeach()
endforeach()
message(STATUS "recovery metric report well formed: ${grammars} grammar(s)")
