# Random grammars checked against an Earley parse. Host only: the build
# generates a parser for each seed.
if(NOT CMAKE_CROSSCOMPILING)
  add_subdirectory(oracle)
endif()
