include_guard(GLOBAL)

set(CURSIV_DEFAULT_ENGINE "LR" CACHE STRING "Default parser engine: LL or LR")
set_property(CACHE CURSIV_DEFAULT_ENGINE PROPERTY STRINGS LL LR)
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH _cursiv_root)
set(CURSIV_SOURCE_ROOT "${_cursiv_root}" CACHE INTERNAL "")
# How the host names an executable, whatever the build targets.
if(CMAKE_HOST_WIN32)
  set(CURSIV_HOST_EXE .exe)
else()
  set(CURSIV_HOST_EXE "")
endif()
set(CURSIV_HOST_TOOLS "" CACHE PATH "An existing native build containing the generators")
# The units a generated parser spreads over, as many as the generators'
# `kParserUnits`: the build names them before a generator runs.
set(_cursiv_units_header "${_cursiv_root}/src/engines/cursiv/shared/gen/CommonArtifacts.hpp")
file(STRINGS "${_cursiv_units_header}" _cursiv_units
     REGEX "^inline constexpr unsigned kParserUnits = [0-9]+;$")
if(NOT _cursiv_units MATCHES "kParserUnits = ([0-9]+)")
  message(FATAL_ERROR "cursiv: no `kParserUnits` in ${_cursiv_units_header}")
endif()
set(CURSIV_PARSER_UNITS "${CMAKE_MATCH_1}" CACHE INTERNAL "")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_cursiv_units_header}")
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
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/engines/cursiv/${_engine}/gen/${_engine}_main.cpp")
  set(${outMain} "${_main}" PARENT_SCOPE)
  set(${outLib} "cursiv::${_engine}_gen" PARENT_SCOPE)
endfunction()

# What a target holding generators links and is compiled with. Generators run
# briefly; large grammars need generous compile-time limits.
function(cursiv_generator_build target engine)
  cursiv_engine_parts("${engine}" _main _lib)
  target_link_libraries(${target} PRIVATE cursiv::core ${_lib})
  target_compile_options(${target} PRIVATE -O0 -pipe
      $<$<CXX_COMPILER_ID:Clang,AppleClang>:-fbracket-depth=16384>
      $<$<CXX_COMPILER_ID:GNU>:-fconstexpr-ops-limit=2147483647>
      $<$<CXX_COMPILER_ID:GNU>:--param=ggc-min-expand=200>
      $<$<CXX_COMPILER_ID:GNU>:--param=ggc-min-heapsize=1000000>)
  set_target_properties(${target} PROPERTIES CXX_SCAN_FOR_MODULES OFF)
endfunction()

function(cursiv_grammar_generator name header ns gns id exts fileNames roots engine)
  string(REPLACE ";" "," _exts "${exts}")
  string(REPLACE ";" "," _fileNames "${fileNames}")
  cursiv_engine_parts("${engine}" _main _lib)
  add_executable(${name}_gen EXCLUDE_FROM_ALL ${_main})
  target_include_directories(${name}_gen PRIVATE ${roots})
  cursiv_generator_build(${name}_gen "${engine}")
  target_compile_definitions(${name}_gen PRIVATE
      CURSIV_GEN_HEADER="${header}"
      CURSIV_GEN_GRAMMAR=${ns}
      CURSIV_GEN_NS="${gns}"
      CURSIV_GEN_NAME="${name}"
      CURSIV_GEN_ID="${id}"
      CURSIV_GEN_EXTENSIONS="${_exts}"
      CURSIV_GEN_FILE_NAMES="${_fileNames}")
  # The cross build always invokes flat Release host paths.
  set_target_properties(${name}_gen PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/gen
      RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/gen)
endfunction()

function(cursiv_host_manifest line)
  file(APPEND "${CMAKE_BINARY_DIR}/host-tools.cmake" "${line}\n")
endfunction()

