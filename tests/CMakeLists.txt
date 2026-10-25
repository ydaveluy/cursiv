# Behaviour tests do not need the backend cost of full Release optimisation.
add_compile_options($<$<CONFIG:Release>:-O1>)

# Unit tests skip Binaryen; benchmarks in the sibling directory keep it.
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  add_link_options(--no-wasm-opt)
endif()

# include(), not add_subdirectory(): paths stay rooted here.
foreach(_suite IN ITEMS Helpers Core LrLowering Shared Ll Lr Workspace
                        Torture Services Integration Refusals Regressions)
  include(cmake/${_suite}.cmake)
endforeach()
