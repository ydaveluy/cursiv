# All examples use the same reusable driver and their language server modules.
add_executable(language_bench language_bench/main.cpp language_bench/Languages.cpp)
target_include_directories(language_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(language_bench PRIVATE cursiv::bench
    arithmeticsLsp domainmodelLsp requirementsLsp statemachineLsp)

add_custom_target(bench DEPENDS language_bench)

# The repair path under measurement: the altered-document lane.
add_executable(recover_bench EXCLUDE_FROM_ALL recover_bench/main.cpp)
target_include_directories(recover_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(recover_bench PRIVATE cursiv::bench arithmeticsCore
    domainmodelCore)

# The same lane over a grammar whose texts fork the LR graph stack, generated
# here at the optimisation the products use, in a binary of its own.
cursiv_generate_language(ID benchgrowth
    GRAMMAR      growth_bench/GrowthGrammar.hpp
    NAMESPACE    tgrowth::G
    OUT          generated/benchgrowth/generated)
set_target_properties(benchgrowthGenerated PROPERTIES EXCLUDE_FROM_ALL ON)
add_executable(growth_bench EXCLUDE_FROM_ALL recover_bench/main.cpp)
target_compile_definitions(growth_bench PRIVATE GROWTH_BENCH)
target_include_directories(growth_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(growth_bench PRIVATE cursiv::bench benchgrowthGenerated)

# What a completion costs in the middle of a large document: conditions that
# read two terminals, generated here at the optimisation the products use.
cursiv_generate_language(ID benchcaret
    GRAMMAR      caret_bench/CaretPhraseGrammar.hpp
    NAMESPACE    tcaretphrase::G
    OUT          generated/benchcaret/generated)
set_target_properties(benchcaretGenerated PROPERTIES EXCLUDE_FROM_ALL ON)
add_executable(caret_bench EXCLUDE_FROM_ALL caret_bench/main.cpp)
target_link_libraries(caret_bench PRIVATE cursiv::bench benchcaretGenerated)

# The repair quality report: faults planted in valid documents, each faulted
# copy compared with its twin, per grammar and per class of fault.
set(CURSIV_RECOVERY_METRIC_LANGUAGES
    arithmetics domainmodel requirements statemachine)
set(_metricSources recovery_metric/main.cpp)
set(_metricLinks cursiv::bench arithmeticsCore domainmodelCore requirementsCore
    statemachineCore)
foreach(_l IN LISTS CURSIV_RECOVERY_METRIC_LANGUAGES)
  list(APPEND _metricSources recovery_metric/${_l}.cpp)
endforeach()
# The two test grammars exist where the suites are configured.
if(TARGET indentGenerated AND TARGET asiGenerated)
  list(APPEND _metricSources recovery_metric/indent.cpp recovery_metric/asi.cpp)
  list(APPEND _metricLinks indentGenerated asiGenerated)
endif()
add_executable(recovery_metric EXCLUDE_FROM_ALL ${_metricSources})
target_link_libraries(recovery_metric PRIVATE ${_metricLinks} cursiv::test)
target_include_directories(recovery_metric PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/tests)
target_compile_definitions(recovery_metric PRIVATE
    CURSIV_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

if(CURSIV_BUILD_TESTS)
  set_target_properties(recovery_metric PROPERTIES EXCLUDE_FROM_ALL OFF)
  # Its smoke lane: arithmetics alone, a few documents, and a check that the
  # report is well formed -- no threshold on what it says.
  add_test(NAME recovery_metric_smoke
      COMMAND ${CMAKE_COMMAND}
          -DMETRIC=$<TARGET_FILE:recovery_metric>
          "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
          -DOUT=${CMAKE_CURRENT_BINARY_DIR}/recovery_metric_smoke
          -P ${CMAKE_CURRENT_SOURCE_DIR}/recovery_metric/smoke.cmake)

  # Its lane: each class stays within one document of the recorded figures.
  add_test(NAME recovery_metric_lane
      COMMAND ${CMAKE_COMMAND}
          -DMETRIC=$<TARGET_FILE:recovery_metric>
          "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
          -DOUT=${CMAKE_CURRENT_BINARY_DIR}/recovery_metric_lane
          -DTSV=${CMAKE_CURRENT_SOURCE_DIR}/recovery_metric/lane.tsv
          -P ${CMAKE_CURRENT_SOURCE_DIR}/recovery_metric/lane.cmake)
endif()
