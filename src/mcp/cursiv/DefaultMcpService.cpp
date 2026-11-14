#include "cursiv/DefaultMcpService.hpp"

#include <lsp/json/json.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "cursiv/McpServer.hpp"
#include "cursiv/McpToolKit.hpp"
#include "cursiv/documentation/DocumentationProvider.hpp"
#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/grammar/GrammarSource.hpp"
#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/references/NameProvider.hpp"
#include "cursiv/references/References.hpp"
#include "cursiv/references/ScopeProvider.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/LanguageMetaData.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/symbols/NamedNodes.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstLeaf.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/CstUtils.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/text/Position.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/text/lower_case.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/JsonValue.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/validation/DefaultCodeActionProtocol.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/validation/SpellingActions.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentFactory.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/IndexManager.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"
#include "cursiv/workspace/ServiceRequirement.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/workspace/WorkspaceLock.hpp"

namespace cursiv {

namespace {

namespace json = lsp::json;

// Answers past this count are elided: a model reads the head of a list, and an
// unbounded workspace dump would drown the lines it asked for.
constexpr std::size_t kMaxLines = 200;
// An outline deeper than this says how much it leaves out below.
constexpr std::size_t kMaxOutlineDepth = 16;

constexpr std::string_view kSeverities[] = {"error", "warning", "information",
                                            "hint"};

void append_location(std::string& out, const Document& document,
                     const AstNode* node) {
  out += document.uri;
  const NodeId record = node->cstNodeId;
  if (record == kNoNode) return;
  out += ':';
  append_tool_position(out, document, document.parseResult.cst()[record].begin);
}

void append_diagnostics(std::string& out, const Document& document) {
  std::vector<const Diagnostic*> all;
  all.reserve(document.diagnostics.size());
  for (const Diagnostic& diagnostic : document.diagnostics)
    all.push_back(&diagnostic);
  const std::vector<JsonValue::Array> spelled =
      detail::spelling_actions(document, all);
  for (std::size_t i = 0; i < all.size(); ++i) {
    const Diagnostic& diagnostic = *all[i];
    out += '[';
    out += kSeverities[static_cast<unsigned>(diagnostic.severity) - 1];
    out += " at ";
    append_tool_position(out, document, diagnostic.range.begin);
    out += "] ";
    out += diagnostic.message;
    if (!diagnostic.source.empty()) {
      out += " (";
      out += diagnostic.source;
      out += ')';
    }
    // The fixes the runtime would have offered, by title: those the validator
    // attached and those a linking error gets on demand, for a caller with no
    // editor.
    const char* sep = " -- fix: ";
    const auto name = [&](const JsonValue& row) {
      if (!row.isObject()) return;
      const auto title = row.object().find(std::string(detail::kTitleField));
      if (title == row.object().end() || !title->second.isString()) return;
      out += sep;
      out += title->second.string();
      sep = "; ";
    };
    if (diagnostic.data.has_value() && diagnostic.data->isObject()) {
      const JsonValue::Object& data = diagnostic.data->object();
      const auto rows = data.find(std::string(detail::kDefaultCodeActionsKey));
      if (rows != data.end() && rows->second.isArray())
        for (const JsonValue& row : rows->second.array()) name(row);
    }
    for (const JsonValue& row : spelled[i]) name(row);
    out += '\n';
  }
}

// Every site naming one of `targets`, declarations included, collected as the
// editor's references and rename collect them.
std::vector<ReferenceDescription> references_to(
    const References& references, std::span<const AstNode* const> targets,
    const CancellationToken& token) {
  return detail::collect_references_per_declaration(
      references, targets,
      [](const AstNode&) {
        return FindReferencesOptions{.includeDeclaration = true};
      },
      token);
}

constexpr std::string_view kCancelled = "the request was cancelled";

// Runs `body` once, under a queued read of the workspace at the given state.
// Otherwise answers an error, `never` when that state is not coming.
std::optional<McpToolResult> read_workspace_at(
    const SharedCoreServices& shared, DocumentState target,
    std::string_view never, const CancellationToken& token,
    cursiv::function_ref<void()> body) {
  for (;;) {
    if (const WaitOutcome reached =
            shared.documentBuilder->waitUntil(target, token);
        reached != WaitOutcome::Reached)
      return McpToolResult{
          std::string(reached == WaitOutcome::Cancelled ? kCancelled : never),
          true};
    bool current = false;
    if (!shared.workspaceLock->read(
            [&] {
              if (shared.documentBuilder->currentState() < target) return;
              current = true;
              body();
            },
            token))
      return McpToolResult{std::string(kCancelled), true};
    if (current) return std::nullopt;
  }
}

// The grammar of the language the call names; the id is required. A miss
// answers with the served ids.
const GrammarSource* pick_grammar(const SharedCoreServices& shared,
                                  const json::Value& arguments,
                                  std::string& error) {
  const json::Value* const languageId = tool_argument(arguments, "languageId");
  if (languageId == nullptr || !languageId->isString())
    throw std::invalid_argument(
        "'languageId' names the grammar; the 'languages' tool lists them");
  std::string served;
  for (const CoreServices* const language : shared.serviceRegistry->all()) {
    if (language->grammarSource == nullptr) continue;
    if (language->languageMetaData.languageId == languageId->string())
      return language->grammarSource.get();
    if (!served.empty()) served += ", ";
    served += language->languageMetaData.languageId;
  }
  error = "no grammar is served for '" + languageId->string() + "'";
  if (!served.empty()) error += "; served: " + served;
  return nullptr;
}

// A scratch name is held for one call, an id forever: slots are numbered once
// and reused, so the store keeps one identity per slot and file name.
class ScratchSlots {
 public:
  [[nodiscard]] std::uint32_t acquire() {
    const std::scoped_lock lock(_mutex);
    if (_free.empty()) {
      // Room for every slot handed out, so a return never allocates.
      _free.reserve(_next + 1);
      return _next++;
    }
    const std::uint32_t slot = _free.back();
    _free.pop_back();
    return slot;
  }
  void release(std::uint32_t slot) noexcept {
    const std::scoped_lock lock(_mutex);
    _free.push_back(slot);
  }

