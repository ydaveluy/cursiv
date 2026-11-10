# The caret against the table: a named keyword, a k=2 cell's two arms, and the
# state a repair leaves behind an unclosed construct.
cursiv_grammar(caret lsp/completion/CaretGrammar.hpp tcaret::Caret EXTENSIONS .caret ENGINE LR)
# A word that spans lines, which a completion edit may not.
cursiv_grammar(block lsp/completion/BlockGrammar.hpp tblock::Block EXTENSIONS .block ENGINE LR)
# Terminals of words beside the data rule reading the same words, in both
# engines.
cursiv_grammar_pair(words lsp/completion/WordsGrammar.hpp twords::Lr twords::Ll)

cursiv_grammar(repairmsg core/validation/RepairMessageGrammar.hpp tmsg::MessageG)
cursiv_test(validation_test core/validation/validation_test.cpp cursiv::lsp wsCore repairmsgGenerated)

cursiv_test(mcp_test mcp/mcp_test.cpp cursiv::lsp cursiv::mcp wsLsp
    wordslrGenerated caretGenerated docgGenerated multirefGenerated)
get_target_property(_wsGenDir wsGenerated CURSIV_GENERATED_DIR)
target_compile_definitions(mcp_test PRIVATE
    CURSIV_WORKSPACE_LANG="${CMAKE_CURRENT_SOURCE_DIR}/languages/workspace"
    CURSIV_WORKSPACE_GENERATED="${_wsGenDir}")

find_program(CURSIV_NODE node)
if(CURSIV_NODE AND NOT CMAKE_CROSSCOMPILING)
  add_test(NAME wasi_runner_test COMMAND ${CURSIV_NODE} --no-warnings --test
      ${CURSIV_SOURCE_ROOT}/tools/wasi-run.test.mjs)
  set_tests_properties(wasi_runner_test PROPERTIES TIMEOUT 300)
  # The example extensions are copies that may differ only by their names.
  add_test(NAME vscode_copies_test COMMAND ${CURSIV_NODE} --no-warnings --test
      ${CURSIV_SOURCE_ROOT}/tools/vscode-copies.test.mjs)
  # The session drivers' client, on servers that misbehave.
  add_test(NAME session_client_test COMMAND ${CURSIV_NODE} --no-warnings --test
      ${CURSIV_SOURCE_ROOT}/tools/session-client.test.mjs)
  set_tests_properties(session_client_test PROPERTIES TIMEOUT 60)
endif()
# The online demo's worker, on the servers it publishes.
if(CURSIV_NODE AND TARGET domainmodel_lsp AND CMAKE_SYSTEM_NAME STREQUAL "WASI")
  add_test(NAME demo_worker_test COMMAND ${CURSIV_NODE} --no-warnings
      ${CURSIV_SOURCE_ROOT}/tools/demo-worker.test.mjs
      $<TARGET_FILE:arithmetics_lsp> $<TARGET_FILE:domainmodel_lsp>
      $<TARGET_FILE:requirements_lsp> $<TARGET_FILE:statemachine_lsp>)
endif()
if(CURSIV_NODE AND TARGET domainmodel_lsp AND NOT CMAKE_CROSSCOMPILING)
  # The MCP stdio session end to end: spawn `--mcp`, handshake, tools, one rule.
  add_test(NAME mcp_session COMMAND ${CURSIV_NODE}
      ${CURSIV_SOURCE_ROOT}/tools/mcp-session.mjs $<TARGET_FILE:domainmodel_lsp>
      --folder ${CURSIV_SOURCE_ROOT}/examples/domainmodel/example
      --call rule "{\"name\":\"Entity\",\"languageId\":\"domain-model\"}")
  set_tests_properties(mcp_session PROPERTIES TIMEOUT 60)
  # A request is answered after the notifications sent before it.
  add_test(NAME lsp_order_session COMMAND ${CURSIV_NODE}
      ${CURSIV_SOURCE_ROOT}/tools/lsp-order-session.mjs
      $<TARGET_FILE:domainmodel_lsp>
      ${CURSIV_SOURCE_ROOT}/examples/domainmodel/example)
  set_tests_properties(lsp_order_session PROPERTIES TIMEOUT 60)
  # The MCP tunnel reads an unsaved buffer over a real LSP session.
  add_test(NAME mcp_tunnel_session COMMAND ${CURSIV_NODE}
      ${CURSIV_SOURCE_ROOT}/tools/mcp-tunnel-session.mjs
      $<TARGET_FILE:domainmodel_lsp>
      ${CURSIV_SOURCE_ROOT}/examples/domainmodel/example)
  set_tests_properties(mcp_tunnel_session PROPERTIES TIMEOUT 60)
  # The LSP stdio session end to end: handshake, scan, an open document, the
  # requests, shutdown.
  add_test(NAME lsp_session COMMAND ${CURSIV_NODE}
      ${CURSIV_SOURCE_ROOT}/tools/lsp-session.mjs $<TARGET_FILE:domainmodel_lsp>
      ${CURSIV_SOURCE_ROOT}/examples/domainmodel/example/blog.dmodel
      --folder ${CURSIV_SOURCE_ROOT}/examples/domainmodel/example
      --semantic-tokens --folding)
  set_tests_properties(lsp_session PROPERTIES TIMEOUT 60)
endif()
# An MCP session that cannot write its answers, or read its input, fails
# instead of ending as a clean one does.
if(TARGET domainmodel_lsp AND NOT CMAKE_CROSSCOMPILING AND EXISTS /dev/full)
  add_test(NAME mcp_session_stream_failures COMMAND sh -c
      "printf '%s\\n' '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}' | \"$0\" --mcp --folder \"$1\" >/dev/full; test $? -eq 1 || exit 1; \"$0\" --mcp --folder \"$1\" </ >/dev/null; test $? -eq 1 || exit 1; \"$0\" --mcp --folder \"$1\" </dev/null"
      $<TARGET_FILE:domainmodel_lsp>
      ${CURSIV_SOURCE_ROOT}/examples/domainmodel/example)
  set_tests_properties(mcp_session_stream_failures PROPERTIES TIMEOUT 60)
endif()

cursiv_test(navigation_test lsp/navigation_test.cpp cursiv::lsp wsLsp multirefGenerated
    duallrGenerated dualllGenerated)

cursiv_test(presentation_test lsp/presentation_test.cpp cursiv::lsp wsCore)

cursiv_test(server_test lsp/server_test.cpp cursiv::lsp wsCore docgGenerated)
target_compile_definitions(server_test PRIVATE
    CURSIV_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}")
cursiv_timeout(server_test 30)
# Refuses one allocation by replacing the global allocator, which the thread
# sanitizer's runtime also defines: there, nothing is refused.
if(NOT CMAKE_CXX_FLAGS MATCHES "sanitize=thread")
  cursiv_own_allocator(server_test)
  target_compile_definitions(server_test PRIVATE CURSIV_TEST_REFUSES_ALLOCATIONS)
endif()

cursiv_test(completion_test "lsp/completion/completion_test.cpp;lsp/completion/CaretLane.cpp" cursiv::lsp wsCore
    caretGenerated blockGenerated wordsllGenerated wordslrGenerated)
set_target_properties(completion_test PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 0)

cursiv_test(edits_test $<TARGET_OBJECTS:editsObjects> cursiv::lsp wsLsp)

# The public test helpers, exercised on the workspace fixture language.
cursiv_test(ValidationHelper_test support/ValidationHelper_test.cpp wsLsp)
