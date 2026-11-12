include_guard(GLOBAL)

# Tests of one source and no setting of their own share one compilation of the
# headers they read, four to an executable that runs the one `CURSIV_TEST`
# names. Each keeps its ctest name, and its own target built on demand.
function(_cursiv_test_batch index)
  set(_members ${ARGN})
  list(LENGTH _members _count)
  if(_count LESS 2)
    return()
  endif()
  set(_batch cursiv_tests_${index})
  set(_unit "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${_batch}.cpp")
  set(_manifest "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${_batch}.txt")
  set(_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/batch_unit.cmake")
  set(_listed "")
  set(_sources "")
  set(_links "")
  foreach(_test IN LISTS _members)
    get_target_property(_source ${_test} SOURCES)
    cmake_path(ABSOLUTE_PATH _source BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        NORMALIZE)
    get_target_property(_libraries ${_test} LINK_LIBRARIES)
    string(APPEND _listed "${_test}\t${_source}\n")
    list(APPEND _sources "${_source}")
    list(APPEND _links ${_libraries})
    set_target_properties(${_test} PROPERTIES
        EXCLUDE_FROM_ALL ON CURSIV_TEST_HOST ${_batch})
    set_property(TEST ${_test} APPEND PROPERTY ENVIRONMENT CURSIV_TEST=${_test})
  endforeach()
  list(REMOVE_DUPLICATES _links)
  file(CONFIGURE OUTPUT "${_manifest}" CONTENT "${_listed}" @ONLY)
  # Written at build time: it holds the directives of each source.
  add_custom_command(OUTPUT "${_unit}"
      COMMAND ${CMAKE_COMMAND} "-DMANIFEST=${_manifest}" "-DOUT=${_unit}"
              -P "${_script}"
      DEPENDS ${_sources} "${_manifest}" "${_script}"
      COMMENT "${_batch}: writing its unit"
      VERBATIM)
  add_executable(${_batch} "${_unit}")
  target_include_directories(${_batch} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${_batch} PRIVATE ${_links})
endfunction()

function(cursiv_batch_tests)
  get_property(_tests GLOBAL PROPERTY CURSIV_TESTS)
  # What a test target holds when nothing but `cursiv_test` set it.
  get_directory_property(_options COMPILE_OPTIONS)
  get_directory_property(_link_options LINK_OPTIONS)
  set(_members "")
  set(_index 0)
  foreach(_test IN LISTS _tests)
    get_target_property(_sources ${_test} SOURCES)
    get_target_property(_roots ${_test} INCLUDE_DIRECTORIES)
    get_target_property(_test_options ${_test} COMPILE_OPTIONS)
    get_target_property(_test_link_options ${_test} LINK_OPTIONS)
    if(NOT _test_link_options)
      set(_test_link_options "")
    endif()
    set(_ordinary TRUE)
    foreach(_property IN ITEMS COMPILE_DEFINITIONS UNITY_BUILD
                              CURSIV_OWN_ALLOCATOR CURSIV_TEST_ALONE)
      get_target_property(_value ${_test} ${_property})
      if(_value)
        set(_ordinary FALSE)
      endif()
    endforeach()
    if(NOT _ordinary OR NOT _sources MATCHES "^[^;$]+[.]cpp$"
       OR NOT _roots STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}"
       OR NOT _test_options STREQUAL "${_options}"
       OR NOT _test_link_options STREQUAL "${_link_options}")
      continue()
    endif()
    list(APPEND _members ${_test})
    list(LENGTH _members _count)
    if(_count EQUAL 4)
      _cursiv_test_batch(${_index} ${_members})
      math(EXPR _index "${_index} + 1")
      set(_members "")
    endif()
  endforeach()
  _cursiv_test_batch(${_index} ${_members})
endfunction()