 private:
  std::mutex _mutex;
  std::vector<std::uint32_t> _free;
  std::uint32_t _next = 0;
};

ScratchSlots& scratch_slots() {
  static ScratchSlots slots;
  return slots;
}

class ScratchLease {
 public:
  ScratchLease() = default;
  ScratchLease(const ScratchLease&) = delete;
  ScratchLease& operator=(const ScratchLease&) = delete;
  ~ScratchLease() {
    if (!_abandoned) scratch_slots().release(_slot);
  }

  [[nodiscard]] std::uint32_t slot() const noexcept { return _slot; }
  // The slot is never handed out again: its document could not be removed.
  void abandon() noexcept { _abandoned = true; }

 private:
  std::uint32_t _slot = scratch_slots().acquire();
  bool _abandoned = false;
};

struct ScratchInput {
  std::string name;
  std::string languageId;
};

// Whether `name` stays one file in its slot once the uri is normalised: a dot
// segment would leave the slot.
bool one_file(std::string_view name) {
  return !name.empty() && name != "." && name != ".." &&
         name.find_first_of("/\\") == std::string_view::npos;
}

ScratchInput scratch_input(const json::Value& arguments,
                           const SharedCoreServices& shared) {
  const CoreServices* language = nullptr;
  if (const json::Value* const languageId =
          tool_argument(arguments, "languageId");
      languageId != nullptr && languageId->isString()) {
    for (const CoreServices* const registered : shared.serviceRegistry->all())
      if (registered->languageMetaData.languageId == languageId->string()) {
        language = registered;
        break;
      }
    if (language == nullptr)
      throw std::invalid_argument("no language is registered as '" +
                                  languageId->string() + "'");
  }
  ScratchInput input;
  if (language != nullptr)
    input.languageId = language->languageMetaData.languageId;
  if (const json::Value* const file = tool_argument(arguments, "file");
      file != nullptr && file->isString()) {
    std::string_view name = file->string();
    if (const auto slash = name.find_last_of("/\\");
        slash != std::string_view::npos)
      name.remove_prefix(slash + 1);
    if (!one_file(name)) throw std::invalid_argument("'file' names a file");
    input.name = name;
    return input;
  }
  if (language != nullptr) {
    if (const LanguageMetaData& meta = language->languageMetaData;
        !meta.fileExtensions.empty())
      input.name = "text" + normalize_extension(meta.fileExtensions.front());
    else if (!meta.fileNames.empty())
      input.name = meta.fileNames.front();
    if (!one_file(input.name)) input.name = "text";
    return input;
  }
  throw std::invalid_argument("'languageId' or 'file' picks the language");
}

std::string scratch_uri(std::uint32_t slot, std::string_view name) {
  return path_to_file_uri("/.mcp-scratch/" + std::to_string(slot) + "/" +
                          std::string(name));
}

}  // namespace

DefaultMcpService::DefaultMcpService(SharedCoreServices& shared,
                                     McpServiceOptions options)
    : _shared(shared), _options(std::move(options)) {}

std::unique_ptr<McpServer> DefaultMcpService::createServer() const {
  auto server = std::make_unique<McpServer>(_options.name, _options.version);
  server->addInstructions(
      "A cursiv language workspace. Discover with 'languages'; learn a "
      "syntax from the cursiv://grammar/<languageId> resource or the "
      "'grammar' and 'rule' tools. Read state with 'documents', "
      "'text-document-content', 'document-symbols' and 'symbols'; navigate "
      "with 'references' and 'hover'. To write documents, loop on 'validate' "
      "(nothing is saved); 'expected-at' says what may be written at a "
      "position and 'scope-candidates' the valid targets of a "
      "cross-reference. Positions are 1-based, columns in utf-16 units; "
      "spans are end-exclusive.");
  registerDiagnosticsTool(*server);
  registerValidateTool(*server);
  registerDocumentsTool(*server);
  registerTextDocumentContentTool(*server);
  registerDocumentSymbolsTool(*server);
  registerSymbolsTool(*server);
  registerReferencesTool(*server);
  registerHoverTool(*server);
  registerRenameTool(*server);
  registerExpectedAtTool(*server);
  registerScopeCandidatesTool(*server);
  registerLanguagesTool(*server);
  registerGrammarResources(*server);
  registerGrammarTool(*server);
  registerRuleTool(*server);
  return server;
}

void DefaultMcpService::registerLanguagesTool(McpServer& server) const {
  server.registerTool(
      {.name = "languages",
       .title = "Registered Languages",
       .description =
           "Lists every language this workspace serves: its id, the file "
           "extensions and names it claims, and whether its grammar text is "
           "available (the 'grammar' and 'rule' tools, and the "
           "cursiv://grammar/<languageId> resource)."},
      [&shared = _shared](const json::Value&, const CancellationToken&) {
        std::string out;
        for (const CoreServices* const language :
             shared.serviceRegistry->all()) {
          const LanguageMetaData& meta = language->languageMetaData;
          out += meta.languageId;
          for (const std::string& extension : meta.fileExtensions) {
            out += ' ';
            out += extension;
          }
          for (const std::string& name : meta.fileNames) {
            out += ' ';
            out += name;
          }
          out += language->grammarSource != nullptr
                     ? "  grammar: cursiv://grammar/" + meta.languageId
                     : "  (no grammar text served)";
          out += '\n';
        }
        return McpToolResult{out.empty() ? "no language is registered"
                                         : std::move(out)};
      });
}

void DefaultMcpService::registerGrammarTool(McpServer& server) const {
  json::Object properties;
  properties["languageId"] = tool_property(
      "string", "which language's grammar; the 'languages' tool lists them");
  json::Array required;
  required.emplace_back(json::String("languageId"));
  server.registerTool(
      {.name = "grammar",
       .title = "Grammar Text",
       .description =
           "The whole grammar of one language, as reviewable text. For a "
           "single rule and its dependencies, call 'rule'.",
       .inputSchema =
           tool_object_schema(std::move(properties), std::move(required))},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken&) {
        std::string error;
        const GrammarSource* const grammar =
            pick_grammar(shared, arguments, error);
        if (grammar == nullptr) return McpToolResult{std::move(error), true};
        return McpToolResult{std::string(grammar->grammarText())};
      });
}

