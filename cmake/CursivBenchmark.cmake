# One small executable per language, using the same driver as language_bench.
#   cursiv_add_benchmark(json SOURCES json/bench/main.cpp)
# The language's LSP module is shared with its server, without its main.cpp.
function(cursiv_add_benchmark name)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES")
  if(NOT "${ARG_UNPARSED_ARGUMENTS}" STREQUAL ""
     OR NOT "${ARG_KEYWORDS_MISSING_VALUES}" STREQUAL ""
     OR "${ARG_SOURCES}" STREQUAL "")
    message(FATAL_ERROR "cursiv_add_benchmark: SOURCES is required")
  endif()
  if(NOT TARGET ${name}Lsp)
    message(FATAL_ERROR "cursiv_add_benchmark: declare ${name}Lsp before ${name}_bench")
  endif()
  add_executable(${name}_bench ${ARG_SOURCES})
  target_link_libraries(${name}_bench PRIVATE cursiv::bench ${name}Lsp)
  # All selected languages' benchmarks can be refreshed with one command.
  if(NOT TARGET bench)
    add_custom_target(bench)
  endif()
  add_dependencies(bench ${name}_bench)
endfunction()
