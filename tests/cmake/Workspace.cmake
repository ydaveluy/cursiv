# A nested document: the fixture whose table has objects, arrays and four
# scalars -- the shape a completion and a colouring are asked about.
cursiv_grammar(docg doc/DocGrammar.hpp fdoc::DocG EXTENSIONS .doc OUT languages/doc/generated
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/languages ENGINE LR)

# One name for a group: the fixture whose column binds to several FROM items.
cursiv_grammar(multiref multiref/MultiRefGrammar.hpp fmulti::MultiG EXTENSIONS .mref
    OUT languages/multiref/generated INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/languages)

# The workspace fixture: the language cursiv's own lanes are written against.
# Names, cross-references, containers and a scope -- every service has
#something to answer for, and no example is in the way.
cursiv_grammar(ws workspace/core/WorkspaceGrammar.hpp ws::Workspace EXTENSIONS .ws
    OUT languages/workspace/generated INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/languages ENGINE LR)

file(GLOB_RECURSE _ws_core CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/languages/workspace/core/*.cpp)
list(FILTER _ws_core EXCLUDE REGEX "/CoreModule[.]cpp$")
add_library(wsCoreObjects OBJECT ${_ws_core})
target_link_libraries(wsCoreObjects PUBLIC cursiv::core)
target_include_directories(wsCoreObjects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/languages>)

file(GLOB_RECURSE _ws_lsp CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/languages/workspace/lsp/*.cpp)
list(FILTER _ws_lsp EXCLUDE REGEX "/LspModule[.]cpp$")
add_library(wsLspObjects OBJECT ${_ws_lsp})
target_link_libraries(wsLspObjects PUBLIC wsCoreObjects cursiv::lsp)
if(BUILD_SHARED_LIBS)
  set_target_properties(wsCoreObjects wsLspObjects PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# The same fixture with its stamp's row forced narrow, as a table whose states
# need the room lays it out: no grammar small enough to build here gets there
# on its own. Its lanes read every record through that layout.
set(CURSIV_NARROW_ROW_BITS 7)
cursiv_grammar(wsnarrow workspace/core/WorkspaceGrammar.hpp ws::Workspace EXTENSIONS .ws
    OUT narrow/workspace/generated INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/languages ENGINE LR
    GENERATOR_ARGS --stamp-row-bits=${CURSIV_NARROW_ROW_BITS})
foreach(_variant IN ITEMS ws wsnarrow)
  add_library(${_variant}Core languages/workspace/core/CoreModule.cpp)
  target_link_libraries(${_variant}Core PUBLIC ${_variant}Generated wsCoreObjects)
  add_library(${_variant}Lsp languages/workspace/lsp/LspModule.cpp)
  target_link_libraries(${_variant}Lsp PUBLIC ${_variant}Core wsLspObjects)
endforeach()

# The runtime's own state: contexts, stacks, ranks, the roof, a reading handed
# over, and records read through a narrow stamp.
cursiv_test(LrRuntime_test engines/lr/LrRuntime_test.cpp wsnarrowCore)
cursiv_test(LrGssForest_test engines/lr/LrGssForest_test.cpp)
cursiv_test(LrGssRope_test engines/lr/LrGssRope_test.cpp)
cursiv_test(LrGssGraph_test engines/lr/LrGssGraph_test.cpp)
target_compile_definitions(LrRuntime_test PRIVATE
    CURSIV_NARROW_ROW_BITS=${CURSIV_NARROW_ROW_BITS})
cursiv_test(edits_narrow_test lsp/edits_test.cpp cursiv::lsp wsnarrowLsp)

# The erased door: a service holds a `Parser&`, not a grammar. And a text
# translated before its tokens are read, on both engines.
cursiv_grammar_pair(translate core/parser/TranslationGrammar.hpp ttrans::Lr
    ttrans::Ll)
cursiv_test(parser_test core/parser/parser_test.cpp wsCore translatelrGenerated
    translatellGenerated)

cursiv_test(services_test core/services/services_test.cpp wsLsp)

# Its grammar is its own: this lane exercises core and links core.
cursiv_grammar(comments core/documentation/CommentsGrammar.hpp tcomment::Comments)

cursiv_test(documentation_test core/documentation/documentation_test.cpp commentsGenerated wsCore)

cursiv_grammar(narrowedsite core/references/NarrowedSiteGrammar.hpp tns::NarrowedSite)
cursiv_grammar(sharedrecord core/references/SharedRecordGrammar.hpp tsr::SharedRecord)

cursiv_test(references_test core/references/references_test.cpp wsCore narrowedsiteGenerated
    multirefGenerated sharedrecordGenerated)

cursiv_test(workspace_test core/workspace/workspace_test.cpp wsCore)
target_compile_definitions(workspace_test PRIVATE
    CURSIV_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}")