void DefaultMcpService::registerGrammarResources(McpServer& server) const {
  for (const CoreServices* const language : _shared.serviceRegistry->all()) {
    const GrammarSource* const grammar = language->grammarSource.get();
    if (grammar == nullptr) continue;
    const std::string& id = language->languageMetaData.languageId;
    server.registerResource(
        {.uri = "cursiv://grammar/" + id,
         .name = id + " grammar",
         .description = "the " + id + " grammar, as reviewable text",
         .mimeType = "text/plain"},
        [grammar] { return std::string(grammar->grammarText()); });
  }
}

void DefaultMcpService::registerRuleTool(McpServer& server) const {
  json::Object properties;
  properties["name"] = tool_property("string", "the rule's name");
  properties["languageId"] = tool_property(
      "string", "which language's grammar; the 'languages' tool lists them");
  json::Array required;
  required.emplace_back(json::String("name"));
  required.emplace_back(json::String("languageId"));
  server.registerTool(
      {.name = "rule",
       .title = "Grammar Rule",
       .description =
           "One grammar rule and every rule it reaches, as text. The whole "
           "grammar is the cursiv://grammar/<languageId> resource.",
       .inputSchema =
           tool_object_schema(std::move(properties), std::move(required))},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken&) {
        const json::Value* const name = tool_argument(arguments, "name");
        if (name == nullptr || !name->isString())
          throw std::invalid_argument("'name' is the rule to print");
        std::string error;
        const GrammarSource* const grammar =
            pick_grammar(shared, arguments, error);
        if (grammar == nullptr) return McpToolResult{std::move(error), true};
        std::string closure = grammar->ruleClosure(name->string());
        if (closure.empty()) {
          std::string miss =
              "no rule named '" + name->string() + "'; the rules are:\n";
          for (const std::string_view rule : grammar->ruleNames()) {
            miss += rule;
            miss += '\n';
          }
          return McpToolResult{std::move(miss), true};
        }
        return McpToolResult{std::move(closure)};
      });
}

void DefaultMcpService::registerDiagnosticsTool(McpServer& server) const {
  json::Object properties;
  properties["directory"] = tool_property(
      "string", "restrict to the documents under this directory (optional)");
  server.registerTool(
      {.name = "get-diagnostics",
       .title = "Get Diagnostics",
       .description =
           "Retrieves diagnostics from the language server for all files in "
           "the given directory, ordered by file.",
       .inputSchema = tool_object_schema(std::move(properties), {})},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken& token) {
        std::string out;
        if (std::optional<McpToolResult> unread = read_workspace_at(
                shared, DocumentState::Validated,
                "the workspace never reached validation", token, [&] {
                  const json::Value* const directory =
                      tool_argument(arguments, "directory");
                  std::vector<std::shared_ptr<Document>> documents =
                      directory != nullptr && directory->isString()
                          ? shared.documents->getDocuments(
                                tool_file_uri(directory->string()))
                          : shared.documents->all();
                  std::ranges::sort(documents,
                                    [](const auto& a, const auto& b) {
                                      return a->uri < b->uri;
                                    });
                  for (const std::shared_ptr<Document>& document : documents) {
                    if (document == nullptr || document->diagnostics.empty())
                      continue;
                    out += document->uri;
                    out += '\n';
                    append_diagnostics(out, *document);
                  }
                }))
          return std::move(*unread);
        return McpToolResult{out.empty() ? "No diagnostics found."
                                         : std::move(out)};
      });
}

