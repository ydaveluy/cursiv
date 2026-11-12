# Refused grammars: each header of refusals/ names its lanes, one line per
# expected sentence, `// expect LR LL: <regex>`. A check both engines share
# runs on LR alone.
if(NOT CMAKE_CROSSCOMPILING)
  set(_refusals ${CMAKE_CURRENT_BINARY_DIR}/refusals)
  # The fixtures of one unit: the compiler's memory and the object's sections
  # grow with them.
  set(_refusal_unit_size 32)
  # A lane is named after its fixture, with its engine where it is not LR.
  set(_refusal_suffix_LR "")
  set(_refusal_suffix_LL _ll)

  add_executable(refusal_lanes refusals/lanes.cpp)

  file(GLOB _refusal_cases CONFIGURE_DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/refusals/*.hpp)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      ${_refusal_cases})
  # Every header a fixture reads, for a unit to read first: no engine header
  # then meets the name a unit gives each fixture's namespace.
  set(_refusal_includes "")
  foreach(_file IN LISTS _refusal_cases)
    get_filename_component(_case ${_file} NAME_WE)
    file(STRINGS ${_file} _includes REGEX "^#include ")
    list(APPEND _refusal_includes ${_includes})
    file(STRINGS ${_file} _expectations ENCODING UTF-8
        REGEX "^// expect [A-Z ]+( at compile)?: ")
    foreach(_expectation IN LISTS _expectations)
      string(REGEX MATCH "^// expect ([A-Z ]+)( at compile)?: (.*)$" _
          "${_expectation}")
      set(_kind run)
      if(CMAKE_MATCH_2)
        set(_kind compile)
      endif()
      # One regex, whatever it holds: a bare `;` would make it two.
      string(REPLACE ";" "\\;" _expected "${CMAKE_MATCH_3}")
      string(REPLACE " " ";" _engines "${CMAKE_MATCH_1}")
      foreach(_engine IN LISTS _engines)
        string(TOLOWER "${_engine}" _name)
        set(_lane ${_case}${_refusal_suffix_${_engine}})
        list(APPEND _refusal_${_engine}_${_kind} ${_case})
        # `at compile` marks a sentence the compiler gives: these lanes share
        # compilations that fail, and each reads the diagnostics naming its
        # fixture. The others share a generator, which runs the grammar named.
        if(_kind STREQUAL "compile")
          add_test(NAME refusal_${_lane} COMMAND ${CMAKE_COMMAND} -E cat
              ${_refusals}/compile/refusals_${_name}_compile/${_case}.log)
          set_tests_properties(refusal_${_lane} PROPERTIES
              FIXTURES_REQUIRED refusals_compile)
        else()
          file(MAKE_DIRECTORY ${_refusals}/${_lane})
          add_test(NAME refusal_${_lane} COMMAND refusals_${_name} ${_lane}
              ${_refusals}/${_lane})
        endif()
        set_tests_properties(refusal_${_lane} PROPERTIES
            PASS_REGULAR_EXPRESSION "${_expected}")
      endforeach()
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES _refusal_includes)
  list(FILTER _refusal_includes EXCLUDE REGEX "refusals/head[.]hpp")
  list(JOIN _refusal_includes "\n" _refusal_includes)

  set(_refusal_compiled "")
  foreach(_engine IN ITEMS LR LL)
    string(TOLOWER "${_engine}" _name)
    cursiv_engine_parts(${_engine} _main _lib)
    foreach(_kind IN ITEMS run compile)
      set(_cases ${_refusal_${_engine}_${_kind}})
      if(NOT _cases)
        continue()
      endif()
      set(_sources "")
      set(_lanes "")
      set(_calls "")
      list(LENGTH _cases _count)
      math(EXPR _last "${_count} - 1")
      foreach(_first RANGE 0 ${_last} ${_refusal_unit_size})
        list(SUBLIST _cases ${_first} ${_refusal_unit_size} _held)
        string(CONCAT _unit
            "// Generated refusal fixtures, each with its generator.\n"
            "${_refusal_includes}\n"
            "#define CURSIV_GEN_NO_MAIN\n"
            "#define CURSIV_GEN_ID \"\"\n"
            "#define CURSIV_GEN_EXTENSIONS \"\"\n"
            "#define CURSIV_GEN_FILE_NAMES \"\"\n"
            "#define CURSIV_GEN_NS \"refuse::generated::G\"\n")
        foreach(_case IN LISTS _held)
          set(_lane ${_case}${_refusal_suffix_${_engine}})
          string(MAKE_C_IDENTIFIER "${_case}" _fixture)
          string(MAKE_C_IDENTIFIER "${_lane}" _scope)
          string(APPEND _unit
              "#define refuse refusal_${_fixture}::refuse\n"
              "#include \"refusals/${_case}.hpp\"\n"
              "#undef refuse\n"
              "#define CURSIV_GEN_SCOPE lane_${_scope}\n"
              "#define CURSIV_GEN_GRAMMAR refusal_${_fixture}::refuse::G\n"
              "#define CURSIV_GEN_HEADER \"refusals/${_case}.hpp\"\n"
              "#define CURSIV_GEN_NAME \"refusal_${_lane}\"\n"
              "#include \"${_main}\"\n"
              "#undef CURSIV_GEN_SCOPE\n"
              "#undef CURSIV_GEN_GRAMMAR\n"
              "#undef CURSIV_GEN_HEADER\n"
              "#undef CURSIV_GEN_NAME\n")
          string(APPEND _lanes
              "namespace lane_${_scope} { int run(int, char**); }\n")
          string(APPEND _calls
              "  if (lane == \"${_lane}\") return lane_${_scope}::run(argc - 1, argv + 1);\n")
        endforeach()
        set(_source ${_refusals}/${_name}_${_kind}_${_first}.cpp)
        file(CONFIGURE OUTPUT ${_source} CONTENT "${_unit}" @ONLY)
        list(APPEND _sources ${_source})
      endforeach()

      if(_kind STREQUAL "run")
        string(CONCAT _entry
            "// Generated: runs the lane its first argument names.\n"
            "#include <cstdio>\n"
            "#include <string_view>\n"
            "${_lanes}"
            "int main(int argc, char** argv) {\n"
            "  const std::string_view lane = argc > 1 ? argv[1] : \"\";\n"
            "${_calls}"
            "  std::fprintf(stderr, \"%s: no such lane\\n\", argv[0]);\n"
            "  return 2;\n"
            "}\n")
        set(_source ${_refusals}/${_name}_${_kind}.cpp)
        file(CONFIGURE OUTPUT ${_source} CONTENT "${_entry}" @ONLY)
        set(_target refusals_${_name})
        add_executable(${_target} ${_sources} ${_source})
      else()
        # Built by `refusals/compile.cmake` alone, which expects it to fail.
        set(_target refusals_${_name}_compile)
        add_library(${_target} OBJECT EXCLUDE_FROM_ALL ${_sources})
        set_target_properties(${_target} PROPERTIES EXPORT_COMPILE_COMMANDS OFF)
        # Every error of a unit, each with its whole trace.
        target_compile_options(${_target} PRIVATE
            -ftemplate-backtrace-limit=0
            $<$<CXX_COMPILER_ID:GNU>:-fmax-errors=0>
            $<$<CXX_COMPILER_ID:Clang,AppleClang>:-ferror-limit=0>)
        list(APPEND _refusal_compiled ${_target})
      endif()
      cursiv_generator_build(${_target} ${_engine})
      target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
      # Wide invalid grammars need diagnostics without retaining debug types.
      target_compile_options(${_target} PRIVATE -g0)
    endforeach()
  endforeach()

  if(_refusal_compiled)
    list(JOIN _refusal_compiled "," _refusal_compiled)
    add_test(NAME refusals_compile COMMAND ${CMAKE_COMMAND}
        "-DBUILD=${CMAKE_BINARY_DIR}"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DCONFIG=$<CONFIG>"
        "-DTARGETS=${_refusal_compiled}"
        "-DLANES=$<TARGET_FILE:refusal_lanes>"
        "-DOUT=${_refusals}/compile"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/refusals/compile.cmake)
    set_tests_properties(refusals_compile PROPERTIES
        FIXTURES_SETUP refusals_compile PROCESSORS 4)
  endif()
endif()
