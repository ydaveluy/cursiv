// The MCP server without a transport, and its default tools over a real
// workspace, with and without the formatter's tool.

#include <lsp/json/json.h>
#include <lsp/types.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "caret/generated/Caret/Module.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/DefaultMcpService.hpp"
#include "cursiv/McpServer.hpp"
#include "cursiv/McpToolKit.hpp"
#include "cursiv/formatting/Formatter.hpp"
#include "cursiv/mcp/LspMcpService.hpp"
#include "cursiv/references/DefaultLinker.hpp"
#include "cursiv/references/DefaultScopeComputation.hpp"
#include "cursiv/runtime/LanguageClient.hpp"
#include "cursiv/runtime/LanguageServer.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "cursiv/validation/ValidationCategory.hpp"
#include "cursiv/validation/ValidationRegistry.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/ServiceRequirement.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/workspace/WorkspaceLock.hpp"
#include "doc/generated/DocG/Module.hpp"
#include "multiref/generated/MultiG/Module.hpp"
#include "support/Throws.hpp"
#include "support/WorkspaceWorld.hpp"
#include "wordslr/generated/Lr/Module.hpp"
#include "workspace/core/ast.hpp"
#include "workspace/generated/Workspace/Module.hpp"
#include "workspace/lsp/LspModule.hpp"

// The MCP server without a transport: the handshake, the two registries, the
// dispatch, and the errors JSON-RPC requires.
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

// The instructions come with the handshake, only when some were written.
void instructions_come_with_the_handshake() {
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

// One entry per name: a second one would be listed and never called.
void a_name_is_registered_once() {
  cursiv::McpServer server = make();
  const auto first = [](const json::Value&, const cursiv::CancellationToken&) {
    return cursiv::McpToolResult{"first"};
  };
  const auto text = [] { return std::string("text"); };
  server.registerTool({.name = "echo"}, first);
  server.registerResource({.uri = "cursiv://x", .name = "x"}, text);
  CHECK(support::throws<std::invalid_argument>(
      [&] { server.registerTool({.name = "echo"}, first); }));
  CHECK(support::throws<std::invalid_argument>([&] {
    server.registerResource({.uri = "cursiv://x", .name = "y"}, text);
  }));
  const json::Value tools =
      ask(server, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
  CHECK(find(*find(tools, "result"), "tools")->array().size() == 1);
  const json::Value resources =
      ask(server, R"({"jsonrpc":"2.0","id":2,"method":"resources/list"})");
  CHECK(find(*find(resources, "result"), "resources")->array().size() == 1);
}

}  // namespace

void run() {
  initialize_echoes_a_known_protocol();
  instructions_come_with_the_handshake();
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
  a_name_is_registered_once();
}

}  // namespace mcp_server

// The default tools over a real workspace of two documents, one broken,
// answered through JSON.
namespace default_mcp_service {

namespace {

namespace json = lsp::json;

constexpr std::string_view kA = "file:///w/a.ws";
constexpr std::string_view kB = "file:///w/b.ws";
constexpr std::string_view kC = "file:///w/c.ws";

// The generated module a language reads with: the workspace's unless another
// is named.
struct GeneratedModule {
  std::unique_ptr<cursiv::Parser> (*parser)() =
      &ws::generated::Workspace::create_parser;
  std::unique_ptr<cursiv::AstReflection> (*reflection)() =
      &ws::generated::Workspace::create_ast_reflection;
  std::unique_ptr<const cursiv::GrammarSource> (*source)() =
      &ws::generated::Workspace::create_grammar_source;
};

constexpr GeneratedModule kCaret{
    &tcaret::generated::Caret::create_parser,
    &tcaret::generated::Caret::create_ast_reflection,
    &tcaret::generated::Caret::create_grammar_source};
constexpr GeneratedModule kDoc{&fdoc::generated::DocG::create_parser,
                               &fdoc::generated::DocG::create_ast_reflection,
                               &fdoc::generated::DocG::create_grammar_source};
constexpr GeneratedModule kMultiRef{
    &fmulti::generated::MultiG::create_parser,
    &fmulti::generated::MultiG::create_ast_reflection,
    &fmulti::generated::MultiG::create_grammar_source};

// The workspace's lock, with a hook around each read the test thread makes
// outside a write, ranked from 1, and `asking` called for each write the test
// thread asks for, before it waits for the readers to leave.
struct HookedLock final : cursiv::WorkspaceLock {
  explicit HookedLock(std::unique_ptr<cursiv::WorkspaceLock> wrapped)
      : inner(std::move(wrapped)) {}