void DefaultMcpService::registerValidateTool(McpServer& server) const {
  json::Object properties;
  properties["text"] = tool_property("string", "the document text to validate");
  properties["languageId"] =
      tool_property("string", "language of the text (or pass 'file')");
  properties["file"] =
      tool_property("string", "a file name whose extension picks the language");
  json::Array required;
  required.emplace_back(json::String("text"));
  server.registerTool(
      {.name = "validate",
       .title = "Validate Text",
       .description =
           "Parses a text, links it against the workspace's symbols, runs "
           "every validation category and answers the diagnostics. Nothing "
           "is saved: the text is discarded afterwards. Pass 'languageId' or "
           "a 'file' name to pick the language.",
       .inputSchema =
           tool_object_schema(std::move(properties), std::move(required))},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken& token) {
        const json::Value* const text = tool_argument(arguments, "text");
        if (text == nullptr || !text->isString())
          throw std::invalid_argument("'text' is the document to validate");
        const ScratchInput input = scratch_input(arguments, shared);
        ScratchLease lease;
        const std::string uri = scratch_uri(lease.slot(), input.name);
        const CoreServices* const language =
            shared.serviceRegistry->findServices(uri, input.languageId);
        if (language == nullptr)
          return McpToolResult{"no language answers for '" + uri + "'", true};
        const std::string_view uriView = uri;
        bool installed = false;
        // A newer write must not cancel the deletion of this call's document.
        // A deletion that failed leaves the document held: its slot is never
        // handed out again, so no later call collides with it.
        const auto discard = [&] {
          if (!installed) return;
          try {
            shared.workspaceLock->write(
                [&](const CancellationToken& writeToken) {
                  shared.documentBuilder->update({}, {&uriView, 1}, writeToken);
                },
                WorkspaceLock::WriteMode::Protected);
          } catch (...) {
            lease.abandon();
            throw;
          }
          installed = false;
        };
        McpToolResult result;
        try {
          // One build per write: what a superseded build left is built first,
          // alone, then the text is installed, built with every category and
          // read in that write, so no second build sets it back under a reader.
          for (bool answered = false; !answered;) {
            if (token.stop_requested()) {
              result = {std::string(kCancelled), true};
              break;
            }
            shared.workspaceLock->write(
                [&](const CancellationToken& writeToken) {
                  DocumentBuilder& builder = *shared.documentBuilder;
                  if (builder.currentState() < DocumentState::Validated) {
                    // Under the write's token alone: work the request gave up
                    // on would wait for a write that may never come.
                    builder.update({}, {}, writeToken);
                    return;
                  }
                  // The text's own work stops on a newer write or on the
                  // request; what it leaves unbuilt, the removal's build
                  // finishes.
                  std::stop_source stop;
                  const std::stop_callback byRequest(
                      token, [&] { stop.request_stop(); });
                  const std::stop_callback byWrite(
                      writeToken, [&] { stop.request_stop(); });
                  const CancellationToken either = stop.get_token();
                  if (!installed) {
                    shared.documents->addDocument(
                        shared.documentFactory->fromTextDocument(
                            TextDocument::create(
                                uri, language->languageMetaData.languageId, 0,
                                text->string()),
                            either));
                    installed = true;
                  }
                  const std::shared_ptr<Document> document =
                      shared.documents->getDocument(uri);
                  if (document == nullptr) {
                    answered = true;
                    result = {"the text to validate was removed", true};
                    return;
                  }
                  Document* const built = document.get();
                  builder.build({&built, 1}, {}, either);
                  answered = true;
                  if (document->state.load() < DocumentState::Validated) {
                    result = {"the validation build did not finish", true};
                    return;
                  }
                  std::string out;
                  append_diagnostics(out, *document);
                  result = {out.empty() ? "No diagnostics: the text is valid."
                                        : std::move(out)};
                },
                WorkspaceLock::WriteMode::Protected);
          }
        } catch (...) {
          // The failure that ended the call is the one reported.
          try {
            discard();
          } catch (...) {
          }
          throw;
        }
        discard();
        return result;
      });
}

