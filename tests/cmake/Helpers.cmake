function(cursiv_test name src)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE cursiv::test cursiv::engine::ll
      cursiv::engine::lr ${ARGN})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME ${name} COMMAND ${name})
  # Each pool of a wasm test starts a thread per worker, and one per host CPU
  # would take most of a short test's time.
  if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set_tests_properties(${name} PROPERTIES ENVIRONMENT CURSIV_WORKERS=4)
  endif()
endfunction()

# A timeout in an optimised build's seconds: unoptimised code runs the same
# work several times slower, and unoptimised wasm slower again.
function(cursiv_timeout name seconds)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    math(EXPR seconds "${seconds} * 4")
    if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
      math(EXPR seconds "${seconds} * 2")
    endif()
  endif()
  set_tests_properties(${name} PROPERTIES TIMEOUT ${seconds})
endfunction()

function(cursiv_grammar id header ns)
  cursiv_test_language(ID ${id} GRAMMAR ${header} NAMESPACE ${ns} ${ARGN})
  if(CMAKE_CROSSCOMPILING)
    cursiv_host_manifest("set_property(GLOBAL APPEND PROPERTY CURSIV_TEST_GENERATORS ${id}_gen)")
  else()
    set_property(GLOBAL APPEND PROPERTY CURSIV_TEST_GENERATORS ${id}_gen)
  endif()
endfunction()

function(cursiv_grammar_pair id header lr ll)
  cursiv_grammar(${id}lr "${header}" "${lr}" ENGINE LR ${ARGN})
  cursiv_grammar(${id}ll "${header}" "${ll}" ENGINE LL ${ARGN})
endfunction()

# Force optional positions through auxiliary rules, including in host tools.
function(cursiv_aux_grammar id header ns)
  cursiv_grammar(${id} "${header}" "${ns}" ENGINE LR
      GENERATOR_ARGS --optional-gate=0)
endfunction()
