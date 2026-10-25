// The MCP endpoint with no wire under it -- handshake, registries, dispatch and
// the errors JSON-RPC owes -- and the default tools over a real workspace, with
// and without the lsp storey's formatter.

#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <lsp/json/json.h>
#include <lsp/types.h>

#include "cursiv/core/references/DefaultNameProvider.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "cursiv/core/validation/ValidationRegistry.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/core/workspace/WorkspaceLock.hpp"
#include "cursiv/lsp/mcp/LspMcpService.hpp"
#include "cursiv/lsp/runtime/LanguageClient.hpp"
#include "cursiv/lsp/runtime/LanguageServer.hpp"
#include "cursiv/mcp/DefaultMcpService.hpp"
#include "cursiv/mcp/McpServer.hpp"
#include "cursiv/mcp/McpToolKit.hpp"
#include "cursiv/test/Check.hpp"
#include "workspace/core/ast.hpp"
#include "workspace/generated/Workspace/Module.hpp"
#include "workspace/lsp/LspModule.hpp"

// The MCP endpoint with no wire under it: the handshake, the two registries,
// the dispatch, and the errors JSON-RPC owes.
namespace mcp_server {

namespace {

namespace json = lsp::json;

json::Value ask(cursiv::McpServer& server, std::string_view text) {
  const std::optional<std::string> answer = server.handleText(text);
  CHECK(answer.has_value());
  if (!answer.has_value()) return {};
  return json::parse(*answer);
}

const json::Value* find(const json::Value& value, std::string_view key) {
  return value.isObject() ? value.object().find(key) : nullptr;
}

bool is_string(const json::Value* value, std::string_view expected) {
  return value != nullptr && value->isString() && value->string() == expected;
}

cursiv::McpServer make() { return {"test-server", "1.0.0"}; }

void initialize_echoes_a_known_protocol() {
  cursiv::McpServer server = make();
  const json::Value reply =
      ask(server, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":)"
                  R"({"protocolVersion":"2025-03-26","capabilities":{},)"
                  R"("clientInfo":{"name":"t","version":"0"}}})");
  const json::Value* const result = find(reply, "result");
  CHECK(result != nullptr);
  CHECK(is_string(find(*result, "protocolVersion"), "2025-03-26"));
  const json::Value* const info = find(*result, "serverInfo");
  CHECK(info != nullptr && is_string(find(*info, "name"), "test-server"));
  CHECK(info != nullptr && is_string(find(*info, "version"), "1.0.0"));
  const json::Value* const capabilities = find(*result, "capabilities");
  CHECK(capabilities != nullptr && find(*capabilities, "tools") != nullptr);
  CHECK(capabilities != nullptr && find(*capabilities, "resources") != nullptr);
}

// The guide rides the handshake, and only when someone wrote one.
void instructions_ride_the_handshake() {
  cursiv::McpServer bare = make();
  const json::Value silent = ask(
      bare, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
  CHECK(find(*find(silent, "result"), "instructions") == nullptr);
  cursiv::McpServer guided = make();
  guided.addInstructions("read the grammar first");
  guided.addInstructions("then validate");
  const json::Value reply = ask(
      guided, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
  const json::Value* const instructions =
      find(*find(reply, "result"), "instructions");
  CHECK(instructions != nullptr && instructions->isString());
  CHECK(instructions != nullptr &&
        instructions->string() == "read the grammar first\nthen validate");
}

void a_stranger_protocol_gets_the_latest() {
  cursiv::McpServer server = make();
  const json::Value reply =
      ask(server, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":)"
                  R"({"protocolVersion":"1999-01-01"}})");
  const json::Value* const result = find(reply, "result");
  CHECK(result != nullptr);
  CHECK(is_string(find(*result, "protocolVersion"), "2025-06-18"));
}

void ping_answers_an_empty_object() {
  cursiv::McpServer server = make();
  const json::Value reply =
      ask(server, R"({"jsonrpc":"2.0","id":2,"method":"ping"})");
  const json::Value* const result = find(reply, "result");
  CHECK(result != nullptr && result->isObject() && result->object().isEmpty());
}

void an_unregistered_server_lists_no_tools() {
  cursiv::McpServer server = make();
  const json::Value reply =
      ask(server, R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})");
  const json::Value* const result = find(reply, "result");
  const json::Value* const tools =
      result == nullptr ? nullptr : find(*result, "tools");
  CHECK(tools != nullptr && tools->isArray() && tools->array().empty());
}

void a_registered_tool_is_listed_and_called() {
  cursiv::McpServer server = make();
  server.registerTool(
      {.name = "echo", .title = "Echo", .description = "answers its argument"},
      [](const json::Value& arguments, const cursiv::CancellationToken&) {
        const json::Value* const text = find(arguments, "text");
        return cursiv::McpToolResult{
            text != nullptr && text->isString() ? text->string() : ""};
      });

  const json::Value list =
      ask(server, R"({"jsonrpc":"2.0","id":4,"method":"tools/list"})");
  const json::Value* const tools = find(*find(list, "result"), "tools");
  CHECK(tools != nullptr && tools->isArray() && tools->array().size() == 1);
  const json::Value& tool = tools->array().front();
  CHECK(is_string(find(tool, "name"), "echo"));
  CHECK(is_string(find(tool, "title"), "Echo"));
  const json::Value* const schema = find(tool, "inputSchema");
  CHECK(schema != nullptr && is_string(find(*schema, "type"), "object"));
  const json::Value* const annotations = find(tool, "annotations");
  const json::Value* const readOnly =
      annotations == nullptr ? nullptr : find(*annotations, "readOnlyHint");
  CHECK(readOnly != nullptr && readOnly->isBoolean() && readOnly->boolean());

  const json::Value call =
      ask(server, R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":)"
                  R"({"name":"echo","arguments":{"text":"hi"}}})");
  const json::Value* const result = find(call, "result");
  CHECK(result != nullptr && find(*result, "isError") == nullptr);
  const json::Value* const content = find(*result, "content");
  CHECK(content != nullptr && content->isArray() &&
        content->array().size() == 1);
  CHECK(is_string(find(content->array().front(), "text"), "hi"));
}

// A tool that throws answers inside the result, so the model reads the
// failure; the protocol error channel stays for the protocol's own faults.
void a_throwing_tool_reports_inside_the_result() {
  cursiv::McpServer server = make();
  server.registerTool(
      {.name = "boom"},
      [](const json::Value&, const cursiv::CancellationToken&)
          -> cursiv::McpToolResult { throw std::runtime_error("broken"); });
  const json::Value call = ask(
      server,
      R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"boom"}})");
  CHECK(find(call, "error") == nullptr);
  const json::Value* const result = find(call, "result");
  const json::Value* const isError =
      result == nullptr ? nullptr : find(*result, "isError");
  CHECK(isError != nullptr && isError->isBoolean() && isError->boolean());
  const json::Value* const content = find(*result, "content");
  CHECK(content != nullptr &&
        is_string(find(content->array().front(), "text"), "broken"));
}

void an_unknown_tool_is_invalid_params() {
  cursiv::McpServer server = make();
  const json::Value reply = ask(
      server,
      R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"nope"}})");
  const json::Value* const error = find(reply, "error");
  const json::Value* const code =
      error == nullptr ? nullptr : find(*error, "code");
  CHECK(code != nullptr && code->isInteger() && code->integer() == -32602);
}