void DefaultMcpService::registerSymbolsTool(McpServer& server) const {
  json::Object properties;
  properties["query"] =
      tool_property("string",
                    "case-insensitive substring of the name (optional; empty "
                    "lists every exported symbol)");
  server.registerTool(
      {.name = "symbols",
       .title = "Workspace Symbols",
       .description = "Lists the symbols every document exports, with their "
                      "declared type and location.",
       .inputSchema = tool_object_schema(std::move(properties), {})},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken& token) {
        const json::Value* const query = tool_argument(arguments, "query");
        // Both sides lowered by the Unicode mapping, as a client's
        // `toLowerCase` lowers them: `école` finds `École`.
        const std::string needle = detail::lower_case(
            query != nullptr && query->isString() ? query->string() : "");
        std::string out;
        std::size_t matches = 0;
        if (std::optional<McpToolResult> unread = read_workspace_at(
                shared, DocumentState::IndexedContent,
                "the workspace never reached indexing", token, [&] {
                  (void)shared.indexManager->visitElements(
                      nullptr, {}, [&](const AstNodeDescription& description) {
                        throw_if_cancelled(token);
                        if (!needle.empty() &&
                            detail::lower_case(description.name).find(needle) ==
                                std::string::npos)
                          return true;
                        if (++matches > kMaxLines) return false;
                        out += description.name;
                        if (description.type != nullptr) {
                          out += " (";
                          out += description.type->name;
                          out += ')';
                        }
                        if (const std::shared_ptr<Document> owningDocument =
                                shared.documents->getDocument(
                                    description.documentId);
                            owningDocument != nullptr) {
                          out += "  ";
                          out += owningDocument->uri;
                          const CoreServices* const language =
                              owningDocument->language;
                          const CstNodeView nameNode =
                              language == nullptr || description.node == nullptr
                                  ? CstNodeView{}
                                  : language->nameProvider->getNameNode(
                                        *description.node,
                                        owningDocument.get());
                          // A node built by hand has no record to point at: its
                          // uri alone says where it lives.
                          const NodeId at =
                              nameNode ? nameNode.id : description.symbolId;
                          if (const Cst& cst =
                                  owningDocument->parseResult.cst();
                              at != kNoNode && at < cst.size()) {
                            out += ':';
                            append_tool_position(out, *owningDocument,
                                                 cst[at].begin);
                          }
                        }
                        out += '\n';
                        return true;
                      });
                }))
          return std::move(*unread);
        // No count: the visitor stops the walk at the cap, so what is past it
        // was never enumerated.
        if (matches > kMaxLines)
          out += "... more results elided (narrow with 'query')\n";
        return McpToolResult{out.empty() ? "No symbols found."
                                         : std::move(out)};
      });
}

void DefaultMcpService::registerReferencesTool(McpServer& server) const {
  server.registerTool(
      {.name = "references",
       .title = "Find References",
       .description =
           "What the name at a position denotes, and every reference to it "
           "across the workspace (declaration included).",
       .inputSchema = tool_position_schema()},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken& token) {
        const McpToolPosition at = tool_position(arguments);
        McpToolResult result = route_document_tool(
            shared, at.uri, WorkspaceState::IndexedReferences, token,
            [&](const Document& document, const CoreServices& language,
                std::string& out) {
              const TextOffset offset = document.textDocument().offsetAt(
                  Position{at.line, at.column});
              const Declaration declaration =
                  language.references->findDeclarations(document, offset,
                                                        token);
              if (!declaration) {
                if (declaration.span.begin != declaration.span.end)
                  out = "the name here resolves to nothing";
                return;
              }
              const std::string_view name =
                  document.textDocument().getText().substr(
                      declaration.span.begin,
                      declaration.span.end - declaration.span.begin);
              out += '\'';
              out += name;
              out += "' -- ";
              out += std::to_string(declaration.targets.size());
              out += " target(s)\n";
              for (const ReferenceDescription& reference : references_to(
                       *language.references, declaration.targets, token)) {
                throw_if_cancelled(token);
                const std::shared_ptr<Document> owningDocument =
                    shared.documents->getDocument(reference.sourceDocumentId);
                if (owningDocument == nullptr) continue;
                out += owningDocument->uri;
                out += ':';
                append_tool_position(out, *owningDocument,
                                     reference.sourceBegin);
                out += '\n';
              }
            });
        if (!result.isError && result.text.empty())
          result.text = "nothing is denoted at this position";
        return result;
      });
}

void DefaultMcpService::registerHoverTool(McpServer& server) const {
  server.registerTool(
      {.name = "hover",
       .title = "Hover",
       .description =
           "What the name at a position denotes: its declared type, where it "
           "is declared, and the documentation its author wrote.",
       .inputSchema = tool_position_schema()},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken& token) {
        const McpToolPosition at = tool_position(arguments);
        McpToolResult result = route_document_tool(
            shared, at.uri, DocumentState::Linked, token,
            [&](const Document& document, const CoreServices& language,
                std::string& out) {
              const TextOffset offset = document.textDocument().offsetAt(
                  Position{at.line, at.column});
              const Declaration declaration =
                  language.references->findDeclarations(document, offset,
                                                        token);
              if (!declaration) return;
              const std::string_view name =
                  document.textDocument().getText().substr(
                      declaration.span.begin,
                      declaration.span.end - declaration.span.begin);
              for (const AstNode* const target : declaration.targets) {
                throw_if_cancelled(token);
                out += '\'';
                out += name;
                out += '\'';
                if (target->type != nullptr) {
                  out += " (";
                  out += target->type->name;
                  out += ')';
                }
                const Document* const owningDocument = target->getDocument();
                if (owningDocument != nullptr) {
                  out += "  declared at ";
                  append_location(out, *owningDocument, target);
                }
                out += '\n';
                // The prose is the declaration's language's: a global
                // candidate may come from another grammar.
                if (owningDocument == nullptr) continue;
                const CoreServices* const owner = owningDocument->language;
                const DocumentationProvider* const docs =
                    owner == nullptr ? nullptr
                                     : owner->documentationProvider.get();
                if (docs == nullptr) continue;
                if (const std::optional<std::string> prose =
                        docs->getDocumentation(*target, owningDocument)) {
                  out += *prose;
                  out += '\n';
                }
              }
            });
        if (!result.isError && result.text.empty())
          result.text = "nothing is denoted at this position";
        return result;
      });
}

