# Generate the parser from the grammar. The build regenerates it when the
# grammar changes.
cursiv_generate_language(ID @CURSIV_NEW_ID@
    GRAMMAR    @CURSIV_NEW_ID@/core/@CURSIV_NEW_CLASS@Grammar.hpp
    NAMESPACE  @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@
    EXTENSIONS @CURSIV_NEW_EXT@
    OUT        @CURSIV_NEW_ID@/generated)

# The language library: every .cpp file under core/.
file(GLOB_RECURSE _core CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/@CURSIV_NEW_ID@/core/*.cpp)
add_library(@CURSIV_NEW_ID@Core ${_core})
target_link_libraries(@CURSIV_NEW_ID@Core PUBLIC @CURSIV_NEW_ID@Generated)

# The command line, the language server and the benchmark are optional:
# delete the cli/, lsp/ or bench/ folder to drop its target.
file(GLOB_RECURSE _cli CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/@CURSIV_NEW_ID@/cli/*.cpp)
if(_cli)
  add_executable(@CURSIV_NEW_ID@_cli ${_cli})
  target_link_libraries(@CURSIV_NEW_ID@_cli
                        PRIVATE @CURSIV_NEW_ID@Core cursiv::cli)
endif()

if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/@CURSIV_NEW_ID@/lsp")
  file(GLOB_RECURSE _lsp CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/@CURSIV_NEW_ID@/lsp/*.cpp)
  list(REMOVE_ITEM _lsp ${CMAKE_CURRENT_SOURCE_DIR}/@CURSIV_NEW_ID@/lsp/main.cpp)
  add_library(@CURSIV_NEW_ID@Lsp ${_lsp})
  target_link_libraries(@CURSIV_NEW_ID@Lsp PUBLIC @CURSIV_NEW_ID@Core cursiv::lsp)
  add_executable(@CURSIV_NEW_ID@_lsp @CURSIV_NEW_ID@/lsp/main.cpp)
  target_link_libraries(@CURSIV_NEW_ID@_lsp PRIVATE @CURSIV_NEW_ID@Lsp)
endif()

if(TARGET @CURSIV_NEW_ID@Lsp AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/@CURSIV_NEW_ID@/bench/main.cpp")
  cursiv_add_benchmark(@CURSIV_NEW_ID@
      SOURCES @CURSIV_NEW_ID@/bench/main.cpp)
endif()

# WASI builds copy the language server into vscode/server/.
cursiv_package_language(@CURSIV_NEW_ID@)