void an_unknown_method_is_method_not_found() {
  cursiv::McpServer server = make();
  const json::Value reply =
      ask(server, R"({"jsonrpc":"2.0","id":8,"method":"prompts/list"})");
  const json::Value* const error = find(reply, "error");
  const json::Value* const code =
      error == nullptr ? nullptr : find(*error, "code");
  CHECK(code != nullptr && code->isInteger() && code->integer() == -32601);
  const json::Value* const id = find(reply, "id");
  CHECK(id != nullptr && id->isInteger() && id->integer() == 8);
}

void a_notification_answers_nothing() {
  cursiv::McpServer server = make();
  CHECK(!server
             .handleText(
                 R"({"jsonrpc":"2.0","method":"notifications/initialized"})")
             .has_value());
  CHECK(!server
             .handleText(
                 R"({"jsonrpc":"2.0","method":"notifications/cancelled"})")
             .has_value());
}

void malformed_json_is_a_parse_error() {
  cursiv::McpServer server = make();
  const std::optional<std::string> answer = server.handleText("{nope");
  CHECK(answer.has_value());
  if (!answer.has_value()) return;
  const json::Value reply = json::parse(*answer);
  const json::Value* const error = find(reply, "error");
  const json::Value* const code =
      error == nullptr ? nullptr : find(*error, "code");
  CHECK(code != nullptr && code->isInteger() && code->integer() == -32700);
  const json::Value* const id = find(reply, "id");
  CHECK(id != nullptr && id->isNull());
}

void a_batch_is_refused() {
  cursiv::McpServer server = make();
  const json::Value reply =
      ask(server, R"([{"jsonrpc":"2.0","id":1,"method":"ping"}])");
  const json::Value* const error = find(reply, "error");
  const json::Value* const code =
      error == nullptr ? nullptr : find(*error, "code");
  CHECK(code != nullptr && code->isInteger() && code->integer() == -32600);
}