void DefaultMcpService::registerRenameTool(McpServer& server) const {
  json::Object properties;
  properties["file"] =
      tool_property("string", "path or file:// uri of the document");
  properties["line"] = tool_property("integer", "1-based line");
  properties["column"] =
      tool_property("integer", "1-based column, in utf-16 units");
  properties["newName"] = tool_property("string", "the replacement name");
  json::Array required;
  required.emplace_back(json::String("file"));
  required.emplace_back(json::String("line"));
  required.emplace_back(json::String("column"));
  required.emplace_back(json::String("newName"));
  server.registerTool(
      {.name = "rename",
       .title = "Rename",
       .description =
           "Every span to edit when renaming the name at a position, "
           "declaration included. Nothing is applied, and the language's own "
           "naming refusals are not checked: apply the edits yourself and "
           "validate.",
       .inputSchema =
           tool_object_schema(std::move(properties), std::move(required))},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken& token) {
        const McpToolPosition at = tool_position(arguments);
        const json::Value* const newName = tool_argument(arguments, "newName");
        if (newName == nullptr || !newName->isString())
          throw std::invalid_argument("'newName' is the replacement");
        McpToolResult result = route_document_tool(
            shared, at.uri, WorkspaceState::IndexedReferences, token,
            [&](const Document& document, const CoreServices& language,
                std::string& out) {
              const TextOffset offset = document.textDocument().offsetAt(
                  Position{at.line, at.column});
              const Declaration declaration =
                  language.references->findDeclarations(document, offset,
                                                        token);
              if (declaration.span.begin == declaration.span.end) return;
              const std::string_view name =
                  document.textDocument().getText().substr(
                      declaration.span.begin,
                      declaration.span.end - declaration.span.begin);
              out += "replace '";
              out += name;
              out += "' with '";
              out += newName->string();
              out += "' at (1-based, end-exclusive):\n";
              const auto edit = [&](const Document& owningDocument,
                                    TextOffset begin, TextOffset end) {
                out += owningDocument.uri;
                out += ':';
                append_tool_position(out, owningDocument, begin);
                out += '-';
                append_tool_position(out, owningDocument, end);
                out += '\n';
              };
              // A typo that resolved to nothing still renames itself.
              if (declaration.targets.empty()) {
                edit(document, declaration.span.begin, declaration.span.end);
                return;
              }
              for (const ReferenceDescription& reference : references_to(
                       *language.references, declaration.targets, token)) {
                throw_if_cancelled(token);
                const std::shared_ptr<Document> owningDocument =
                    shared.documents->getDocument(reference.sourceDocumentId);
                if (owningDocument != nullptr)
                  edit(*owningDocument, reference.sourceBegin,
                       reference.sourceEnd);
              }
            });
        if (!result.isError && result.text.empty())
          result.text = "nothing renamable at this position";
        return result;
      });
}

void DefaultMcpService::registerDocumentsTool(McpServer& server) const {
  server.registerTool(
      {.name = "documents",
       .title = "Workspace Documents",
       .description =
           "Lists the documents the workspace holds: uri, whether an editor "
           "holds it open, the client's version stamp, and how many "
           "diagnostics it carries so far."},
      [&shared = _shared](const json::Value&, const CancellationToken& token) {
        std::string out;
        if (!shared.workspaceLock->read(
                [&] {
                  std::vector<std::shared_ptr<Document>> documents =
                      shared.documents->all();
                  std::ranges::sort(documents,
                                    [](const auto& a, const auto& b) {
                                      return a->uri < b->uri;
                                    });
                  for (const std::shared_ptr<Document>& document : documents) {
                    if (document == nullptr) continue;
                    out += document->uri;
                    // An editor's hold is all that is known: a closed document
                    // may have no file behind it.
                    out += document->isOpen ? "  open" : "  closed";
                    out += "  version ";
                    out += std::to_string(document->textDocument().version());
                    if (const std::size_t n = document->diagnostics.size();
                        n != 0)
                      out += "  " + std::to_string(n) + " diagnostic(s)";
                    out += '\n';
                  }
                },
                token))
          return McpToolResult{std::string(kCancelled), true};
        return McpToolResult{out.empty() ? "the workspace holds no document"
                                         : std::move(out)};
      });
}