  void write(WriteAction action, WriteMode mode) override {
    const bool mine = std::this_thread::get_id() == owner;
    if (mine && writing == 0 && asking) asking();
    if (mine) ++writing;
    struct Leave {
      bool mine;
      int& writing;
      ~Leave() {
        if (mine) --writing;
      }
    } leave{mine, writing};
    inner->write(action, mode);
  }
  [[nodiscard]] bool read(ReadAction action,
                          const cursiv::CancellationToken& token,
                          ReadPriority priority) override {
    if (std::this_thread::get_id() != owner || writing != 0)
      return inner->read(action, token, priority);
    const int rank = ++reads;
    // A refusal is what a cancelled wait answers.
    if (before && !before(rank)) return false;
    const bool admitted = inner->read(action, token, priority);
    if (after) after(rank);
    return admitted;
  }
  void allowImmediateReads() override { inner->allowImmediateReads(); }
  [[nodiscard]] bool immediateReadsAllowed() const override {
    return inner->immediateReadsAllowed();
  }
  void cancelWrite() override { inner->cancelWrite(); }

  std::unique_ptr<cursiv::WorkspaceLock> inner;
  std::thread::id owner = std::this_thread::get_id();
  int writing = 0;
  int reads = 0;
  std::function<bool(int)> before;
  std::function<void(int)> after;
  std::function<void()> asking;
};

struct World : support::World<> {
  World() { addLanguage("workspace", {".ws"}); }

  void addLanguage(
      std::string id, std::vector<std::string> extensions,
      std::string diagnostic = {}, GeneratedModule from = {},
      const std::function<void(cursiv::CoreServices&)>& tweak = {}) {
    (void)support::add_language<cursiv::CoreServices>(
        shared, {std::move(id), std::move(extensions), {}}, from.parser(),
        from.reflection(), [&](cursiv::CoreServices& language) {
          language.grammarSource = from.source();
          if (!diagnostic.empty())
            language.validationRegistry->registerAfterDocument(
                [message = std::move(diagnostic)](
                    const cursiv::Document&, cursiv::ValidationAcceptor& accept,
                    const cursiv::CancellationToken&) {
                  accept.accept(cursiv::TextRange{0, 0},
                                cursiv::DiagnosticSeverity::Warning, message);
                });
          if (tweak) tweak(language);
        });
  }

  HookedLock& hook() {
    auto hooked = std::make_unique<HookedLock>(std::move(shared.workspaceLock));
    HookedLock& lock = *hooked;
    shared.workspaceLock = std::move(hooked);
    return lock;
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
                 std::string_view argumentsJson,
                 const cursiv::CancellationToken& token = {}) {
  std::string request = R"({"jsonrpc":"2.0","id":1,"method":"tools/call",)";
  request += R"("params":{"name":")";
  request += name;
  request += R"(","arguments":)";
  request += argumentsJson;
  request += "}}";
  const std::optional<std::string> raw = server.handleText(request, token);
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

// One workspace for the whole test: building it is slow, and the tools only
// read it.
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

// A fix the validator attached reaches a caller with no editor: the
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