# One native build for all generators, including languages declared by consumers
# after FetchContent_MakeAvailable. A single build command avoids concurrent Ninja runs.
function(cursiv_host_tools)
  get_property(_targets GLOBAL PROPERTY CURSIV_HOST_GENERATORS)
  # A generator reads its grammar with the final compile definitions and
  # include directories of the directory that declares the language.
  foreach(_target IN LISTS _targets)
    get_property(_directory GLOBAL PROPERTY CURSIV_HOST_DIRECTORY_${_target})
    if(NOT _directory)
      continue()
    endif()
    get_directory_property(_definitions DIRECTORY "${_directory}" COMPILE_DEFINITIONS)
    get_directory_property(_includes DIRECTORY "${_directory}" INCLUDE_DIRECTORIES)
    if(_definitions)
      cursiv_host_manifest("target_compile_definitions(${_target} PRIVATE [==[${_definitions}]==])")
    endif()
    if(_includes)
      cursiv_host_manifest("target_include_directories(${_target} BEFORE PRIVATE [==[${_includes}]==])")
    endif()
  endforeach()
  list(TRANSFORM _targets REPLACE "(.+)" "${CURSIV_HOST_DIR}/gen/\\1${CURSIV_HOST_EXE}" OUTPUT_VARIABLE _files)
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
      IMPORTED_LOCATION "${CURSIV_HOST_DIR}/gen/lspgen${CURSIV_HOST_EXE}")
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

    # Copy the module, runner and notices into the extension at each link of
    # the server; the runner and notices go with a folder's first server.
    set(_server "${PROJECT_SOURCE_DIR}/vscode/server")
    set(_runtime "${CURSIV_SOURCE_ROOT}/tools/wasi-run.mjs"
        "${_licenses}/cursiv.LICENSE"
        "${_licenses}/lsp-framework.LICENSE"
        "${_licenses}/mimalloc.LICENSE")
    set(_copies "${_server}/${name}_lsp.wasm")
    get_property(_packaged GLOBAL PROPERTY CURSIV_PACKAGED_SERVER_DIRS)
    if(NOT _server IN_LIST _packaged)
      set_property(GLOBAL APPEND PROPERTY CURSIV_PACKAGED_SERVER_DIRS "${_server}")
      foreach(_file IN LISTS _runtime)
        cmake_path(GET _file FILENAME _leaf)
        list(APPEND _copies "${_server}/${_leaf}")
      endforeach()
    endif()
    add_custom_command(TARGET ${name}_lsp POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_server}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:${name}_lsp> "${_server}/${name}_lsp.wasm"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_runtime} "${_server}"
      BYPRODUCTS ${_copies}
      COMMENT "${name}: refreshing vscode/server/${name}_lsp.wasm"
      VERBATIM)
    set_property(TARGET ${name}_lsp APPEND PROPERTY LINK_DEPENDS ${_runtime})
  endif()
endfunction()

