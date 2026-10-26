# Behaviour tests do not need the backend cost of full Release optimisation.
add_compile_options($<$<CONFIG:Release>:-O1>)

# Unit tests skip Binaryen; benchmarks in the sibling directory keep it.
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  add_link_options(--no-wasm-opt)
endif()

# include(), not add_subdirectory(): paths stay rooted here.
include(cmake/BatchGenerators.cmake)
foreach(_suite IN ITEMS Helpers Core LrLowering Shared Ll Lr Workspace
                        Torture Services Integration Refusals Regressions)
  include(cmake/${_suite}.cmake)
endforeach()

# Target-specific grammar checks above must remain visible to the batcher.
if(CMAKE_CROSSCOMPILING)
  cursiv_host_manifest("include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/BatchGenerators.cmake\")")
  cursiv_host_manifest("cursiv_batch_test_generators()")
else()
  cursiv_batch_test_generators()
endif()

include(cmake/BatchLanguages.cmake)