void a_resource_is_listed_and_read() {
  cursiv::McpServer server = make();
  server.registerResource({.uri = "cursiv://grammar/test",
                           .name = "grammar",
                           .description = "the grammar as text",
                           .mimeType = "text/plain"},
                          [] { return std::string("grammar text"); });

  const json::Value list =
      ask(server, R"({"jsonrpc":"2.0","id":9,"method":"resources/list"})");
  const json::Value* const resources = find(*find(list, "result"), "resources");
  CHECK(resources != nullptr && resources->isArray() &&
        resources->array().size() == 1);
  CHECK(is_string(find(resources->array().front(), "uri"),
                  "cursiv://grammar/test"));

  const json::Value read = ask(
      server, R"({"jsonrpc":"2.0","id":10,"method":"resources/read","params":)"
              R"({"uri":"cursiv://grammar/test"}})");
  const json::Value* const contents = find(*find(read, "result"), "contents");
  CHECK(contents != nullptr && contents->isArray() &&
        contents->array().size() == 1);
  CHECK(is_string(find(contents->array().front(), "text"), "grammar text"));
  CHECK(is_string(find(contents->array().front(), "mimeType"), "text/plain"));

  const json::Value missing = ask(
      server, R"({"jsonrpc":"2.0","id":11,"method":"resources/read","params":)"
              R"({"uri":"cursiv://grammar/other"}})");
  const json::Value* const error = find(missing, "error");
  const json::Value* const code =
      error == nullptr ? nullptr : find(*error, "code");
  CHECK(code != nullptr && code->isInteger() && code->integer() == -32002);
}

}  // namespace

void run() {
  initialize_echoes_a_known_protocol();
  instructions_ride_the_handshake();
  a_stranger_protocol_gets_the_latest();
  ping_answers_an_empty_object();
  an_unregistered_server_lists_no_tools();
  a_registered_tool_is_listed_and_called();
  a_throwing_tool_reports_inside_the_result();
  an_unknown_tool_is_invalid_params();
  an_unknown_method_is_method_not_found();
  a_notification_answers_nothing();
  malformed_json_is_a_parse_error();
  a_batch_is_refused();
  a_resource_is_listed_and_read();
}

}  // namespace mcp_server

// The default tool set over a real workspace: two documents, one broken, and
// the five facades answered through the JSON door.
namespace default_mcp_service {

namespace {

namespace json = lsp::json;

constexpr std::string_view kA = "file:///w/a.ws";
constexpr std::string_view kB = "file:///w/b.ws";
constexpr std::string_view kC = "file:///w/c.ws";

struct World {
  struct Shared : cursiv::SharedCoreServices {};
  struct Language : cursiv::CoreServices {
    explicit Language(Shared& s) : cursiv::CoreServices(s) {}
  };

  Shared shared;

  World() {
    shared.fileSystemProvider =
        std::make_unique<cursiv::EmptyFileSystemProvider>();
    cursiv::install_default_shared_core_services(shared);
    addLanguage("workspace", {".ws"});
  }

  void addLanguage(std::string id, std::vector<std::string> extensions,
                   std::string diagnostic = {}) {
    auto language = std::make_unique<Language>(shared);
    language->languageMetaData = {std::move(id), std::move(extensions), {}};
    language->parser = ws::generated::Workspace::create_parser();
    language->astReflection = ws::generated::Workspace::create_ast_reflection();
    language->nameProvider =
        std::make_unique<cursiv::DefaultNameProvider>(*language);
    language->grammarSource = ws::generated::Workspace::create_grammar_source();
    cursiv::install_default_core_services(*language);
    if (!diagnostic.empty())
      language->validationRegistry->registerAfterDocument(
          [message = std::move(diagnostic)](
              const cursiv::Document&, cursiv::ValidationAcceptor& accept,
              const cursiv::CancellationToken&) {
            accept.accept(cursiv::TextRange{0, 0},
                          cursiv::DiagnosticSeverity::Warning, message);
          });
    shared.serviceRegistry->registerServices(std::move(language));
  }

  void build(
      std::initializer_list<std::pair<std::string_view, std::string_view>>
          documents) {
    std::vector<std::string_view> changed;
    shared.workspaceLock->write([&](const cursiv::CancellationToken& token) {
      for (const auto& [uri, text] : documents) {
        (void)shared.documents->createDocument(uri, std::string(text), token);
        changed.push_back(uri);
      }
      shared.documentBuilder->update(changed, {}, token);
    });
  }
};

struct Answer {
  std::string text;
  bool isError = false;
};

Answer call_tool(cursiv::McpServer& server, std::string_view name,
                 std::string_view argumentsJson) {
  std::string request = R"({"jsonrpc":"2.0","id":1,"method":"tools/call",)";
  request += R"("params":{"name":")";
  request += name;
  request += R"(","arguments":)";
  request += argumentsJson;
  request += "}}";
  const std::optional<std::string> raw = server.handleText(request);
  CHECK(raw.has_value());
  if (!raw.has_value()) return {};
  const json::Value reply = json::parse(*raw);
  const json::Value* const result = reply.object().find("result");
  CHECK(result != nullptr);
  if (result == nullptr) return {};
  Answer answer;
  if (const json::Value* const isError = result->object().find("isError"))
    answer.isError = isError->boolean();
  const json::Value* const content = result->object().find("content");
  CHECK(content != nullptr && content->isArray() && !content->array().empty());
  if (content == nullptr || !content->isArray() || content->array().empty())
    return answer;
  answer.text = content->array().front().object().get("text").string();
  return answer;
}

bool contains(const std::string& text, std::string_view needle) {
  return text.find(needle) != std::string::npos;
}