# Shared implementation: test languages get a default output path and cheaper compilation.
function(_cursiv_generate_language test_only)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "SEPARATE_UNITS"
      "GRAMMAR;NAMESPACE;ID;OUT;BASE;ENGINE;TEXTMATE"
      "EXTENSIONS;FILE_NAMES;INCLUDE_DIRS;GENERATOR_ARGS")
  if(NOT "${ARG_UNPARSED_ARGUMENTS}" STREQUAL "")
    message(FATAL_ERROR "cursiv_generate_language: unknown arguments ${ARG_UNPARSED_ARGUMENTS}")
  endif()
  # A one-value keyword given nothing is a mistake, never its default. A list
  # keyword may be empty.
  set(_missing ${ARG_KEYWORDS_MISSING_VALUES})
  list(REMOVE_ITEM _missing EXTENSIONS FILE_NAMES INCLUDE_DIRS GENERATOR_ARGS)
  if(_missing)
    message(FATAL_ERROR "cursiv_generate_language: no value after ${_missing}")
  endif()
  if(NOT test_only AND DEFINED ARG_INCLUDE_DIRS)
    message(FATAL_ERROR "cursiv_generate_language: unknown arguments INCLUDE_DIRS: "
        "a language's grammar is always found from core/")
  endif()
  if("${ARG_ID}" STREQUAL "" OR "${ARG_GRAMMAR}" STREQUAL ""
     OR "${ARG_NAMESPACE}" STREQUAL "")
    message(FATAL_ERROR "cursiv_generate_language: ID, GRAMMAR and NAMESPACE are required")
  endif()
  # Each list reaches the generator as one quoted string of comma-separated
  # entries, which an entry holding one of these would break.
  foreach(_list IN ITEMS EXTENSIONS FILE_NAMES)
    foreach(_entry IN LISTS ARG_${_list})
      if(_entry MATCHES "[,\"\\]")
        message(FATAL_ERROR "cursiv_generate_language: ${_list} entry `${_entry}` "
            "holds a comma, a quote or a backslash, which this list cannot carry")
      endif()
    endforeach()
  endforeach()
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
  # A language's grammar and AST are found from `core` alone, so the packages
  # beside it stay out of reach. Test languages name their own roots.
  if(test_only)
    set(_sourceRoots ${_srcRoot} ${CMAKE_CURRENT_SOURCE_DIR})
  else()
    set(_sourceRoots ${CMAKE_CURRENT_SOURCE_DIR}/core)
    if(NOT EXISTS "${_sourceRoots}/${ARG_GRAMMAR}")
      message(FATAL_ERROR "cursiv_generate_language: GRAMMAR ${ARG_GRAMMAR} is "
          "not under ${_sourceRoots}")
    endif()
  endif()
  set(_roots ${_root} ${_sourceRoots} ${_baseRoots})
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
    set(_part ParserRules)
  else()
    set(_part ParserGroups)
  endif()
  # One file per unit the generators write.
  math(EXPR _lastUnit "${CURSIV_PARSER_UNITS} - 1")
  foreach(_unit RANGE 0 ${_lastUnit})
    list(APPEND _parserUnits ${_part}${_unit}.cpp)
  endforeach()
  list(APPEND _headers ${_part}.hpp)
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
    set(_gen ${CURSIV_HOST_DIR}/gen/${_name}_gen${CURSIV_HOST_EXE})
    set_property(GLOBAL APPEND PROPERTY CURSIV_HOST_GENERATORS ${_name}_gen)
    set_property(GLOBAL PROPERTY CURSIV_HOST_DIRECTORY_${_name}_gen "${CMAKE_CURRENT_SOURCE_DIR}")
    set(_dep ${_gen})
  else()
    cursiv_grammar_generator(${_name} "${ARG_GRAMMAR}" ${ARG_NAMESPACE} ${_gns}
        "${ARG_ID}" "${ARG_EXTENSIONS}" "${ARG_FILE_NAMES}" "${_roots}" "${ARG_ENGINE}")
    set(_gen $<TARGET_FILE:${_name}_gen>)
    set(_dep ${_name}_gen)
  endif()

  # The generator rewrites the keywords of this hand-written file, which is
  # no build output, so a clean never deletes it.
  set(_textmate "")
  if(DEFINED ARG_TEXTMATE)
    cmake_path(ABSOLUTE_PATH ARG_TEXTMATE BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        NORMALIZE)
    set(_textmate "--textmate=${ARG_TEXTMATE}")
  endif()

  # Compiler dependency files track grammar headers through the generator executable.
  add_custom_command(OUTPUT ${_written}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${_dir}
      COMMAND ${_gen} ${ARG_GENERATOR_ARGS} ${_textmate} ${_dir}
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
  # Keep generated WASI Debug functions within the engines' local limits. WASI
  # needs tail calls between parser groups only: Clang's spelling is below,
  # another compiler adds its own.
  set_property(SOURCE ${_parserSources} PROPERTY COMPILE_OPTIONS
      $<$<CONFIG:Release>:${_parserOptimization}>
      $<$<AND:$<PLATFORM_ID:WASI>,$<CONFIG:Debug>>:-O1>
      $<$<AND:$<PLATFORM_ID:WASI>,$<CXX_COMPILER_ID:Clang>>:-mtail-call>
      $<$<AND:$<PLATFORM_ID:WASI>,$<CXX_COMPILER_ID:Clang>>:-fno-optimize-sibling-calls>)
  target_include_directories(${_name}Generated PUBLIC ${_roots})
  string(TOLOWER "${ARG_ENGINE}" _engineTarget)
  target_link_libraries(${_name}Generated PUBLIC
      cursiv::engine::${_engineTarget})
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
