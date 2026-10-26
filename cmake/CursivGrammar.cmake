include_guard(GLOBAL)

set(CURSIV_DEFAULT_ENGINE "LR" CACHE STRING "Default parser engine: LL or LR")
set_property(CACHE CURSIV_DEFAULT_ENGINE PROPERTY STRINGS LL LR)
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH _cursiv_root)
set(CURSIV_SOURCE_ROOT "${_cursiv_root}" CACHE INTERNAL "")
set(CURSIV_HOST_TOOLS "" CACHE PATH "An existing native build containing the generators")
if(CURSIV_HOST_TOOLS)
  set(CURSIV_HOST_DIR "${CURSIV_HOST_TOOLS}" CACHE INTERNAL "")
else()
  set(CURSIV_HOST_DIR "${CMAKE_BINARY_DIR}/host-tools" CACHE INTERNAL "")
endif()

function(cursiv_engine_parts engine outMain outLib)
  string(TOLOWER "${engine}" _engine)
  if(NOT _engine MATCHES "^(ll|lr)$")
    message(FATAL_ERROR "cursiv: ENGINE must be LL or LR, got `${engine}`")
  endif()
  cmake_path(SET _main NORMALIZE
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/cursiv/engines/${_engine}/gen/${_engine}_main.cpp")
  set(${outMain} "${_main}" PARENT_SCOPE)
  set(${outLib} "cursiv::${_engine}_gen" PARENT_SCOPE)
endfunction()

function(cursiv_grammar_generator name header ns gns id exts fileNames roots engine)
  string(REPLACE ";" "," _exts "${exts}")
  string(REPLACE ";" "," _fileNames "${fileNames}")
  cursiv_engine_parts("${engine}" _main _lib)
  add_executable(${name}_gen EXCLUDE_FROM_ALL ${_main})
  target_include_directories(${name}_gen PRIVATE ${roots})
  target_link_libraries(${name}_gen PRIVATE cursiv::core ${_lib})
  target_compile_definitions(${name}_gen PRIVATE
      CURSIV_GEN_HEADER="${header}"
      CURSIV_GEN_GRAMMAR=${ns}
      CURSIV_GEN_NS="${gns}"
      CURSIV_GEN_NAME="${name}"
      CURSIV_GEN_ID="${id}"
      CURSIV_GEN_EXTENSIONS="${_exts}"
      CURSIV_GEN_FILE_NAMES="${_fileNames}")
  # Generators run briefly; large grammars need generous compile-time limits.
  target_compile_options(${name}_gen PRIVATE -O0 -pipe
      $<$<CXX_COMPILER_ID:Clang>:-fbracket-depth=16384>
      $<$<CXX_COMPILER_ID:GNU>:-fconstexpr-ops-limit=2147483647>
      $<$<CXX_COMPILER_ID:GNU>:--param=ggc-min-expand=200>
      $<$<CXX_COMPILER_ID:GNU>:--param=ggc-min-heapsize=1000000>)
  # The cross build always invokes flat Release host paths.
  set_target_properties(${name}_gen PROPERTIES
      CXX_SCAN_FOR_MODULES OFF RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/gen
      RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/gen)
endfunction()

function(cursiv_host_manifest line)
  file(APPEND "${CMAKE_BINARY_DIR}/host-tools.cmake" "${line}\n")
endfunction()

# One native build for all generators, including languages declared by consumers
# after FetchContent_MakeAvailable. A single build command avoids concurrent Ninja runs.
function(cursiv_host_tools)
  get_property(_targets GLOBAL PROPERTY CURSIV_HOST_GENERATORS)
  list(TRANSFORM _targets PREPEND "${CURSIV_HOST_DIR}/gen/" OUTPUT_VARIABLE _files)
  if(NOT CURSIV_HOST_TOOLS)
    FetchContent_GetProperties(lsp_framework SOURCE_DIR _lsp)
    execute_process(COMMAND ${CMAKE_COMMAND}
        -S ${CURSIV_SOURCE_ROOT} -B ${CURSIV_HOST_DIR} -G ${CMAKE_GENERATOR}
        -DCMAKE_BUILD_TYPE=Release
        -DCURSIV_MIMALLOC=OFF
        -DCURSIV_WARNINGS_AS_ERRORS=${CURSIV_WARNINGS_AS_ERRORS}
        -DCURSIV_HOST_MANIFEST=${CMAKE_BINARY_DIR}/host-tools.cmake
        -DFETCHCONTENT_SOURCE_DIR_LSP_FRAMEWORK=${_lsp}
        COMMAND_ERROR_IS_FATAL ANY)
  endif()
  add_custom_target(cursiv_host_tools
      COMMAND ${CMAKE_COMMAND} --build ${CURSIV_HOST_DIR} --config Release --target ${_targets}
      BYPRODUCTS ${_files}
      COMMENT "cursiv: building the host generators"
      VERBATIM)
  add_dependencies(lspgen cursiv_host_tools)
endfunction()

if(CMAKE_CROSSCOMPILING AND TARGET lspgen)
  file(WRITE "${CMAKE_BINARY_DIR}/host-tools.cmake" "")
  set_property(GLOBAL APPEND PROPERTY CURSIV_HOST_GENERATORS lspgen)
  set_target_properties(lspgen PROPERTIES
      IMPORTED_LOCATION "${CURSIV_HOST_DIR}/gen/lspgen")
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL cursiv_host_tools)
elseif(TARGET lspgen)
  set_target_properties(lspgen PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/gen
      RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/gen)