// One world for the whole lane: the build is the slow half, and the tools all
// read the same one.
World& world() {
  static World w;
  static const bool built = [] {
    w.build({{kA, "datatype D\nentity A { x : D }\n"},
             {kB, "entity B { y : Missing }\n"},
             {kC, "entity C { z : "}});
    return true;
  }();
  (void)built;
  return w;
}

cursiv::McpServer make_server() {
  const cursiv::DefaultMcpService service(world().shared, {"test-mcp", "0"});
  auto server = service.createServer();
  return std::move(*server);
}

void diagnostics_report_the_broken_documents_only() {
  cursiv::McpServer server = make_server();
  const Answer answer = call_tool(server, "get-diagnostics", "{}");
  CHECK(!answer.isError);
  CHECK(contains(answer.text, "b.ws"));
  CHECK(contains(answer.text, "[error at 1:"));
  CHECK(!contains(answer.text, "a.ws"));
}

// A fix the validator stamped reaches a caller with no editor: the
// diagnostic names it by title.
void diagnostics_name_the_fixes_they_carry() {
  World own;
  own.build({{"file:///w/f.ws", "datatype String\nentity F { x : Strng }\n"}});
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  cursiv::McpServer server = std::move(*service.createServer());
  const Answer answer = call_tool(server, "get-diagnostics", "{}");
  CHECK(!answer.isError);
  CHECK(contains(answer.text, "-- fix: Replace with 'String'"));
}

void symbols_list_the_exported_names() {
  cursiv::McpServer server = make_server();
  const Answer all = call_tool(server, "symbols", "{}");
  CHECK(!all.isError);
  CHECK(contains(all.text, "D (DataType)"));
  CHECK(contains(all.text, "A (Entity)"));
  CHECK(contains(all.text, "a.ws:"));
  const Answer filtered = call_tool(server, "symbols", R"({"query":"d"})");
  CHECK(contains(filtered.text, "D (DataType)"));
  CHECK(!contains(filtered.text, "A (Entity)"));
}