void DefaultMcpService::registerTextDocumentContentTool(
    McpServer& server) const {
  json::Object properties;
  properties["file"] =
      tool_property("string", "path or file:// uri of the document");
  json::Array required;
  required.emplace_back(json::String("file"));
  server.registerTool(
      {.name = "text-document-content",
       .title = "Text Document Content",
       .description =
           "The CURRENT text of a document as the workspace holds it -- an "
           "unsaved editor buffer included. The 'documents' tool lists what "
           "is held.",
       .inputSchema =
           tool_object_schema(std::move(properties), std::move(required))},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken& token) {
        const json::Value* const file = tool_argument(arguments, "file");
        if (file == nullptr || !file->isString())
          throw std::invalid_argument("'file' names the document");
        const std::string key = tool_file_uri(file->string());
        std::string out;
        bool found = false;
        if (!shared.workspaceLock->read(
                [&] {
                  const std::shared_ptr<Document> document =
                      shared.documents->getDocument(key);
                  if (document == nullptr) return;
                  found = true;
                  out = std::string(document->textDocument().getText());
                },
                token))
          return McpToolResult{std::string(kCancelled), true};
        if (!found)
          return McpToolResult{
              key + " is not in the workspace; the 'documents' tool lists it",
              true};
        return McpToolResult{std::move(out)};
      });
}

void DefaultMcpService::registerDocumentSymbolsTool(McpServer& server) const {
  json::Object properties;
  properties["file"] =
      tool_property("string", "path or file:// uri of the document");
  json::Array required;
  required.emplace_back(json::String("file"));
  server.registerTool(
      {.name = "document-symbols",
       .title = "Document Symbols",
       .description =
           "The outline of one document, as the editor shows it: every named "
           "node under its nearest named ancestor, each with its type and "
           "position, down to 16 levels below the top.",
       .inputSchema =
           tool_object_schema(std::move(properties), std::move(required))},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken& token) {
        const json::Value* const file = tool_argument(arguments, "file");
        if (file == nullptr || !file->isString())
          throw std::invalid_argument("'file' names the document");
        McpToolResult result = route_document_tool(
            shared, tool_file_uri(file->string()), DocumentState::Parsed, token,
            [&](const Document& document, const CoreServices& language,
                std::string& out) {
              const detail::NamedNodes named = detail::collect_named_nodes(
                  document, *language.nameProvider, token);
              // Postorder: an owner comes after what it owns, so each list
              // below is in text order and each count is final when read.
              std::vector<std::vector<std::size_t>> owned(named.nodes.size());
              std::vector<std::size_t> roots;
              std::vector<std::size_t> below(named.nodes.size(), 0);
              for (std::size_t i = 0; i < named.nodes.size(); ++i) {
                const std::size_t owner = named.owners[i];
                if (owner == detail::NamedNodes::kNoOwner) {
                  roots.push_back(i);
                  continue;
                }
                owned[owner].push_back(i);
                below[owner] += below[i] + 1;
              }
              const Cst& cst = document.parseResult.cst();
              const auto print = [&](auto&& self, std::size_t i,
                                     std::size_t depth) -> void {
                throw_if_cancelled(token);
                const AstNode& node = *named.nodes[i];
                out.append(2 * depth, ' ');
                out += language.nameProvider->getName(node).value_or(
                    named.nameNodes[i].text());
                if (node.type != nullptr) {
                  out += " (";
                  out += node.type->name;
                  out += ')';
                }
                out += "  ";
                append_tool_position(out, document, cst[node.cstNodeId].begin);
                out += '\n';
                if (owned[i].empty()) return;
                if (depth == kMaxOutlineDepth) {
                  out.append(2 * depth + 2, ' ');
                  out += "... ";
                  out += std::to_string(below[i]);
                  out += " nested declaration(s) not shown\n";
                  return;
                }
                for (const std::size_t child : owned[i])
                  self(self, child, depth + 1);
              };
              for (const std::size_t root : roots) print(print, root, 0);
            });
        if (!result.isError && result.text.empty())
          result.text = "this document declares no symbol";
        return result;
      });
}

namespace {

// The features the engine answers at `offset`, asked as completion asks: in
// place of the word at the caret, else at the caret alone. `word` is what they
// replace, empty for a bare insertion.
std::vector<NextFeature> features_at(const Document& document,
                                     const Parser& parser, TextOffset offset,
                                     CstLeaf& word) {
  const Cst& cst = document.parseResult.cst();
  std::vector<NextFeature> nexts;
  word = detail::word_at(cst, offset);
  if (word) {
    parser.nextFeaturesAt(cst, word.begin, word.end, nexts);
    if (!nexts.empty() || offset != word.end || offset == word.begin)
      return nexts;
    word = {};
  }
  parser.nextFeaturesAt(cst, offset, offset, nexts);
  return nexts;
}

}  // namespace

