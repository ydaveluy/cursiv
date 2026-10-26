# Test parsers repeatedly instantiate the same runtime/STL templates. Share
# those instantiations across four languages, retaining every generated module
# and its public target. Products keep their independent compilation units.
function(cursiv_batch_test_languages)
  get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  set(_namespaces "")
  foreach(_target IN LISTS _targets)
    get_target_property(_ns ${_target} CURSIV_GENERATED_NAMESPACE)
    if(_ns)
      list(APPEND _namespaces "${_ns}")
    endif()
  endforeach()

  foreach(_engine IN ITEMS LL LR)
    set(_batch "")
    foreach(_target IN LISTS _targets)
      get_target_property(_backend ${_target} CURSIV_ENGINE)
      if(NOT _backend STREQUAL _engine)
        continue()
      endif()
      get_target_property(_sources ${_target} SOURCES)
      list(LENGTH _sources _count)
      get_target_property(_header ${_target} CURSIV_GRAMMAR_HEADER)
      get_target_property(_ns ${_target} CURSIV_GENERATED_NAMESPACE)
      set(_same_namespace "${_namespaces}")
      list(FILTER _same_namespace INCLUDE REGEX "^${_ns}$")
      list(LENGTH _same_namespace _duplicates)
      # Separate-unit, bare-header and same-namespace fixtures deliberately
      # exercise isolation. Keep the largest fixtures alone to bound memory.
      if(NOT _count EQUAL 1 OR NOT _sources MATCHES "/Parser.cpp$"
         OR NOT _header MATCHES "/" OR _duplicates GREATER 1
         OR _target MATCHES "^(semanticll|charactersll|characterslr)Generated$")
        continue()
      endif()
      list(APPEND _batch ${_target})
      list(LENGTH _batch _size)
      if(_size EQUAL 4)
        cursiv_test_language_batch(${_engine} ${_batch})
        set(_batch "")
      endif()
    endforeach()
    list(LENGTH _batch _size)
    if(_size GREATER 1)
      cursiv_test_language_batch(${_engine} ${_batch})
    endif()
  endforeach()
endfunction()

function(cursiv_test_language_batch engine)
  list(GET ARGN 0 _first)
  set(_name ${_first}Batch)
  set(_source "// Generated test language batch.\n")
  add_library(${_name} OBJECT)
  target_link_libraries(${_name} PRIVATE cursiv::core)
  target_compile_definitions(${_name} PRIVATE CURSIV_ENGINE_${engine}=1)
  foreach(_target IN LISTS ARGN)
    get_target_property(_sources ${_target} SOURCES)
    get_target_property(_roots ${_target} INCLUDE_DIRECTORIES)
    target_include_directories(${_name} PRIVATE ${_roots})
    string(APPEND _source "#include \"${_sources}\"\n")
    string(REGEX REPLACE "Generated$" "_generate" _generation ${_target})
    add_dependencies(${_name} ${_generation})
    # One object can sit in several static archives. The linker extracts it
    # once, resolving all modules in the batch together.
    set_property(TARGET ${_target} PROPERTY SOURCES $<TARGET_OBJECTS:${_name}>)
    if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|WASI)$")
      # Reference the shared object rather than copying it into four archives.
      set_property(TARGET ${_target} APPEND PROPERTY STATIC_LIBRARY_OPTIONS --thin)
      target_link_options(${_target} INTERFACE "LINKER:--gc-sections")
    endif()
  endforeach()
  set(_file "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${_name}.cpp")
  file(GENERATE OUTPUT "${_file}" CONTENT "${_source}")
  target_sources(${_name} PRIVATE "${_file}")
  target_compile_options(${_name} PRIVATE
      $<$<AND:$<CONFIG:Release>,$<NOT:$<PLATFORM_ID:WASI>>>:-O0>
      $<$<PLATFORM_ID:WASI>:-O1;-mtail-call;-fno-optimize-sibling-calls>)
  if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|WASI)$")
    # A test using one module need not retain the other modules in its batch.
    target_compile_options(${_name} PRIVATE -ffunction-sections -fdata-sections)
  endif()
endfunction()

cursiv_batch_test_languages()
