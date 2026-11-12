# Writes the unit of a test batch from its manifest, one `name<TAB>source` line
# per test: the directives that read each source's headers, then each source
# under a namespace of its own, where its `main` is the test's entry.
cmake_minimum_required(VERSION 3.30)

file(STRINGS "${MANIFEST}" _tests)
set(_headers "")
set(_bodies "")
set(_calls "")
foreach(_line IN LISTS _tests)
  string(REGEX MATCH "^([^\t]+)\t(.+)$" _ "${_line}")
  set(_name "${CMAKE_MATCH_1}")
  set(_source "${CMAKE_MATCH_2}")
  string(MAKE_C_IDENTIFIER "cursiv_test_${_name}" _scope)
  file(STRINGS "${_source}" _directives
      REGEX "^[ \t]*#[ \t]*(include|if|ifdef|ifndef|elif|else|endif)")
  list(JOIN _directives "\n" _directives)
  string(APPEND _headers "${_directives}\n")
  string(APPEND _bodies
      "namespace ${_scope} {\n"
      "#define main cursiv_test_main\n"
      "#include \"${_source}\"\n"
      "#undef main\n")
  # A macro of one source stays out of the next.
  file(STRINGS "${_source}" _macros
      REGEX "^[ \t]*#[ \t]*define[ \t]+[A-Za-z_0-9]+")
  foreach(_macro IN LISTS _macros)
    string(REGEX MATCH "define[ \t]+([A-Za-z_0-9]+)" _ "${_macro}")
    string(APPEND _bodies "#undef ${CMAKE_MATCH_1}\n")
  endforeach()
  string(APPEND _bodies "}  // namespace ${_scope}\n")
  string(APPEND _calls
      "  if (name == \"${_name}\") return ${_scope}::cursiv_test_main();\n")
endforeach()
# Written each time: the unit must be newer than the source that changed.
file(WRITE "${OUT}"
"// Generated test batch.
${_headers}#include <cstdio>
#include <cstdlib>
#include <string_view>
${_bodies}
int main(int argc, char** argv) {
  const char* const named = argc > 1 ? argv[1] : std::getenv(\"CURSIV_TEST\");
  const std::string_view name = named != nullptr ? named : \"\";
${_calls}  std::fprintf(stderr, \"%s: no test named `%s`\\n\", argv[0],
               named != nullptr ? named : \"\");
  return 2;
}
")