void references_answer_declaration_and_uses() {
  cursiv::McpServer server = make_server();
  const Answer answer =
      call_tool(server, "references",
                R"({"file":"file:///w/a.ws","line":1,"column":10})");
  CHECK(!answer.isError);
  CHECK(contains(answer.text, "'D'"));
  CHECK(contains(answer.text, "a.ws:2:"));
}

void expected_at_names_the_member_being_filled() {
  cursiv::McpServer server = make_server();
  const Answer answer =
      call_tool(server, "expected-at",
                R"({"file":"file:///w/c.ws","line":1,"column":16})");
  CHECK(!answer.isError);
  CHECK(contains(answer.text, "fills 'type'"));
  CHECK(!contains(answer.text, "in place of"));
}

// A caret ending a word the author is still typing is asked in its place:
// `ent` completes to `entity`, and the answer says what it replaces instead
// of offering `entity` behind it.
void expected_at_answers_in_place_of_the_word_typed() {
  World own;
  own.build({{"file:///w/w.ws", "datatype D\nent"}});
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  cursiv::McpServer server = std::move(*service.createServer());
  const Answer answer =
      call_tool(server, "expected-at",
                R"({"file":"file:///w/w.ws","line":2,"column":4})");
  CHECK(!answer.isError);
  CHECK(contains(answer.text, "in place of 'ent'"));
  CHECK(contains(answer.text, "'entity'"));
}

void scope_candidates_answer_the_scope() {
  cursiv::McpServer server = make_server();
  const Answer answer =
      call_tool(server, "scope-candidates",
                R"({"file":"file:///w/c.ws","line":1,"column":16})");
  CHECK(!answer.isError);
  CHECK(contains(answer.text, "candidates for member 'type'"));
  CHECK(contains(answer.text, "D (DataType)"));
}

void documents_list_the_store() {
  cursiv::McpServer server = make_server();
  const Answer answer = call_tool(server, "documents", "{}");
  CHECK(!answer.isError);
  CHECK(contains(answer.text, "a.ws  on disk"));
  CHECK(contains(answer.text, "b.ws"));
  CHECK(contains(answer.text, "1 diagnostic(s)"));
}

// The asymmetry closer: what the workspace holds is readable, disk or not.
void text_document_content_serves_the_held_text() {
  World own;
  own.build({{kA, "datatype D\nentity A { x : D }\n"},
             {"file:///w/a%23b%3Fc%25d.ws", "entity P { }\n"}});
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  cursiv::McpServer server = std::move(*service.createServer());
  const Answer text = call_tool(server, "text-document-content",
                                R"({"file":"file:///w/a.ws"})");
  CHECK(!text.isError);
  CHECK(text.text == "datatype D\nentity A { x : D }\n");
  for (const std::string_view arguments :
       {R"({"file":"/w/a#b?c%d.ws"})",
        R"({"file":"file:///w/a%23b%3Fc%25d.ws"})"}) {
    const Answer special = call_tool(server, "text-document-content", arguments);
    CHECK(!special.isError && special.text == "entity P { }\n");
  }
  const Answer missing = call_tool(server, "text-document-content",
                                   R"({"file":"file:///w/zzz.ws"})");
  CHECK(missing.isError);
  CHECK(contains(missing.text, "'documents' tool"));
}

void document_symbols_answer_the_outline() {
  cursiv::McpServer server = make_server();
  const Answer outline =
      call_tool(server, "document-symbols", R"({"file":"file:///w/a.ws"})");
  CHECK(!outline.isError);
  CHECK(contains(outline.text, "D (DataType)"));
  CHECK(contains(outline.text, "A (Entity)"));
  CHECK(contains(outline.text, "x (Feature)"));
}

void hover_describes_the_declaration() {
  cursiv::McpServer server = make_server();
  const Answer answer = call_tool(
      server, "hover", R"({"file":"file:///w/a.ws","line":2,"column":16})");
  CHECK(!answer.isError);
  CHECK(contains(answer.text, "'D' (DataType)"));
  CHECK(contains(answer.text, "declared at"));
  CHECK(contains(answer.text, "a.ws:1:"));
}

void rename_lists_every_edit_and_applies_nothing() {
  cursiv::McpServer server = make_server();
  const Answer answer = call_tool(
      server, "rename",
      R"({"file":"file:///w/a.ws","line":1,"column":10,"newName":"E"})");
  CHECK(!answer.isError);
  CHECK(contains(answer.text,
                 "replace 'D' with 'E' at (1-based, end-exclusive):"));
  CHECK(contains(answer.text, "a.ws:1:10-1:11"));
  CHECK(contains(answer.text, "a.ws:2:16-2:17"));
  const Answer content = call_tool(server, "text-document-content",
                                   R"({"file":"file:///w/a.ws"})");
  CHECK(contains(content.text, "datatype D"));
}

// The generation loop's tool: a text validated against the workspace's own
// symbols, then discarded without a trace.
void validate_links_against_the_workspace() {
  cursiv::McpServer server = make_server();
  const Answer valid =
      call_tool(server, "validate",
                R"({"text":"entity E { v : D }\n","languageId":"workspace"})");
  CHECK(!valid.isError);
  CHECK(contains(valid.text, "No diagnostics"));
  const Answer symbols = call_tool(server, "symbols", R"({"query":"E"})");
  CHECK(contains(symbols.text, "No symbols found."));
}

void validate_uses_the_explicit_language() {
  World own;
  own.addLanguage("requested", {".shared"}, "requested language checked");
  own.addLanguage("other", {".shared"}, "other language checked");
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();
  for (const std::string_view arguments : {
           R"({"text":"datatype D\n","languageId":"requested"})",
           R"({"text":"datatype D\n","languageId":"requested","file":"other.ws"})"}) {
    const Answer result = call_tool(*server, "validate", arguments);
    CHECK(!result.isError);
    CHECK(contains(result.text, "requested language checked"));
    CHECK(!contains(result.text, "other language checked"));
    CHECK(own.shared.documents->all().empty());
  }
  const Answer unknown = call_tool(
      *server, "validate",
      R"({"text":"datatype D\n","languageId":"missing","file":"valid.ws"})");
  CHECK(unknown.isError);
  CHECK(contains(unknown.text, "no language is registered as 'missing'"));
  CHECK(own.shared.documents->all().empty());
}

void validate_accepts_an_extension_without_a_dot_or_no_mapping() {
  World own;
  own.addLanguage("bare-extension", {"raw"}, "bare extension checked");
  own.addLanguage("id-only", {}, "id-only language checked");
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();

  const Answer bare = call_tool(
      *server, "validate",
      R"({"text":"datatype D\n","languageId":"bare-extension"})");
  CHECK(!bare.isError);
  CHECK(contains(bare.text, "bare extension checked"));

  const Answer unmapped = call_tool(
      *server, "validate",
      R"({"text":"datatype D\n","languageId":"id-only"})");
  CHECK(!unmapped.isError);
  CHECK(contains(unmapped.text, "id-only language checked"));
  CHECK(own.shared.documents->all().empty());
}

void validate_discards_the_scratch_after_an_initial_build_failure() {
  World own;
  own.build({{kA, "datatype D\n"}, {kB, "entity B { y : D }\n"}});
  const auto declaration = own.shared.documents->getDocument(kA);
  const auto dependent = own.shared.documents->getDocument(kB);
  const auto* model =
      cursiv::ast_cast<ws::ast::Workspace>(dependent->parseResult.value);
  CHECK(model != nullptr && model->elements.size() == 1);
  if (model == nullptr || model->elements.size() != 1) return;
  const auto* entity = cursiv::ast_cast<ws::ast::Entity>(model->elements[0]);
  CHECK(entity != nullptr && entity->features.size() == 1);
  if (entity == nullptr || entity->features.size() != 1) return;
  const auto* target = entity->features[0]->type.get();
  CHECK(target != nullptr && target->getDocument() == declaration.get());

  struct ThrowOnce final : cursiv::BuildPhaseListener {
    bool thrown = false;
    void onBuildPhase(cursiv::DocumentState state,
                      std::span<cursiv::Document* const>,
                      const cursiv::CancellationToken&) override {
      if (state == cursiv::DocumentState::IndexedContent &&
          !std::exchange(thrown, true))
        throw std::runtime_error("initial validation build failed");
    }
  } failure;
  own.shared.documentBuilder->addBuildPhaseListener(failure);
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();
  const Answer failed = call_tool(
      *server, "validate", R"({"text":"datatype D\n","file":"failure.ws"})");
  own.shared.documentBuilder->removeBuildPhaseListener(failure);
  CHECK(failure.thrown);
  CHECK(failed.isError);
  CHECK(contains(failed.text, "initial validation build failed"));
  CHECK(own.shared.documents->all().size() == 2);
  CHECK(own.shared.documents->getDocument(kA) == declaration);
  CHECK(own.shared.documents->getDocument(kB) == dependent);
  CHECK(entity->features[0]->type.get() == target);
  CHECK(!dependent->hasLinkingErrors());

  const Answer retry = call_tool(
      *server, "validate", R"({"text":"datatype D\n","file":"failure.ws"})");
  CHECK(!retry.isError);
  CHECK(contains(retry.text, "No diagnostics"));
  CHECK(own.shared.documents->all().size() == 2);
  CHECK(entity->features[0]->type.get() == target);
}

void validate_keeps_a_document_it_did_not_install() {
  World own;
  constexpr std::string_view reserved =
      "file:///.mcp-scratch/0/reserved.ws";
  own.build({{reserved, "datatype D\n"}, {kB, "entity B { y : D }\n"}});
  const auto existing = own.shared.documents->getDocument(reserved);
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();

  const Answer result = call_tool(
      *server, "validate",
      R"({"text":"datatype Replacement\n","file":"reserved.ws"})");
  CHECK(result.isError);
  CHECK(contains(result.text, "already present"));
  CHECK(own.shared.documents->all().size() == 2);
  CHECK(own.shared.documents->getDocument(reserved) == existing);
  CHECK(existing->textDocument().getText() == "datatype D\n");
  CHECK(!own.shared.documents->getDocument(kB)->hasLinkingErrors());
}

void validate_reports_the_broken_text_and_pollutes_nothing() {
  cursiv::McpServer server = make_server();
  const Answer broken =
      call_tool(server, "validate",
                R"({"text":"entity E { v : Nope }\n","file":"gen#?%.ws"})");
  CHECK(!broken.isError);
  CHECK(contains(broken.text, "[error at 1:"));
  const Answer diagnostics = call_tool(server, "get-diagnostics", "{}");
  CHECK(contains(diagnostics.text, "b.ws"));
  CHECK(!contains(diagnostics.text, "scratch"));
  const Answer unknown =
      call_tool(server, "validate", R"({"text":"x","languageId":"nope"})");
  CHECK(unknown.isError);
  CHECK(contains(unknown.text, "no language is registered as 'nope'"));

  // A validation through the editor's MCP door also clears its temporary
  // diagnostics when the builder removes the document.
  struct Client final : cursiv::LanguageClient {
    std::vector<lsp::PublishDiagnosticsParams> published;
    void publishDiagnostics(lsp::PublishDiagnosticsParams params) override {
      published.push_back(std::move(params));
    }
  };
  ws::WorkspaceLanguage language = ws::create_lsp_services();
  auto client = std::make_unique<Client>();
  const auto& published = client->published;
  language.shared->languageClient = std::move(client);
  (void)language.shared->languageServer->initialize({});
  const cursiv::LspMcpService service(*language.shared, {"test-mcp", "0"});
  auto editorServer = service.createServer();
  (void)call_tool(*editorServer, "validate",
                  R"({"text":"entity E { v : Nope }\n","file":"gen#?%.ws"})");
  CHECK(published.size() == 2);
  if (published.size() == 2) {
    CHECK(!published.front().diagnostics.empty());
    CHECK(published.back().uri == published.front().uri);
    CHECK(published.back().diagnostics.empty());
    CHECK(!published.back().version.has_value());
  }
}

// The grammar resource is byte for byte the golden reference.
// One validation at a time uses one scratch identity, handed back after: the
// store does not grow by one row per call.
void validate_reuses_one_scratch_identity() {
  cursiv::McpServer server = make_server();
  for (int i = 0; i < 3; ++i)
    (void)call_tool(server, "validate",
                    R"({"text":"datatype D\n","file":"again.ws"})");
  const cursiv::Documents& store = *world().shared.documents;
  CHECK(!store.hasDocument("file:///.mcp-scratch/0/again.ws"));
  CHECK(store.getDocumentId("file:///.mcp-scratch/1/again.ws") ==
        cursiv::kNoDocument);
}

void the_grammar_resource_serves_the_golden_text() {
  cursiv::McpServer server = make_server();
  const std::optional<std::string> listed = server.handleText(
      R"({"jsonrpc":"2.0","id":1,"method":"resources/list"})");
  CHECK(listed.has_value() && contains(*listed, "cursiv://grammar/workspace"));
  const std::optional<std::string> raw =
      server.handleText(R"({"jsonrpc":"2.0","id":2,"method":"resources/read",)"
                        R"("params":{"uri":"cursiv://grammar/workspace"}})");
  CHECK(raw.has_value());
  if (!raw.has_value()) return;
  const json::Value reply = json::parse(*raw);
  const json::Value* const result = reply.object().find("result");
  CHECK(result != nullptr);
  if (result == nullptr) return;
  const std::string served = result->object()
                                 .get("contents")
                                 .array()
                                 .front()
                                 .object()
                                 .get("text")
                                 .string();
  std::string golden = cursiv::test::read_file(
      std::string(CURSIV_WORKSPACE_GENERATED) + "/GRAMMAR.txt");
  while (!golden.empty() && (golden.back() == '\n' || golden.back() == ' '))
    golden.pop_back();
  std::string trimmed = served;
  while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == ' '))
    trimmed.pop_back();
  CHECK(!golden.empty());
  CHECK(trimmed == golden);
}

void languages_list_the_registry() {
  cursiv::McpServer server = make_server();
  const Answer answer = call_tool(server, "languages", "{}");
  CHECK(!answer.isError);
  CHECK(contains(answer.text, "workspace"));
  CHECK(contains(answer.text, ".ws"));
  CHECK(contains(answer.text, "grammar: cursiv://grammar/workspace"));
}

// `languageId` is required even with one language: the contract must read the
// same whatever the workspace holds.
void the_grammar_tool_serves_the_text() {
  cursiv::McpServer server = make_server();
  const Answer named =
      call_tool(server, "grammar", R"({"languageId":"workspace"})");
  CHECK(!named.isError);
  CHECK(contains(named.text, "grammar Workspace"));
  CHECK(contains(named.text, "entry"));
  const Answer bare = call_tool(server, "grammar", "{}");
  CHECK(bare.isError);
  CHECK(contains(bare.text, "'languageId' names the grammar"));
  const Answer stranger =
      call_tool(server, "grammar", R"({"languageId":"nope"})");
  CHECK(stranger.isError);
  CHECK(contains(stranger.text, "no grammar is served for 'nope'"));
  CHECK(contains(stranger.text, "served: workspace"));
}

void the_rule_tool_answers_the_closure() {
  cursiv::McpServer server = make_server();
  const Answer closure = call_tool(
      server, "rule", R"({"name":"FeatureRule","languageId":"workspace"})");
  CHECK(!closure.isError);
  CHECK(contains(closure.text, "FeatureRule"));
  CHECK(contains(closure.text, "QualifiedName"));
  const Answer missing =
      call_tool(server, "rule", R"({"name":"Nope","languageId":"workspace"})");
  CHECK(missing.isError);
  CHECK(contains(missing.text, "no rule named 'Nope'"));
  CHECK(contains(missing.text, "EntityRule"));
}

void a_wrong_call_reports_inside_the_result() {
  cursiv::McpServer server = make_server();
  const Answer missing = call_tool(server, "references", "{}");
  CHECK(missing.isError);
  CHECK(contains(missing.text, "'file' names the document"));
  const Answer stranger =
      call_tool(server, "references",
                R"({"file":"file:///w/nope.ws","line":1,"column":1})");
  CHECK(stranger.isError);
  CHECK(contains(stranger.text, "not in the workspace"));
}

}  // namespace

void run() {
  diagnostics_report_the_broken_documents_only();
  diagnostics_name_the_fixes_they_carry();
  symbols_list_the_exported_names();
  references_answer_declaration_and_uses();
  expected_at_names_the_member_being_filled();
  expected_at_answers_in_place_of_the_word_typed();
  scope_candidates_answer_the_scope();
  documents_list_the_store();
  text_document_content_serves_the_held_text();
  document_symbols_answer_the_outline();
  hover_describes_the_declaration();
  rename_lists_every_edit_and_applies_nothing();
  validate_links_against_the_workspace();
  validate_uses_the_explicit_language();
  validate_accepts_an_extension_without_a_dot_or_no_mapping();
  validate_discards_the_scratch_after_an_initial_build_failure();
  validate_keeps_a_document_it_did_not_install();
  validate_reports_the_broken_text_and_pollutes_nothing();
  validate_reuses_one_scratch_identity();
  the_grammar_resource_serves_the_golden_text();
  languages_list_the_registry();
  the_grammar_tool_serves_the_text();
  the_rule_tool_answers_the_closure();
  a_wrong_call_reports_inside_the_result();
}

}  // namespace default_mcp_service

// The lsp-storey MCP service: the default set plus the formatter's door,
// over the one shipped language whose formatter has opinions.
namespace lsp_mcp_service {

namespace {

namespace json = lsp::json;

constexpr std::string_view kUri = "file:///w/messy.ws";
constexpr std::string_view kMessy =
    "entity   Person{\n"
    "name   :String\n"
    "     age:String\n"
    "}\n"
    "datatype     String\n";

struct World {
  ws::WorkspaceLanguage services = ws::create_lsp_services();

  World() {
    services.shared->workspaceLock->write(
        [&](const cursiv::CancellationToken& token) {
          (void)services.shared->documents->createDocument(
              kUri, std::string(kMessy), token);
          const std::string_view changed[] = {kUri};
          services.shared->documentBuilder->update(changed, {}, token);
        });
  }
};

World& world() {
  static World w;
  return w;
}

cursiv::McpServer make_server() {
  const cursiv::LspMcpService service(*world().services.shared,
                                      {"test-lsp-mcp", "0"});
  return std::move(*service.createServer());
}

bool contains(const std::string& text, std::string_view needle) {
  return text.find(needle) != std::string::npos;
}

json::Value ask(cursiv::McpServer& server, std::string_view text) {
  const std::optional<std::string> answer = server.handleText(text);
  CHECK(answer.has_value());
  if (!answer.has_value()) return {};
  return json::parse(*answer);
}

void the_lsp_service_extends_the_default_set() {
  cursiv::McpServer server = make_server();
  const std::optional<std::string> tools =
      server.handleText(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
  CHECK(tools.has_value());
  CHECK(tools.has_value() && contains(*tools, "\"format\""));
  CHECK(tools.has_value() && contains(*tools, "get-diagnostics"));
  const json::Value init =
      ask(server, R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":)"
                  R"({"protocolVersion":"2025-06-18"}})");
  const std::string instructions =
      init.object().get("result").object().get("instructions").string();
  CHECK(contains(instructions, "'format'"));
  CHECK(contains(instructions, "'validate'"));
}

void format_answers_the_shaped_text() {
  cursiv::McpServer server = make_server();
  const json::Value reply =
      ask(server,
          R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":)"
          R"({"name":"format","arguments":{"file":"file:///w/messy.ws"}}})");
  const json::Value& result = reply.object().get("result");
  CHECK(result.object().find("isError") == nullptr);
  const std::string text = result.object()
                               .get("content")
                               .array()
                               .front()
                               .object()
                               .get("text")
                               .string();
  CHECK(contains(text, "entity Person {\n  name: String\n  age: String\n}"));
  CHECK(!contains(text, "name   :"));
  const json::Value missing = ask(
      server, R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":)"
              R"({"name":"format","arguments":{"file":"file:///w/none.ws"}}})");
  const json::Value* const isError =
      missing.object().get("result").object().find("isError");
  CHECK(isError != nullptr && isError->boolean());
}

// What a language project does: subclass, add its own door with the public
// kit, and silence a default one by overriding its register method.
struct BlogMcpService final : cursiv::LspMcpService {
  using cursiv::LspMcpService::LspMcpService;

  [[nodiscard]] std::unique_ptr<cursiv::McpServer> createServer()
      const override {
    std::unique_ptr<cursiv::McpServer> server =
        cursiv::LspMcpService::createServer();
    json::Object properties;
    properties["file"] =
        cursiv::tool_property("string", "path or file:// uri of the document");
    json::Array required;
    required.emplace_back(json::String("file"));
    server->registerTool(
        {.name = "byte-count",
         .title = "Byte Count",
         .description = "how many bytes a document holds",
         .inputSchema = cursiv::tool_object_schema(std::move(properties),
                                                   std::move(required))},
        [&shared = _shared](const json::Value& arguments,
                            const cursiv::CancellationToken& token) {
          const json::Value* const file =
              cursiv::tool_argument(arguments, "file");
          if (file == nullptr || !file->isString())
            throw std::invalid_argument("'file' names the document");
          return cursiv::route_document_tool(
              shared, cursiv::tool_file_uri(file->string()),
              cursiv::DocumentState::Parsed, token,
              [&](const cursiv::Document& document, const cursiv::CoreServices&,
                  std::string& out) {
                out = std::to_string(document.textDocument().getText().size()) +
                      " byte(s)";
              });
        });
    return server;
  }

  void registerRenameTool(cursiv::McpServer&) const override {}
};

void a_language_extends_and_prunes_the_set() {
  const BlogMcpService service(*world().services.shared, {"blog-mcp", "0"});
  cursiv::McpServer server = std::move(*service.createServer());
  const std::optional<std::string> tools =
      server.handleText(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
  CHECK(tools.has_value());
  if (!tools.has_value()) return;
  CHECK(contains(*tools, "byte-count"));
  CHECK(contains(*tools, "\"format\""));
  CHECK(!contains(*tools, "rename"));
  const json::Value reply = ask(
      server,
      R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":)"
      R"({"name":"byte-count","arguments":{"file":"file:///w/messy.ws"}}})");
  const std::string text = reply.object()
                               .get("result")
                               .object()
                               .get("content")
                               .array()
                               .front()
                               .object()
                               .get("text")
                               .string();
  CHECK(text == std::to_string(kMessy.size()) + " byte(s)");
}

}  // namespace

void run() {
  the_lsp_service_extends_the_default_set();
  format_answers_the_shaped_text();
  a_language_extends_and_prunes_the_set();
}

}  // namespace lsp_mcp_service

int main() {
  mcp_server::run();
  default_mcp_service::run();
  lsp_mcp_service::run();
  return cursiv::test::report("mcp");
}