endif()

function(cursiv_package_language name)
  if(CMAKE_CROSSCOMPILING AND CURSIV_WASI_THREADS
     AND TARGET ${name}_lsp AND EXISTS "${PROJECT_SOURCE_DIR}/vscode")
    # Keep notices beside the built modules, including with local dependency
    # overrides, so both CMake and the repository's packaging scripts use them.
    FetchContent_GetProperties(lsp_framework SOURCE_DIR _lsp_source)
    set(_licenses "${CMAKE_BINARY_DIR}/cursiv-licenses")
    file(MAKE_DIRECTORY "${_licenses}")
    configure_file("${CURSIV_SOURCE_ROOT}/LICENSE"
        "${_licenses}/cursiv.LICENSE" COPYONLY)
    configure_file("${_lsp_source}/LICENSE"
        "${_licenses}/lsp-framework.LICENSE" COPYONLY)
    configure_file("${CURSIV_SOURCE_ROOT}/cmake/patches/mimalloc.LICENSE"
        "${_licenses}/mimalloc.LICENSE" COPYONLY)

    # Refresh the module, runner and notices packaged by the VS Code extension.
    add_custom_command(TARGET ${name}_lsp POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_SOURCE_DIR}/vscode/server"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:${name}_lsp> "${PROJECT_SOURCE_DIR}/vscode/server/${name}_lsp.wasm"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CURSIV_SOURCE_ROOT}/tools/wasi-run.mjs"
              "${_licenses}/cursiv.LICENSE"
              "${_licenses}/lsp-framework.LICENSE"
              "${_licenses}/mimalloc.LICENSE"
              "${PROJECT_SOURCE_DIR}/vscode/server"
      COMMENT "${name}: refreshing vscode/server/${name}_lsp.wasm"
      VERBATIM)
  endif()
endfunction()