  // The case is ignored beyond ASCII too.
  World own;
  own.build({{kA,
              "datatype \xC3\x89\x63ole\nentity Z\xC3\xA8\x62re { x : "
              "\xC3\x89\x63ole }\n"}});
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto unicode = service.createServer();
  for (const std::string_view query :
       {R"({"query":"\u00e9cole"})", R"({"query":"\u00c9COLE"})"}) {
    const Answer found = call_tool(*unicode, "symbols", query);
    CHECK(contains(found.text, "\xC3\x89\x63ole (DataType)"));
    CHECK(!contains(found.text, "(Entity)"));
  }
  const Answer zebra = call_tool(*unicode, "symbols", R"({"query":"Z\u00c8"})");
  CHECK(contains(zebra.text, "Z\xC3\xA8\x62re (Entity)"));
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

// A terminal that reads a choice of words is answered by each word, in both
// of its forms, as the data rule reading the same words is by its keywords.
void expected_at_spells_the_words_of_a_terminal() {
  World own;
  own.addLanguage("words", {".words"}, {},
                  {&twords::generated::Lr::create_parser,
                   &twords::generated::Lr::create_ast_reflection,
                   &twords::generated::Lr::create_grammar_source});
  own.build({{"file:///w/b.words", "branch:\na "},
             {"file:///w/w.words", "whole:\na "},
             {"file:///w/r.words", "rule:\na "}});
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  cursiv::McpServer server = std::move(*service.createServer());
  for (const std::string_view uri :
       {"file:///w/b.words", "file:///w/w.words", "file:///w/r.words"}) {
    const Answer answer = call_tool(
        server, "expected-at",
        R"({"file":")" + std::string(uri) + R"(","line":2,"column":3})");
    CHECK(!answer.isError);
    CHECK(contains(answer.text, "'TRUE'"));
    CHECK(contains(answer.text, "'FALSE'"));
    CHECK(!contains(answer.text, "(token)"));
  }
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
  CHECK(contains(answer.text, "a.ws  closed"));
  CHECK(contains(answer.text, "b.ws"));
  CHECK(contains(answer.text, "1 diagnostic(s)"));
}

// What the workspace holds is readable, on disk or not, under a uri with no
// authority too.
void text_document_content_serves_the_held_text() {
  World own;
  own.build({{kA, "datatype D\nentity A { x : D }\n"},
             {"file:///w/a%23b%3Fc%25d.ws", "entity P { }\n"},
             {"untitled:Note.ws", "entity N { }\n"}});
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  cursiv::McpServer server = std::move(*service.createServer());
  const Answer untitled = call_tool(server, "text-document-content",
                                    R"({"file":"untitled:Note.ws"})");
  CHECK(!untitled.isError && untitled.text == "entity N { }\n");
  const Answer text = call_tool(server, "text-document-content",
                                R"({"file":"file:///w/a.ws"})");
  CHECK(!text.isError);
  CHECK(text.text == "datatype D\nentity A { x : D }\n");
  std::vector<std::string_view> spellings{
      R"({"file":"file:///w/a%23b%3Fc%25d.ws"})"};
  // Windows reads this path on the current drive, and forbids its `?`.
  if (std::filesystem::path::preferred_separator != '\\')
    spellings.push_back(R"({"file":"/w/a#b?c%d.ws"})");
  for (const std::string_view arguments : spellings) {
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
  // A position is a number with no fractional part, however it is written.
  const Answer spelled =
      call_tool(server, "hover",
                R"({"file":"file:///w/a.ws","line":2.0,"column":1.6e1})");
  CHECK(!spelled.isError && spelled.text == answer.text);
  for (const std::string_view position :
       {R"("line":1.5,"column":1)", R"("line":0.0,"column":1)",
        R"("line":1,"column":-1)", R"("line":1,"column":3e9)",
        R"("line":"1","column":1)"}) {
    const Answer refused =
        call_tool(server, "hover",
                  R"({"file":"file:///w/a.ws",)" + std::string(position) + "}");
    CHECK(refused.isError && contains(refused.text, "is 1-based"));
  }
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

// A text validated against the workspace's own symbols, then removed without
// a trace.
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
  // A dot name would leave the call's own slot once the uri is normalised.
  for (const std::string_view arguments :
       {R"({"text":"datatype D\n","languageId":"requested","file":".."})",
        R"({"text":"datatype D\n","languageId":"requested","file":"a/."})"}) {
    const Answer dot = call_tool(*server, "validate", arguments);
    CHECK(dot.isError && contains(dot.text, "'file' names a file"));
  }
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
  const std::string reserved =
      cursiv::path_to_file_uri("/.mcp-scratch/0/reserved.ws");
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

  // A validation through the editor's MCP tunnel also clears its temporary
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

// One validation at a time uses one scratch identity, handed back after: the
// store does not grow by one row per call.
void validate_reuses_one_scratch_identity() {
  cursiv::McpServer server = make_server();
  for (int i = 0; i < 3; ++i)
    (void)call_tool(server, "validate",
                    R"({"text":"datatype D\n","file":"again.ws"})");
  const cursiv::Documents& store = *world().shared.documents;
  CHECK(!store.hasDocument(
      cursiv::path_to_file_uri("/.mcp-scratch/0/again.ws")));
  CHECK(store.getDocumentId(cursiv::path_to_file_uri(
            "/.mcp-scratch/1/again.ws")) == cursiv::kNoDocument);
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

// A tool whose read never happened says so, rather than answer an empty
// finding: a refused read, a document removed or set back between the wait
// and the read. `validate` reads inside the write that built its text.
void a_tool_that_read_nothing_says_so() {
  World own;
  own.build({{kA, "datatype D\nentity A { x : D }\n"},
             {kB, "datatype E\n"},
             {kC, "datatype F\n"}});
  HookedLock& lock = own.hook();
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();
  // A positional tool reads three times: the id, the wait's handle, then the
  // admission.
  const auto at_admission = [&](std::function<void()> meanwhile) {
    lock.reads = 0;
    lock.before = [meanwhile = std::move(meanwhile)](int rank) {
      if (rank == 3) meanwhile();
      return true;
    };
  };

  lock.reads = 0;
  lock.before = [](int rank) { return rank != 3; };
  const Answer refused = call_tool(
      *server, "hover", R"({"file":"file:///w/a.ws","line":2,"column":16})");
  CHECK(refused.isError && contains(refused.text, "cancelled"));

  // A workspace tool reads once.
  for (const auto& [tool, arguments] :
       {std::pair{"get-diagnostics", "{}"}, std::pair{"symbols", "{}"},
        std::pair{"documents", "{}"},
        std::pair{"text-document-content", R"({"file":"file:///w/a.ws"})"}}) {
    lock.reads = 0;
    lock.before = [](int rank) { return rank != 1; };
    const Answer unread = call_tool(*server, tool, arguments);
    CHECK(unread.isError && contains(unread.text, "cancelled"));
  }

  lock.reads = 0;
  lock.before = [](int rank) { return rank != 3; };
  const Answer broken = call_tool(
      *server, "validate",
      R"({"text":"entity E { v : Nope }\n","languageId":"workspace"})");
  CHECK(!broken.isError && contains(broken.text, "[error at 1:"));

  at_admission([&] {
    own.shared.workspaceLock->write(
        [&](const cursiv::CancellationToken& token) {
          const std::string_view gone[] = {kB};
          own.shared.documentBuilder->update({}, gone, token);
        });
  });
  const Answer removed = call_tool(
      *server, "hover", R"({"file":"file:///w/b.ws","line":1,"column":10})");
  CHECK(removed.isError && contains(removed.text, "not in the workspace"));

  at_admission([&] {
    own.shared.workspaceLock->write([&](const cursiv::CancellationToken&) {
      own.shared.documentBuilder->resetToState(
          *own.shared.documents->getDocument(kC),
          cursiv::DocumentState::Changed);
    });
  });
  const Answer reset = call_tool(
      *server, "hover", R"({"file":"file:///w/c.ws","line":1,"column":10})");
  CHECK(reset.isError);
  lock.before = {};
}

// `references` reads every document's index, so it waits for the whole
// workspace: a document already indexed does not answer while another one
// that names it is still linking.
void references_wait_for_the_whole_workspace() {
  World own;
  own.build({{kA, "datatype D\n"}, {kB, "entity B { y : D }\n"}});
  struct Hold final : cursiv::DocumentPhaseListener {
    std::mutex mutex;
    std::condition_variable moved;
    bool holding = false;
    bool released = false;
    void onDocumentPhase(cursiv::Document& document,
                         cursiv::DocumentState reached,
                         const cursiv::CancellationToken&) override {
      if (document.uri != kB || reached != cursiv::DocumentState::Linked)
        return;
      std::unique_lock lock(mutex);
      holding = true;
      moved.notify_all();
      moved.wait(lock, [&] { return released; });
    }
    void release() {
      {
        const std::scoped_lock lock(mutex);
        released = true;
      }
      moved.notify_all();
    }
  } hold;
  HookedLock& lock = own.hook();
  own.shared.documentBuilder->addDocumentPhaseListener(hold);
  std::thread writer([&] {
    own.shared.workspaceLock->write(
        [&](const cursiv::CancellationToken& token) {
          const std::string_view changed[] = {kA, kB};
          own.shared.documentBuilder->update(changed, {}, token);
        });
  });
  {
    std::unique_lock wait(hold.mutex);
    hold.moved.wait(wait, [&] { return hold.holding; });
  }
  // B is let go once the tool has read, or once it has waited a while: a
  // tool that awaited `a.ws` alone has read by then, with B unindexed.
  lock.reads = 0;
  lock.after = [&](int rank) {
    if (rank == 3) hold.release();
  };
  std::thread release([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    hold.release();
  });
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();
  const Answer answer =
      call_tool(*server, "references",
                R"({"file":"file:///w/a.ws","line":1,"column":10})");
  release.join();
  writer.join();
  lock.after = {};
  own.shared.documentBuilder->removeDocumentPhaseListener(hold);
  CHECK(!answer.isError);
  CHECK(contains(answer.text, "b.ws:1:"));
}

// Every category runs, not only those an edit runs: the verdict is whole.
void validate_runs_every_category() {
  World own;
  own.addLanguage(
      "slowed", {".slowed"}, {}, {}, [](cursiv::CoreServices& language) {
        language.validationRegistry->registerAfterDocument(
            [](const cursiv::Document&, cursiv::ValidationAcceptor& accept,
               const cursiv::CancellationToken&) {
              accept.accept(cursiv::TextRange{0, 0},
                            cursiv::DiagnosticSeverity::Warning,
                            "slow check ran");
            },
            &cursiv::slow);
      });
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();
  const Answer answer = call_tool(
      *server, "validate", R"({"text":"datatype D\n","languageId":"slowed"})");
  CHECK(!answer.isError && contains(answer.text, "slow check ran"));
  CHECK(own.shared.documents->all().empty());
}

// The text is published once, with every category: no later build of the same
// write sets it back or tells a subscriber under an admitted reader. Run on a
// built workspace and on one never built, which is built first.
void validate_sets_nothing_back_under_an_admitted_reader() {
  for (const bool built : {true, false}) {
    World own;
    struct Watch final : cursiv::DocumentPhaseListener,
                         cursiv::DocumentUpdateListener {
      cursiv::WorkspaceLock* lock = nullptr;
      std::mutex mutex;
      std::condition_variable moved;
      cursiv::DocumentId scratch = cursiv::kNoDocument;
      int verdicts = 0;
      bool inside = false;
      bool asked = false;
      bool toldAfterAllowing = false;
      bool slowBesideReader = false;
      void onDocumentPhase(cursiv::Document& document,
                           cursiv::DocumentState reached,
                           const cursiv::CancellationToken&) override {
        if (reached != cursiv::DocumentState::Validated ||
            document.uri.find("/.mcp-scratch/") == std::string::npos)
          return;
        const std::scoped_lock hold(mutex);
        scratch = document.id;
        ++verdicts;
        moved.notify_all();
      }
      void onUpdate(std::span<const std::string_view> changed,
                    std::span<const std::string_view>) override {
        std::unique_lock hold(mutex);
        toldAfterAllowing = toldAfterAllowing || lock->immediateReadsAllowed();
        if (verdicts == 0 ||
            std::ranges::none_of(changed, [](std::string_view uri) {
              return uri.find("/.mcp-scratch/") != std::string_view::npos;
            }))
          return;
        (void)moved.wait_for(hold, std::chrono::seconds(5),
                             [&] { return inside; });
      }
      void slowRuns() {
        const std::scoped_lock hold(mutex);
        slowBesideReader = slowBesideReader || inside;
      }
      // A write asked once a verdict is out waits for the reader it let in.
      void ask() {
        const std::scoped_lock hold(mutex);
        if (verdicts == 0) return;
        asked = true;
        moved.notify_all();
      }
    } watch;
    own.addLanguage(
        "slowed", {".slowed"}, {}, {}, [&](cursiv::CoreServices& language) {
          language.validationRegistry->registerAfterDocument(
              [&](const cursiv::Document&, cursiv::ValidationAcceptor& accept,
                  const cursiv::CancellationToken&) {
                watch.slowRuns();
                accept.accept(cursiv::TextRange{0, 0},
                              cursiv::DiagnosticSeverity::Warning,
                              "slow check ran");
              },
              &cursiv::slow);
        });
    if (built) own.build({{kA, "datatype D\n"}});
    HookedLock& lock = own.hook();
    watch.lock = &lock;
    lock.asking = [&] { watch.ask(); };
    own.shared.documentBuilder->addDocumentPhaseListener(watch);
    own.shared.documentBuilder->addDocumentUpdateListener(watch);

    bool changedUnderReader = false;
    std::thread reader([&] {
      cursiv::DocumentId id = cursiv::kNoDocument;
      {
        std::unique_lock hold(watch.mutex);
        if (!watch.moved.wait_for(hold, std::chrono::seconds(5),
                                  [&] { return watch.verdicts > 0; }))
          return;
        id = watch.scratch;
      }
      // As an editor request does: the state, then a read beside the build.
      if (own.shared.documentBuilder->waitUntil(
              cursiv::DocumentState::Validated, id) !=
          cursiv::WaitOutcome::Reached)
        return;
      (void)lock.read(
          [&] {
            const std::shared_ptr<cursiv::Document> document =
                own.shared.documents->getDocument(id);
            if (document == nullptr ||
                document->state.load() < cursiv::DocumentState::Validated)
              return;
            const std::size_t seen = document->diagnostics.size();
            std::unique_lock hold(watch.mutex);
            const int verdicts = watch.verdicts;
            watch.inside = true;
            watch.moved.notify_all();
            (void)watch.moved.wait_for(hold, std::chrono::seconds(5), [&] {
              return watch.asked || watch.verdicts != verdicts;
            });
            watch.inside = false;
            changedUnderReader = document->diagnostics.size() != seen;
          },
          {}, cursiv::WorkspaceLock::ReadPriority::Immediate);
    });
    const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
    auto server = service.createServer();
    const Answer answer =
        call_tool(*server, "validate",
                  R"({"text":"datatype D\n","languageId":"slowed"})");
    reader.join();
    lock.asking = {};
    own.shared.documentBuilder->removeDocumentPhaseListener(watch);
    own.shared.documentBuilder->removeDocumentUpdateListener(watch);
    CHECK(!answer.isError && contains(answer.text, "slow check ran"));
    CHECK(watch.verdicts == 1);
    CHECK(!watch.slowBesideReader);
    CHECK(!changedUnderReader);
    CHECK(!watch.toldAfterAllowing);
    CHECK(own.shared.documents->all().size() == (built ? 1U : 0U));
  }
}

// A cancelled request stops the text's own build where it stands, even when no
// newer write supersedes it, and still removes the text.
void validate_stops_when_its_request_is_cancelled() {
  World own;
  std::stop_source request;
  bool stopped = false;
  own.addLanguage(
      "slowed", {".slowed"}, {}, {}, [&](cursiv::CoreServices& language) {
        language.validationRegistry->registerAfterDocument(
            [&](const cursiv::Document&, cursiv::ValidationAcceptor&,
                const cursiv::CancellationToken& token) {
              // The client gives up while a long check runs; the check polls
              // its token.
              request.request_stop();
              const auto deadline =
                  std::chrono::steady_clock::now() + std::chrono::seconds(5);
              while (!token.stop_requested() &&
                     std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
              stopped = token.stop_requested();
              cursiv::throw_if_cancelled(token);
            },
            &cursiv::slow);
      });
  own.build({{kA, "datatype D\n"}});
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();
  const Answer answer = call_tool(
      *server, "validate", R"({"text":"datatype D\n","languageId":"slowed"})",
      request.get_token());
  CHECK(stopped);
  CHECK(answer.isError && contains(answer.text, "cancelled"));
  CHECK(own.shared.documents->all().size() == 1);
  CHECK(own.shared.documentBuilder->currentState() ==
        cursiv::DocumentState::Validated);
}

// A document keeps the language it was opened with, whatever its uri says:
// the documentation of a declaration under a uri no extension routes is its
// language's still.
void hover_reads_the_language_the_document_was_opened_with() {
  World own;
  constexpr std::string_view kBare = "file:///w/bare";
  own.shared.workspaceLock->write([&](const cursiv::CancellationToken& token) {
    own.shared.documents->addDocument(
        own.shared.documentFactory->fromTextDocument(
            cursiv::TextDocument::create(
                std::string(kBare), "workspace", 0,
                "/** The data type. */\ndatatype D\nentity A { x : D }\n"),
            token));
    const std::string_view changed[] = {kBare};
    own.shared.documentBuilder->update(changed, {}, token);
  });
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();
  const Answer answer = call_tool(
      *server, "hover", R"({"file":"file:///w/bare","line":3,"column":16})");
  CHECK(!answer.isError && contains(answer.text, "The data type."));
}

// A description built by hand has no record: its document is named, a
// position is not.
void symbols_name_a_symbol_without_a_record() {
  World own;
  own.addLanguage(
      "synthetic", {".syn"}, {}, {}, [](cursiv::CoreServices& language) {
        struct Synthetic final : cursiv::DefaultScopeComputation {
          using DefaultScopeComputation::DefaultScopeComputation;
          [[nodiscard]] std::vector<cursiv::AstNodeDescription>
          collectExportedSymbols(
              const cursiv::Document& document,
              const cursiv::CancellationToken& token) const override {
            auto exported = DefaultScopeComputation::collectExportedSymbols(
                document, token);
            exported.push_back(
                {.name = "Synthetic", .documentId = document.id});
            return exported;
          }
        };
        language.scopeComputation = std::make_unique<Synthetic>(language);
      });
  own.build({{"file:///w/s.syn", "datatype D\n"}});
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();
  const Answer answer =
      call_tool(*server, "symbols", R"({"query":"Synthetic"})");
  CHECK(!answer.isError);
  CHECK(answer.text == "Synthetic  file:///w/s.syn\n");
}

// One name binding two declarations: each site is listed once, as the
// editor's references and rename list them.
void a_site_two_targets_share_is_listed_once() {
  World own;
  own.addLanguage("multiref", {".mref"}, {}, kMultiRef);
  own.build({{"file:///w/q.mref", "SELECT t FROM t, t"}});
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();
  const auto count = [](const std::string& text, std::string_view needle) {
    std::size_t n = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + 1))
      ++n;
    return n;
  };
  const Answer references =
      call_tool(*server, "references",
                R"({"file":"file:///w/q.mref","line":1,"column":8})");
  CHECK(!references.isError && contains(references.text, "2 target(s)"));
  CHECK(count(references.text, "q.mref:1:8\n") == 1);
  CHECK(count(references.text, "q.mref:") == 3);
  const Answer rename = call_tool(
      *server, "rename",
      R"({"file":"file:///w/q.mref","line":1,"column":8,"newName":"u"})");
  CHECK(!rename.isError && count(rename.text, "q.mref:1:8-1:9") == 1);
  CHECK(count(rename.text, "q.mref:") == 3);
}

// The outline nests each name under its nearest named ancestor, through
// containers that have none, as the editor's does; past its depth it says
// how much it leaves out.
void document_symbols_nest_through_unnamed_containers() {
  World own;
  own.addLanguage("doc", {".doc"}, {}, kDoc);
  std::string deep;
  for (int i = 0; i < 20; ++i) deep += R"({"k": )";
  deep += '1';
  for (int i = 0; i < 20; ++i) deep += '}';
  own.build({{"file:///w/n.doc", R"({"a": {"b": 1}, "c": [{"d": 2}]})"},
             {"file:///w/deep.doc", deep}});
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();
  const Answer nested =
      call_tool(*server, "document-symbols", R"({"file":"file:///w/n.doc"})");
  CHECK(!nested.isError);
  CHECK(contains(nested.text, "\"a\" (Member)  1:2\n  \"b\" (Member)  1:8\n"));
  CHECK(contains(nested.text, "\"c\" (Member)  1:17\n  \"d\" (Member)"));
  const Answer cut = call_tool(*server, "document-symbols",
                               R"({"file":"file:///w/deep.doc"})");
  CHECK(!cut.isError);
  CHECK(contains(cut.text, "... 3 nested declaration(s) not shown\n"));
}

// Every reference site the position admits is read, each through the class
// the grammar names for it, as a completion reads them.
void scope_candidates_read_every_site_through_its_own_class() {
  World own;
  own.addLanguage("caret", {".caret"}, {}, kCaret);
  constexpr std::string_view kEither = "fn f() { alpha a; beta b; either  }";
  constexpr std::string_view kNested =
      "fn f() { alpha a; beta b; nest a { tie  } }";
  own.build({{"file:///w/e.caret", kEither}, {"file:///w/n.caret", kNested}});
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();
  const auto at = [](std::string_view uri, std::string_view text,
                     std::string_view after) {
    return R"({"file":")" + std::string(uri) + R"(","line":1,"column":)" +
           std::to_string(text.find(after) + after.size() + 1) + "}";
  };
  const Answer either = call_tool(*server, "scope-candidates",
                                  at("file:///w/e.caret", kEither, "either "));
  CHECK(!either.isError);
  CHECK(contains(either.text, "candidates for member 'alpha':\na (Alpha)\n"));
  CHECK(contains(either.text, "candidates for member 'beta':\nb (Beta)\n"));
  const Answer tie = call_tool(*server, "scope-candidates",
                               at("file:///w/n.caret", kNested, "tie "));
  CHECK(!tie.isError);
  CHECK(contains(tie.text, "b (Beta)"));
  CHECK(!contains(tie.text, "a (Alpha)"));

  // A word is answered from its own node wherever the caret stands in it, as
  // completion answers it.
  World after;
  after.addLanguage("caret", {".caret"}, {}, kCaret);
  constexpr std::string_view kAfter =
      "fn f() { alpha a; nest a { beta c; } 'xy; }";
  after.build({{"file:///w/after.caret", kAfter}});
  const cursiv::DefaultMcpService afterService(after.shared, {"test-mcp", "0"});
  auto afterServer = afterService.createServer();
  const Answer atEnd = call_tool(*afterServer, "scope-candidates",
                                 at("file:///w/after.caret", kAfter, "'xy"));
  CHECK(!atEnd.isError && contains(atEnd.text, "a (Alpha)"));
  CHECK(!contains(atEnd.text, "c (Beta)"));
  for (const std::string_view inside : {"'x", "'", "} "}) {
    const Answer within =
        call_tool(*afterServer, "scope-candidates",
                  at("file:///w/after.caret", kAfter, inside));
    CHECK(!contains(within.text, "c (Beta)"));
    CHECK(within.text == atEnd.text);
  }
}

// A removal that failed keeps its slot out of use: the next validation of
// the same name gets an identity of its own, not the one still held. Runs
// last: the slots belong to the process.
void validate_abandons_a_slot_it_could_not_empty() {
  World own;
  bool armed = false;
  own.addLanguage(
      "fragile", {".fragile"}, {}, {}, [&](cursiv::CoreServices& language) {
        struct Fragile final : cursiv::DefaultLinker {
          Fragile(const cursiv::CoreServices& services, const bool& armed)
              : DefaultLinker(services), _armed(armed) {}
          void unlink(cursiv::Document& document) const override {
            if (_armed) throw std::runtime_error("unlink failed");
            DefaultLinker::unlink(document);
          }
          const bool& _armed;
        };
        language.linker = std::make_unique<Fragile>(language, armed);
      });
  // A broken link is repaired, and so let go of, by every removal.
  own.build({{"file:///w/b.fragile", "entity B { y : Missing }\n"}});
  const cursiv::DefaultMcpService service(own.shared, {"test-mcp", "0"});
  auto server = service.createServer();
  armed = true;
  const Answer failed = call_tool(
      *server, "validate", R"({"text":"datatype D\n","file":"kept.ws"})");
  armed = false;
  CHECK(failed.isError && contains(failed.text, "unlink failed"));
  const Answer retry = call_tool(*server, "validate",
                                 R"({"text":"datatype D\n","file":"kept.ws"})");
  CHECK(!retry.isError && contains(retry.text, "No diagnostics"));
}

}  // namespace

void run() {
  diagnostics_report_the_broken_documents_only();
  diagnostics_name_the_fixes_they_carry();
  symbols_list_the_exported_names();
  references_answer_declaration_and_uses();
  expected_at_names_the_member_being_filled();
  expected_at_answers_in_place_of_the_word_typed();
  expected_at_spells_the_words_of_a_terminal();
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
  a_tool_that_read_nothing_says_so();
  references_wait_for_the_whole_workspace();
  validate_runs_every_category();
  validate_sets_nothing_back_under_an_admitted_reader();
  validate_stops_when_its_request_is_cancelled();
  hover_reads_the_language_the_document_was_opened_with();
  symbols_name_a_symbol_without_a_record();
  a_site_two_targets_share_is_listed_once();
  document_symbols_nest_through_unnamed_containers();
  scope_candidates_read_every_site_through_its_own_class();
  validate_abandons_a_slot_it_could_not_empty();
}

}  // namespace default_mcp_service

// The LSP MCP service: the default tools plus the formatter's, over the
// workspace fixture.
namespace lsp_mcp_service {

namespace {

namespace json = lsp::json;

constexpr std::string_view kUri = "file:///w/messy.ws";
using support::kMessy;

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

// The formatter is asked as the editor asks it: the parameters name the
// document they format, and carry the indentation the call asked for.
void format_names_the_document_to_the_formatter() {
  struct Recording final : cursiv::Formatter {
    std::string* seen;
    lsp::Uint* tabSize;
    Recording(std::string* into, lsp::Uint* tabInto)
        : seen(into), tabSize(tabInto) {}
    [[nodiscard]] std::vector<lsp::TextEdit> formatDocument(
        const cursiv::Document&, const lsp::DocumentFormattingParams& params,
        const cursiv::CancellationToken&) const override {
      *seen = params.textDocument.uri.toString();
      *tabSize = params.options.tabSize;
      return {};
    }
    [[nodiscard]] std::vector<lsp::TextEdit> formatDocumentRange(
        const cursiv::Document&, const lsp::DocumentRangeFormattingParams&,
        const cursiv::CancellationToken&) const override {
      return {};
    }
  };
  World own;
  std::string seen;
  lsp::Uint tabSize = 0;
  own.services.language.formatter =
      std::make_unique<Recording>(&seen, &tabSize);
  const cursiv::LspMcpService service(*own.services.shared,
                                      {"test-lsp-mcp", "0"});
  cursiv::McpServer server = std::move(*service.createServer());
  const json::Value reply =
      ask(server,
          R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":)"
          R"({"name":"format","arguments":{"file":"file:///w/messy.ws"}}})");
  CHECK(reply.object().get("result").object().find("isError") == nullptr);
  CHECK(seen == kUri);
  CHECK(tabSize == 2);
  (void)ask(server,
            R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":)"
            R"({"name":"format","arguments":{"file":"file:///w/messy.ws",)"
            R"("tabSize":4.0}}})");
  CHECK(tabSize == 4);
}

// The `file` argument, as a uri.
std::string file_argument(const json::Value& arguments) {
  const json::Value* const file = cursiv::tool_argument(arguments, "file");
  if (file == nullptr || !file->isString())
    throw std::invalid_argument("'file' names the document");
  return cursiv::tool_file_uri(file->string());
}

// What a language project does: subclass, add its own tools with the public
// kit, and silence a default one by overriding its register method. A tool
// that reads every document's index waits for the whole workspace.
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
    const json::Value schema =
        cursiv::tool_object_schema(std::move(properties), std::move(required));
    server->registerTool(
        {.name = "byte-count",
         .title = "Byte Count",
         .description = "how many bytes a document holds",
         .inputSchema = schema},
        [&shared = _shared](const json::Value& arguments,
                            const cursiv::CancellationToken& token) {
          return cursiv::route_document_tool(
              shared, file_argument(arguments), cursiv::DocumentState::Parsed,
              token,
              [&](const cursiv::Document& document, const cursiv::CoreServices&,
                  std::string& out) {
                out = std::to_string(document.textDocument().getText().size()) +
                      " byte(s)";
              });
        });
    server->registerTool(
        {.name = "workspace-indexed",
         .title = "Workspace Indexed",
         .description = "whether every document's references are indexed",
         .inputSchema = schema},
        [&shared = _shared](const json::Value& arguments,
                            const cursiv::CancellationToken& token) {
          return cursiv::route_document_tool(
              shared, file_argument(arguments),
              cursiv::WorkspaceState::IndexedReferences, token,
              [&](const cursiv::Document&, const cursiv::CoreServices&,
                  std::string& out) {
                out = shared.documentBuilder->currentState() >=
                              cursiv::DocumentState::IndexedReferences
                          ? "indexed"
                          : "not indexed";
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
  const json::Value indexed = ask(
      server,
      R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":)"
      R"({"name":"workspace-indexed","arguments":{"file":"file:///w/messy.ws"}}})");
  CHECK(indexed.object()
            .get("result")
            .object()
            .get("content")
            .array()
            .front()
            .object()
            .get("text")
            .string() == "indexed");
}

}  // namespace

void run() {
  the_lsp_service_extends_the_default_set();
  format_answers_the_shaped_text();
  format_names_the_document_to_the_formatter();
  a_language_extends_and_prunes_the_set();
}

}  // namespace lsp_mcp_service

int main() {
  mcp_server::run();
  default_mcp_service::run();
  lsp_mcp_service::run();
  return cursiv::test::report("mcp");
}