void DefaultMcpService::registerExpectedAtTool(McpServer& server) const {
  server.registerTool(
      {.name = "expected-at",
       .title = "Expected At Position",
       .description =
           "What the grammar accepts at a position: keywords to insert, "
           "token categories to name, and the member each one fills. A "
           "position ending a word is answered in the word's place, and the "
           "answer says so first. For the valid targets of a cross-reference "
           "member, call scope-candidates.",
       .inputSchema = tool_position_schema()},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken& token) {
        const McpToolPosition at = tool_position(arguments);
        McpToolResult result = route_document_tool(
            shared, at.uri, DocumentState::Linked, token,
            [&](const Document& document, const CoreServices& language,
                std::string& out) {
              const Grammar& grammar = language.parser->getGrammar();
              const TextOffset offset = document.textDocument().offsetAt(
                  Position{at.line, at.column});
              std::vector<std::string> lines;
              CstLeaf word;
              for (const NextFeature& next :
                   features_at(document, *language.parser, offset, word)) {
                throw_if_cancelled(token);
                // A keyword by its text, a terminal that reads a choice of
                // words by each of them, any other terminal by its name.
                std::vector<std::string> heads;
                for (const std::string_view spelled :
                     grammar.terminalSpellings(next.feature))
                  heads.push_back('\'' + std::string(spelled) + '\'');
                if (heads.empty()) {
                  const std::string_view name =
                      grammar.terminalName(next.feature);
                  if (name.empty()) continue;
                  heads.push_back(std::string(name) + " (token)");
                }
                std::string tail;
                if (!next.property.empty()) {
                  tail += "  fills '";
                  tail += next.property;
                  tail += '\'';
                }
                if (const std::string_view doc =
                        grammar.terminalDoc(next.feature);
                    !doc.empty()) {
                  tail += "  -- ";
                  tail += doc;
                }
                for (std::string& line : heads) {
                  line += tail;
                  if (std::ranges::find(lines, line) == lines.end())
                    lines.push_back(std::move(line));
                }
              }
              if (word && !lines.empty()) {
                out += "in place of '";
                out += document.parseResult.cst().text().substr(
                    word.begin, word.end - word.begin);
                out += "'\n";
              }
              for (const std::string& line : lines) {
                out += line;
                out += '\n';
              }
            });
        if (!result.isError && result.text.empty())
          result.text = "nothing may be written at this position";
        return result;
      });
}

void DefaultMcpService::registerScopeCandidatesTool(McpServer& server) const {
  server.registerTool(
      {.name = "scope-candidates",
       .title = "Cross-Reference Candidates",
       .description =
           "The valid targets of each cross-reference that may be written at "
           "a position, member by member, as the scope at that site answers "
           "them.",
       .inputSchema = tool_position_schema()},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken& token) {
        const McpToolPosition at = tool_position(arguments);
        McpToolResult result = route_document_tool(
            shared, at.uri, DocumentState::Linked, token,
            [&](const Document& document, const CoreServices& language,
                std::string& out) {
              const TextOffset offset = document.textDocument().offsetAt(
                  Position{at.line, at.column});
              const Cst& cst = document.parseResult.cst();
              CstLeaf word;
              const std::vector<NextFeature> features =
                  features_at(document, *language.parser, offset, word);
              // From the word's own node when the caret is in a word, as
              // completion reads; else from the node before the caret, or the
              // root when none precedes it.
              AstNode* container =
                  word ? owner_of_leaf(cst, word.id) : owner_at(cst, offset);
              if (container == nullptr) container = document.parseResult.value;
              if (container == nullptr) return;
              // One list per reference member, in the order the grammar names
              // them: every site the position admits is read, and the sites
              // of one member add up their offers, as a completion's do.
              struct Member {
                std::string_view property;
                std::uint16_t reference = kNoReference;
                std::string lines;
              };
              std::vector<Member> members;
              using Seen =
                  std::tuple<std::size_t, std::string_view, const AstTypeTag*>;
              std::set<Seen> seen;
              std::vector<NextFeature> asked;
              std::size_t matches = 0;
              for (const NextFeature& next : features) {
                throw_if_cancelled(token);
                if (matches > kMaxLines) break;
                // Two terminals filling one site ask the same scope.
                const auto site = [&](const NextFeature& other) {
                  return other.property == next.property &&
                         other.narrow == next.narrow &&
                         other.type == next.type &&
                         other.entered == next.entered;
                };
                if (next.property.empty() || std::ranges::any_of(asked, site))
                  continue;
                asked.push_back(next);
                AstNodePtr entered;
                const std::optional<ReferenceInfo> context =
                    detail::reference_at_caret(*language.astReflection,
                                               document, container, next,
                                               entered);
                if (!context) continue;
                auto member =
                    std::ranges::find_if(members, [&](const Member& m) {
                      return m.property == next.property &&
                             m.reference == context->reference;
                    });
                if (member == members.end())
                  member = members.insert(
                      members.end(), Member{.property = next.property,
                                            .reference = context->reference,
                                            .lines = {}});
                const auto index =
                    static_cast<std::size_t>(member - members.begin());
                (void)language.scopeProvider->visitScopeEntries(
                    *context, [&](const AstNodeDescription& entry) {
                      throw_if_cancelled(token);
                      if (!seen.emplace(index, entry.name, entry.type).second)
                        return true;
                      if (++matches > kMaxLines) return false;
                      std::string& lines = members[index].lines;
                      lines += entry.name;
                      if (entry.type != nullptr) {
                        lines += " (";
                        lines += entry.type->name;
                        lines += ')';
                      }
                      lines += '\n';
                      return true;
                    });
              }
              for (const Member& member : members) {
                out += "candidates for member '";
                out += member.property;
                out += "':\n";
                out += member.lines;
              }
              if (matches > kMaxLines) out += "... more results elided\n";
            });
        if (!result.isError && result.text.empty())
          result.text = "no cross-reference is expected at this position";
        return result;
      });
}

}  // namespace cursiv