# Shared implementation: test languages get a default output path and cheaper compilation.
function(_cursiv_generate_language test_only)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "SEPARATE_UNITS" "GRAMMAR;NAMESPACE;ID;OUT;BASE;ENGINE"
      "EXTENSIONS;FILE_NAMES;INCLUDE_DIRS;GENERATOR_ARGS")
  if(NOT "${ARG_UNPARSED_ARGUMENTS}" STREQUAL "")
    message(FATAL_ERROR "cursiv_generate_language: unknown arguments ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  if("${ARG_ID}" STREQUAL "" OR "${ARG_GRAMMAR}" STREQUAL ""
     OR "${ARG_NAMESPACE}" STREQUAL "")
    message(FATAL_ERROR "cursiv_generate_language: ID, GRAMMAR and NAMESPACE are required")
  endif()
  if(test_only AND "${ARG_OUT}" STREQUAL "")
    set(ARG_OUT generated/${ARG_ID}/generated)
  endif()
  if("${ARG_OUT}" STREQUAL "")
    message(FATAL_ERROR "cursiv_generate_language: OUT is required")
  endif()
  if("${ARG_ENGINE}" STREQUAL "")
    set(ARG_ENGINE ${CURSIV_DEFAULT_ENGINE})
  endif()
  string(TOUPPER "${ARG_ENGINE}" ARG_ENGINE)
  cursiv_engine_parts("${ARG_ENGINE}" _engineMain _engineLib)
  cmake_path(GET _engineMain PARENT_PATH _engineDir)
  # Quoted includes search beside the generator before the language's include roots.
  if(EXISTS ${_engineDir}/${ARG_GRAMMAR})
    message(FATAL_ERROR
        "cursiv: GRAMMAR ${ARG_GRAMMAR} is named like a source of the "
        "${ARG_ENGINE} generator, which would include that source instead -- "
        "rename the header, or spell it from a directory above it")
  endif()
  if(NOT ARG_NAMESPACE MATCHES "^(.+)::([^:]+)$")
    message(FATAL_ERROR "cursiv: NAMESPACE must be qualified (${ARG_NAMESPACE})")
  endif()
  set(_gns ${CMAKE_MATCH_1}::generated::${CMAKE_MATCH_2})
  set(_leaf ${CMAKE_MATCH_2})
  set(_name ${ARG_ID})

  # Relative OUT paths live in the build tree; include roots preserve their source layout.
  set(_srcRoot "")
  if(NOT IS_ABSOLUTE ${ARG_OUT})
    cmake_path(GET ARG_OUT PARENT_PATH _relRoot)
    cmake_path(GET _relRoot PARENT_PATH _relRoot)
    set(_srcRoot ${CMAKE_CURRENT_SOURCE_DIR}/${_relRoot})
    set(ARG_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated-src/${ARG_OUT})
  endif()
  cmake_path(GET ARG_OUT PARENT_PATH _root)
  cmake_path(GET _root PARENT_PATH _root)
  if(NOT "${ARG_BASE}" STREQUAL "")
    if(NOT TARGET ${ARG_BASE}Generated)
      message(FATAL_ERROR "cursiv_generate_language: declare BASE ${ARG_BASE} before this language")
    endif()
    get_target_property(_baseRoots ${ARG_BASE}Generated INTERFACE_INCLUDE_DIRECTORIES)
  endif()
  set(_roots ${_root} ${_srcRoot} ${CMAKE_CURRENT_SOURCE_DIR} ${_baseRoots})
  foreach(_include IN LISTS ARG_INCLUDE_DIRS)
    if(NOT _include MATCHES "^\\$<")
      cmake_path(ABSOLUTE_PATH _include BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
    endif()
    list(APPEND _roots "${_include}")
  endforeach()
  list(REMOVE_DUPLICATES _roots)
  set(_dir ${ARG_OUT}/${_leaf})

  set(_parserUnits ParserStates.cpp)
  set(_headers Table.hpp Parser.hpp AstReflection.hpp Grammar.hpp GrammarSource.hpp Module.hpp GRAMMAR.txt)
  if(ARG_ENGINE STREQUAL "LL")
    foreach(_part RANGE 0 7)
      list(APPEND _parserUnits ParserRules${_part}.cpp)
    endforeach()
    list(APPEND _headers ParserRules.hpp)
  endif()
  set(_units ${_parserUnits} AstReflection.cpp Grammar.cpp Module.cpp)
  set(_written ${_units} ${_headers})
  list(TRANSFORM _written PREPEND "${_dir}/")
  list(TRANSFORM _units PREPEND "${_dir}/" OUTPUT_VARIABLE _generatedSources)
  list(TRANSFORM _parserUnits PREPEND "${_dir}/" OUTPUT_VARIABLE _parserSources)

  if(CMAKE_CROSSCOMPILING)
    cursiv_host_manifest(
        "cursiv_grammar_generator(${_name} \"${ARG_GRAMMAR}\" \"${ARG_NAMESPACE}\" \"${_gns}\" \"${ARG_ID}\" \"${ARG_EXTENSIONS}\" \"${ARG_FILE_NAMES}\" \"${_roots}\" \"${ARG_ENGINE}\")")
    foreach(_setting IN ITEMS STANDARD STANDARD_REQUIRED EXTENSIONS)
      if(DEFINED CMAKE_CXX_${_setting})
        cursiv_host_manifest("set_property(TARGET ${_name}_gen PROPERTY CXX_${_setting} \"${CMAKE_CXX_${_setting}}\")")
      elseif(_setting STREQUAL "EXTENSIONS")
        cursiv_host_manifest("set_property(TARGET ${_name}_gen PROPERTY CXX_EXTENSIONS ON)")
      endif()
    endforeach()
    set(_gen ${CURSIV_HOST_DIR}/gen/${_name}_gen)
    set_property(GLOBAL APPEND PROPERTY CURSIV_HOST_GENERATORS ${_name}_gen)
    set(_dep ${_gen})
  else()
    cursiv_grammar_generator(${_name} "${ARG_GRAMMAR}" ${ARG_NAMESPACE} ${_gns}
        "${ARG_ID}" "${ARG_EXTENSIONS}" "${ARG_FILE_NAMES}" "${_roots}" "${ARG_ENGINE}")
    set(_gen $<TARGET_FILE:${_name}_gen>)
    set(_dep ${_name}_gen)
  endif()

  # Compiler dependency files track grammar headers through the generator executable.
  add_custom_command(OUTPUT ${_written}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${_dir}
      COMMAND ${_gen} ${ARG_GENERATOR_ARGS} ${_dir}
      DEPENDS ${_dep}
      COMMENT "${_name}: writing ${_dir}"
      VERBATIM)
  add_custom_target(${_name}_generate DEPENDS ${_written})
  if(test_only AND NOT ARG_SEPARATE_UNITS)
    # Compile each test language once: its parser, metadata and reflection
    # share the grammar's templates and headers in the same translation unit.
    set(_testParser "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${_name}Generated.dir/Parser.cpp")
    set(_testSource "// Generated test language compilation unit.\n")
    foreach(_unit IN LISTS _parserUnits)
      string(APPEND _testSource "#include \"${_dir}/${_unit}\"\n")
    endforeach()
    string(APPEND _testSource
        "#include \"${_dir}/AstReflection.cpp\"\n"
        "#include \"${_dir}/Grammar.cpp\"\n"
        "#include \"${_dir}/Module.cpp\"\n")
    file(GENERATE OUTPUT "${_testParser}" CONTENT "${_testSource}")
    set_source_files_properties("${_testParser}" PROPERTIES GENERATED TRUE)
    set(_generatedSources "${_testParser}")
    set(_parserSources "${_testParser}")
  endif()
  add_library(${_name}Generated STATIC ${_generatedSources})
  if(test_only)
    add_dependencies(${_name}Generated ${_name}_generate)
  endif()
  if(BUILD_SHARED_LIBS AND NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
    set_property(TARGET ${_name}Generated PROPERTY POSITION_INDEPENDENT_CODE ON)
  endif()
  if(CMAKE_CROSSCOMPILING)
    add_dependencies(${_name}_generate cursiv_host_tools)
    add_dependencies(${_name}Generated cursiv_host_tools)
  endif()
  set_target_properties(${_name}Generated PROPERTIES CXX_SCAN_FOR_MODULES OFF)
  set(_parserOptimization -O2)
  if(test_only)
    set(_parserOptimization -O0)
    if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
      set(_parserOptimization -O1)
    endif()
    # WASI engines limit function size and locals; unoptimised generated
    # parsers can exceed those limits even in otherwise Debug builds.
    target_compile_options(${_name}Generated PRIVATE
        $<$<CONFIG:Release>:${_parserOptimization}>
        $<$<PLATFORM_ID:WASI>:-O1>)
  endif()
  # WASI needs tail calls between parser groups, but not for by-value C++ calls.
  set_property(SOURCE ${_parserSources} PROPERTY COMPILE_OPTIONS
      $<$<CONFIG:Release>:${_parserOptimization}>
      $<$<PLATFORM_ID:WASI>:-mtail-call>
      $<$<PLATFORM_ID:WASI>:-fno-optimize-sibling-calls>)
  target_include_directories(${_name}Generated PUBLIC ${_roots})
  target_link_libraries(${_name}Generated PUBLIC cursiv::core)
  target_compile_definitions(${_name}Generated PUBLIC CURSIV_ENGINE_${ARG_ENGINE}=1)
  set_target_properties(${_name}Generated PROPERTIES
      CURSIV_GENERATED_DIR ${_dir}
      CURSIV_GENERATED_NAMESPACE ${_gns}
      CURSIV_GRAMMAR_HEADER ${ARG_GRAMMAR}
      CURSIV_GENERATED_PARSER_SOURCES "${_parserSources}"
      CURSIV_ENGINE ${ARG_ENGINE})
  if(NOT "${ARG_BASE}" STREQUAL "")
    target_link_libraries(${_name}Generated PUBLIC ${ARG_BASE}Generated)
  endif()
endfunction()

# Public entry points; see docs/reference/cmake.md for arguments and targets.
function(cursiv_generate_language)
  _cursiv_generate_language(FALSE ${ARGN})
endfunction()

function(cursiv_test_language)
  _cursiv_generate_language(TRUE ${ARGN})
endfunction()
