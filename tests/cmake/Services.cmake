# The caret against the table: a named keyword, a k=2 cell's two arms, and the
# state a repair leaves behind an unclosed construct.
cursiv_grammar(caret lsp/completion/CaretGrammar.hpp tcaret::Caret EXTENSIONS .caret ENGINE LR)

cursiv_grammar(repairmsg core/validation/RepairMessageGrammar.hpp tmsg::MessageG)
cursiv_test(validation_test core/validation/validation_test.cpp cursiv::lsp wsCore repairmsgGenerated)

cursiv_test(mcp_test mcp/mcp_test.cpp cursiv::lsp cursiv::mcp wsLsp)
get_target_property(_wsGenDir wsGenerated CURSIV_GENERATED_DIR)
target_compile_definitions(mcp_test PRIVATE
    CURSIV_WORKSPACE_LANG="${CMAKE_CURRENT_SOURCE_DIR}/languages/workspace"
    CURSIV_WORKSPACE_GENERATED="${_wsGenDir}")

# The MCP stdio session end to end: spawn `--mcp`, handshake, tools, one rule.
find_program(CURSIV_NODE node)
if(CURSIV_NODE AND TARGET domainmodel_lsp AND NOT CMAKE_CROSSCOMPILING)
  add_test(NAME mcp_session COMMAND ${CURSIV_NODE}
      ${CURSIV_SOURCE_ROOT}/tools/mcp-session.mjs $<TARGET_FILE:domainmodel_lsp>
      --folder ${CURSIV_SOURCE_ROOT}/examples/domainmodel/example
      --call rule "{\"name\":\"EntityRule\",\"languageId\":\"domain-model\"}")
  set_tests_properties(mcp_session PROPERTIES TIMEOUT 60)
  # The tunnel, over a real LSP session -- and the acceptance criterion of the
  # whole storey: an unsaved buffer readable through the MCP door.
  # A request is answered from the notifications sent before it: the client
  # types and asks, and the answer is the document it wrote.
  add_test(NAME lsp_order_session COMMAND ${CURSIV_NODE}
      ${CURSIV_SOURCE_ROOT}/tools/lsp-order-session.mjs
      $<TARGET_FILE:domainmodel_lsp>
      ${CURSIV_SOURCE_ROOT}/examples/domainmodel/example)
  set_tests_properties(lsp_order_session PROPERTIES TIMEOUT 60)
  add_test(NAME mcp_tunnel_session COMMAND ${CURSIV_NODE}
      ${CURSIV_SOURCE_ROOT}/tools/mcp-tunnel-session.mjs
      $<TARGET_FILE:domainmodel_lsp>
      ${CURSIV_SOURCE_ROOT}/examples/domainmodel/example)
  set_tests_properties(mcp_tunnel_session PROPERTIES TIMEOUT 60)
  # The editor's own transport end to end: handshake, scan, an open tab, the
  # requests, shutdown.
  add_test(NAME lsp_session COMMAND ${CURSIV_NODE}
      ${CURSIV_SOURCE_ROOT}/tools/lsp-session.mjs $<TARGET_FILE:domainmodel_lsp>
      ${CURSIV_SOURCE_ROOT}/examples/domainmodel/example/blog.dmodel
      --folder ${CURSIV_SOURCE_ROOT}/examples/domainmodel/example
      --semantic-tokens --folding)
  set_tests_properties(lsp_session PROPERTIES TIMEOUT 60)
endif()

cursiv_test(navigation_test lsp/navigation_test.cpp cursiv::lsp wsLsp multirefGenerated)

cursiv_test(presentation_test lsp/presentation_test.cpp cursiv::lsp wsCore)
cursiv_test(code_action_test lsp/code_action_test.cpp cursiv::lsp)

cursiv_test(TextDocumentCursor_test core/workspace/TextDocumentCursor_test.cpp cursiv::core)

cursiv_test(server_test lsp/server_test.cpp cursiv::lsp wsCore docgGenerated)
target_compile_definitions(server_test PRIVATE
    CURSIV_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(server_test PROPERTIES TIMEOUT 30)

cursiv_test(completion_test "lsp/completion/completion_test.cpp;lsp/completion/CaretLane.cpp" cursiv::lsp wsCore
    caretGenerated)

cursiv_test(edits_test lsp/edits_test.cpp cursiv::lsp wsLsp)

# The public test module: what a language author writes checks with, exercised
# through the one shipped language that fills every slot.
cursiv_test(ValidationHelper_test support/ValidationHelper_test.cpp wsLsp)
