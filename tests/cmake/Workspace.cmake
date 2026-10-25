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
add_library(wsCore ${_ws_core})
target_link_libraries(wsCore PUBLIC wsGenerated)
target_include_directories(wsCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/languages>)

file(GLOB_RECURSE _ws_lsp CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/languages/workspace/lsp/*.cpp)
add_library(wsLsp ${_ws_lsp})
target_link_libraries(wsLsp PUBLIC wsCore cursiv::lsp)

# The same fixture with its stamp's row forced narrow, as a table whose states
# need the room lays it out: no grammar small enough to build here gets there
# on its own. Its lanes read every record through that layout.
set(CURSIV_NARROW_ROW_BITS 7)
cursiv_grammar(wsnarrow workspace/core/WorkspaceGrammar.hpp ws::Workspace EXTENSIONS .ws
    OUT narrow/workspace/generated INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/languages ENGINE LR
    GENERATOR_ARGS --stamp-row-bits=${CURSIV_NARROW_ROW_BITS})
add_library(wsnarrowCore ${_ws_core})
target_link_libraries(wsnarrowCore PUBLIC wsnarrowGenerated)
target_include_directories(wsnarrowCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/languages>)
add_library(wsnarrowLsp ${_ws_lsp})
target_link_libraries(wsnarrowLsp PUBLIC wsnarrowCore cursiv::lsp)

# The runtime's own state: contexts, stacks, ranks, the roof, a reading handed
# over, and records read through a narrow stamp.
cursiv_test(LrRuntime_test engines/lr/LrRuntime_test.cpp wsnarrowCore)
cursiv_test(LrGssForest_test engines/lr/LrGssForest_test.cpp)
cursiv_test(LrGssRope_test engines/lr/LrGssRope_test.cpp)
cursiv_test(LrGssGraph_test engines/lr/LrGssGraph_test.cpp)
target_compile_definitions(LrRuntime_test PRIVATE
    CURSIV_NARROW_ROW_BITS=${CURSIV_NARROW_ROW_BITS})
cursiv_test(edits_narrow_test lsp/edits_test.cpp cursiv::lsp wsnarrowLsp)

# The erased door: a service holds a `Parser&`, not a grammar.
cursiv_test(Parser_test core/parser/Parser_test.cpp wsCore)

# Its own operator new, which ThreadSanitizer's runtime also defines.
if(NOT CMAKE_CXX_FLAGS MATCHES "sanitize=thread")
  cursiv_test(RegistrationFailure_test core/services/RegistrationFailure_test.cpp wsCore)
  set_target_properties(RegistrationFailure_test PROPERTIES CURSIV_OWN_ALLOCATOR ON)
endif()

cursiv_test(services_test core/services/services_test.cpp wsLsp)

# Its grammar is its own: this lane exercises core and links core.
cursiv_grammar(comments core/documentation/CommentsGrammar.hpp tcomment::Comments)

cursiv_test(documentation_test core/documentation/documentation_test.cpp commentsGenerated wsCore)

cursiv_grammar(narrowedsite core/references/NarrowedSiteGrammar.hpp tns::NarrowedSite)

cursiv_test(references_test core/references/references_test.cpp wsCore narrowedsiteGenerated multirefGenerated)

cursiv_test(workspace_test core/workspace/workspace_test.cpp wsCore)
target_compile_definitions(workspace_test PRIVATE
    CURSIV_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}")
