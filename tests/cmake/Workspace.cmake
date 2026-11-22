# Nested documents with objects, arrays and four scalars, for completion and
# colouring.
cursiv_grammar(docg doc/DocGrammar.hpp fdoc::DocG EXTENSIONS .doc OUT languages/doc/generated
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/languages)

# One name for a group: the fixture whose column binds to several FROM items.
cursiv_grammar(multiref multiref/MultiRefGrammar.hpp fmulti::MultiG EXTENSIONS .mref
    OUT languages/multiref/generated INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/languages)

# The fixture language most tests use: names, cross-references, containers and
# a scope, so every service has something to answer.
cursiv_grammar(ws workspace/core/WorkspaceGrammar.hpp ws::Workspace EXTENSIONS .ws
    OUT languages/workspace/generated INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/languages)

file(GLOB_RECURSE _ws_core CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/languages/workspace/core/*.cpp)
list(FILTER _ws_core EXCLUDE REGEX "/CoreModule[.]cpp$")
add_library(wsCoreObjects OBJECT ${_ws_core})
target_link_libraries(wsCoreObjects PUBLIC cursiv::engine)
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

add_library(wsCore languages/workspace/core/CoreModule.cpp)
target_link_libraries(wsCore PUBLIC wsGenerated wsCoreObjects)
add_library(wsLsp languages/workspace/lsp/LspModule.cpp)
target_link_libraries(wsLsp PUBLIC wsCore wsLspObjects)

add_library(editsObjects OBJECT lsp/edits_test.cpp)
target_link_libraries(editsObjects PRIVATE cursiv::test wsLspObjects)
target_include_directories(editsObjects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# A service holds a type-erased `Parser&`; a text is translated before it is
# lexed.
cursiv_grammar(translatelr core/parser/TranslationGrammar.hpp ttrans::Base)
cursiv_test(parser_test core/parser/parser_test.cpp cursiv::lsp wsCore translatelrGenerated)

cursiv_test(services_test core/services/services_test.cpp wsLsp)

cursiv_grammar(comments core/documentation/CommentsGrammar.hpp tcomment::Comments)

cursiv_test(documentation_test core/documentation/documentation_test.cpp cursiv::lsp commentsGenerated wsCore)

cursiv_grammar(narrowedsite core/references/NarrowedSiteGrammar.hpp tns::NarrowedSite)
cursiv_grammar(sharedrecord core/references/SharedRecordGrammar.hpp tsr::SharedRecord)

cursiv_test(references_test core/references/references_test.cpp cursiv::lsp wsCore narrowedsiteGenerated
    multirefGenerated sharedrecordGenerated)

cursiv_test(workspace_test core/workspace/workspace_test.cpp cursiv::lsp wsCore)
target_compile_definitions(workspace_test PRIVATE
    CURSIV_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}")
