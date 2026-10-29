#include "cursiv/mcp/DefaultMcpService.hpp"

#include <lsp/json/json.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/documentation/DocumentationProvider.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/GrammarSource.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/references/References.hpp"
#include "cursiv/core/references/ScopeProvider.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/services/LanguageMetaData.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/syntax-tree/CstUtils.hpp"
#include "cursiv/core/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/core/text/Position.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/UriUtils.hpp"
#include "cursiv/core/validation/DefaultCodeActionProtocol.hpp"
#include "cursiv/core/validation/SpellingActions.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/IndexManager.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/core/workspace/WorkspaceLock.hpp"
#include "cursiv/mcp/McpServer.hpp"
#include "cursiv/mcp/McpToolKit.hpp"

namespace cursiv {

namespace {

namespace json = lsp::json;

// Answers past this count are elided: a model reads the head of a list, and an
// unbounded workspace dump would drown the lines it asked for.
constexpr std::size_t kMaxLines = 200;

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
  const std::vector<JsonValue::Array> spelled = spelling_actions(document, all);
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
    // The fixes the runtime would have offered, by title: those the
    // validator stamped and those a linking error is owed on demand -- a
    // caller with no editor still learns them.
    const char* sep = " -- fix: ";
    const auto name = [&](const JsonValue& row) {
      if (!row.isObject()) return;
      const auto title = row.object().find(std::string(kTitleField));
      if (title == row.object().end() || !title->second.isString()) return;
      out += sep;
      out += title->second.string();
      sep = "; ";
    };
    if (diagnostic.data.has_value() && diagnostic.data->isObject()) {
      const JsonValue::Object& data = diagnostic.data->object();
      const auto rows = data.find(std::string(kDefaultCodeActionsKey));
      if (rows != data.end() && rows->second.isArray())
        for (const JsonValue& row : rows->second.array()) name(row);
    }
    for (const JsonValue& row : spelled[i]) name(row);
    out += '\n';
  }
}

// The grammar of the language the call NAMES -- the id is required, so the
// contract reads the same whatever the workspace holds. A miss answers with
// the served ids inline, sparing the round-trip through 'languages'.
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

// A scratch name is the store's for the length of one call, and an id is for
// ever: the slots are numbered once and handed back, so the store holds as
// many as validations ever ran at once.
class ScratchSlots {
 public:
  [[nodiscard]] std::uint32_t acquire() {
    const std::scoped_lock lock(_mutex);
    if (_free.empty()) return _next++;
    const std::uint32_t slot = _free.back();
    _free.pop_back();
    return slot;
  }
  void release(std::uint32_t slot) {
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

struct ScratchLease {
  std::uint32_t slot = scratch_slots().acquire();
  ScratchLease() = default;
  ScratchLease(const ScratchLease&) = delete;
  ScratchLease& operator=(const ScratchLease&) = delete;
  ~ScratchLease() { scratch_slots().release(slot); }
};

struct ScratchInput {
  std::string name;
  std::string languageId;
};

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
    if (name.empty()) throw std::invalid_argument("'file' names a file");
    input.name = name;
    return input;
  }
  if (language != nullptr) {
    const LanguageMetaData& meta = language->languageMetaData;
    if (!meta.fileExtensions.empty())
      input.name = "text" + normalize_extension(meta.fileExtensions.front());
    else if (!meta.fileNames.empty())
      input.name = meta.fileNames.front();
    else
      input.name = "text";
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
        return McpToolResult{out.empty() ? "no language is registered" : out};
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
        if (shared.documentBuilder->waitUntil(DocumentState::Validated,
                                              token) != WaitOutcome::Reached)
          return McpToolResult{"the workspace never reached validation", true};
        std::string out;
        (void)shared.workspaceLock->read(
            [&] {
              const json::Value* const directory =
                  tool_argument(arguments, "directory");
              std::vector<std::shared_ptr<Document>> documents =
                  directory != nullptr && directory->isString()
                      ? shared.documents->getDocuments(
                            tool_file_uri(directory->string()))
                      : shared.documents->all();
              std::ranges::sort(documents, [](const auto& a, const auto& b) {
                return a->uri < b->uri;
              });
              for (const std::shared_ptr<Document>& document : documents) {
                if (document == nullptr || document->diagnostics.empty())
                  continue;
                out += document->uri;
                out += '\n';
                append_diagnostics(out, *document);
              }
            },
            token);
        return McpToolResult{out.empty() ? "No diagnostics found." : out};
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
           "the validations and answers the diagnostics. Nothing is saved: "
           "the text is discarded afterwards. Pass 'languageId' or a 'file' "
           "name to pick the language.",
       .inputSchema =
           tool_object_schema(std::move(properties), std::move(required))},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken& token) {
        const json::Value* const text = tool_argument(arguments, "text");
        if (text == nullptr || !text->isString())
          throw std::invalid_argument("'text' is the document to validate");
        const ScratchInput input = scratch_input(arguments, shared);
        const ScratchLease lease;
        const std::string uri = scratch_uri(lease.slot, input.name);
        const CoreServices* const language =
            shared.serviceRegistry->findServices(uri, input.languageId);
        if (language == nullptr)
          return McpToolResult{"no language answers for '" + uri + "'", true};
        const std::string_view uriView = uri;
        bool installed = false;
        // A newer write must not cancel the deletion of this call's document.
        const auto discard = [&] {
          if (!installed) return;
          shared.workspaceLock->write(
              [&](const CancellationToken& writeToken) {
                shared.documentBuilder->update({}, {&uriView, 1}, writeToken);
              },
              WorkspaceLock::WriteMode::Durable);
          installed = false;
        };
        McpToolResult result;
        try {
          // Durable: the install must not lose to a keystroke that arrived
          // behind it; past settle the build is superseded normally.
          shared.workspaceLock->write(
              [&](const CancellationToken& writeToken) {
                auto document = shared.documentFactory->fromTextDocument(
                    TextDocument::create(uri,
                                         language->languageMetaData.languageId,
                                         0, text->string()),
                    writeToken);
                shared.documents->addDocument(std::move(document));
                installed = true;
                shared.documentBuilder->update({&uriView, 1}, {}, writeToken);
              },
              WorkspaceLock::WriteMode::Durable);
          DocumentId id = kNoDocument;
          (void)shared.workspaceLock->read(
              [&] { id = shared.documents->getDocumentId(uri); }, token,
              WorkspaceLock::ReadPriority::Immediate);
          if (shared.documentBuilder->waitUntil(DocumentState::Validated, id,
                                                token) !=
              WaitOutcome::Reached) {
            result = {"the validation build did not finish", true};
          } else {
            std::string out;
            (void)shared.workspaceLock->read(
                [&] {
                  const std::shared_ptr<Document> document =
                      shared.documents->getDocument(id);
                  if (document != nullptr) append_diagnostics(out, *document);
                },
                token, WorkspaceLock::ReadPriority::Immediate);
            result = {out.empty() ? "No diagnostics: the text is valid." : out};
          }
        } catch (...) {
          discard();
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
        if (shared.documentBuilder->waitUntil(DocumentState::IndexedContent,
                                              token) != WaitOutcome::Reached)
          return McpToolResult{"the workspace never reached indexing", true};
        const json::Value* const query = tool_argument(arguments, "query");
        const std::string needle =
            query != nullptr && query->isString() ? query->string() : "";
        std::string out;
        std::size_t matches = 0;
        (void)shared.workspaceLock->read(
            [&] {
              (void)shared.indexManager->visitElements(
                  nullptr, {}, [&](const AstNodeDescription& description) {
                    throw_if_cancelled(token);
                    const auto lower = [](char c) {
                      return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32)
                                                  : c;
                    };
                    const auto matched = std::ranges::search(
                        description.name, needle,
                        [&](char a, char b) { return lower(a) == lower(b); });
                    if (matched.empty() && !needle.empty()) return true;
                    if (++matches > kMaxLines) return false;
                    out += description.name;
                    if (description.type != nullptr) {
                      out += " (";
                      out += description.type->name;
                      out += ')';
                    }
                    const std::shared_ptr<Document> home =
                        shared.documents->getDocument(description.documentId);
                    if (home != nullptr) {
                      out += "  ";
                      out += home->uri;
                      out += ':';
                      const CoreServices* const language =
                          shared.serviceRegistry->findServices(home->uri);
                      const std::optional<CstNodeView> nameNode =
                          language == nullptr || description.node == nullptr
                              ? std::nullopt
                              : language->nameProvider->getNameNode(
                                    *description.node, home.get());
                      const NodeId at = nameNode.has_value()
                                            ? nameNode->id
                                            : description.symbolId;
                      const CstNode& node = home->parseResult.cst()[at];
                      append_tool_position(out, *home, node.begin);
                    }
                    out += '\n';
                    return true;
                  });
            },
            token);
        // No count: the visitor stops the walk at the cap, so what is past it
        // was never enumerated.
        if (matches > kMaxLines)
          out += "... more results elided (narrow with 'query')\n";
        return McpToolResult{out.empty() ? "No symbols found." : out};
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
            shared, at.uri, DocumentState::IndexedReferences, token,
            [&](const Document& document, const CoreServices& language,
                std::string& out) {
              const TextOffset offset = document.textDocument().offsetAt(
                  Position{at.line, at.column});
              const Declaration declaration =
                  language.references->findDeclaration(document, offset, token);
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
              for (const AstNode* const target : declaration.targets) {
                const std::vector<ReferenceDescription> found =
                    language.references->findReferences(
                        *target, {.includeDeclaration = true}, token);
                for (const ReferenceDescription& reference : found) {
                  const std::shared_ptr<Document> home =
                      shared.documents->getDocument(reference.sourceDocumentId);
                  if (home == nullptr) continue;
                  out += home->uri;
                  out += ':';
                  append_tool_position(out, *home, reference.sourceBegin);
                  out += '\n';
                }
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
                  language.references->findDeclaration(document, offset, token);
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
                const Document* const home = target->getDocument();
                if (home != nullptr) {
                  out += "  declared at ";
                  append_location(out, *home, target);
                }
                out += '\n';
                // The prose is the declaration's language's: a global
                // candidate may come from another grammar.
                if (home == nullptr) continue;
                const CoreServices* const owner =
                    shared.serviceRegistry->findServices(home->uri);
                const DocumentationProvider* const docs =
                    owner == nullptr ? nullptr
                                     : owner->documentationProvider.get();
                if (docs == nullptr) continue;
                if (const std::optional<std::string> prose =
                        docs->getDocumentation(*target, home)) {
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
            shared, at.uri, DocumentState::IndexedReferences, token,
            [&](const Document& document, const CoreServices& language,
                std::string& out) {
              const TextOffset offset = document.textDocument().offsetAt(
                  Position{at.line, at.column});
              const Declaration declaration =
                  language.references->findDeclaration(document, offset, token);
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
              std::vector<std::pair<DocumentId, TextOffset>> seen;
              const auto edit = [&](const Document& home, TextOffset begin,
                                    TextOffset end) {
                if (std::ranges::find(seen, std::pair{home.id, begin}) !=
                    seen.end())
                  return;
                seen.emplace_back(home.id, begin);
                out += home.uri;
                out += ':';
                append_tool_position(out, home, begin);
                out += '-';
                append_tool_position(out, home, end);
                out += '\n';
              };
              // A typo that resolved to nothing still renames itself.
              if (declaration.targets.empty()) {
                edit(document, declaration.span.begin, declaration.span.end);
                return;
              }
              for (const AstNode* const target : declaration.targets)
                for (const ReferenceDescription& reference :
                     language.references->findReferences(
                         *target, {.includeDeclaration = true}, token)) {
                  const std::shared_ptr<Document> home =
                      shared.documents->getDocument(reference.sourceDocumentId);
                  if (home != nullptr)
                    edit(*home, reference.sourceBegin, reference.sourceEnd);
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
        (void)shared.workspaceLock->read(
            [&] {
              std::vector<std::shared_ptr<Document>> documents =
                  shared.documents->all();
              std::ranges::sort(documents, [](const auto& a, const auto& b) {
                return a->uri < b->uri;
              });
              for (const std::shared_ptr<Document>& document : documents) {
                if (document == nullptr) continue;
                out += document->uri;
                out += document->isOpen ? "  open" : "  on disk";
                out += "  version ";
                out += std::to_string(document->textDocument().version());
                if (const std::size_t n = document->diagnostics.size(); n != 0)
                  out += "  " + std::to_string(n) + " diagnostic(s)";
                out += '\n';
              }
            },
            token);
        return McpToolResult{out.empty() ? "the workspace holds no document"
                                         : out};
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
        (void)shared.workspaceLock->read(
            [&] {
              const std::shared_ptr<Document> document =
                  shared.documents->getDocument(key);
              if (document == nullptr) return;
              found = true;
              out = std::string(document->textDocument().getText());
            },
            token);
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
           "The outline of one document: its declarations, nested, each with "
           "its declared type and position.",
       .inputSchema =
           tool_object_schema(std::move(properties), std::move(required))},
      [&shared = _shared](const json::Value& arguments,
                          const CancellationToken& token) {
        const json::Value* const file = tool_argument(arguments, "file");
        if (file == nullptr || !file->isString())
          throw std::invalid_argument("'file' names the document");
        McpToolResult result = route_document_tool(
            shared, tool_file_uri(file->string()),
            DocumentState::ComputedScopes, token,
            [&](const Document& document, const CoreServices&,
                std::string& out) {
              const auto walk = [&](auto&& self, const AstNode* container,
                                    std::size_t depth) -> void {
                if (depth > 16) return;
                for (const AstNodeDescription& entry :
                     document.localSymbols.forContainer(container)) {
                  throw_if_cancelled(token);
                  out.append(2 * depth, ' ');
                  out += entry.name;
                  if (entry.type != nullptr) {
                    out += " (";
                    out += entry.type->name;
                    out += ')';
                  }
                  if (entry.symbolId != kNoNode) {
                    out += "  ";
                    append_tool_position(
                        out, document,
                        document.parseResult.cst()[entry.symbolId].begin);
                  }
                  out += '\n';
                  if (entry.node != nullptr && entry.node != container)
                    self(self, entry.node, depth + 1);
                }
              };
              // The root's parent is the document, so the top level hangs off
              // it; a scope computation that keys on the root node instead
              // still answers through the fallback.
              walk(walk, &document, 0);
              if (out.empty() && document.parseResult.value != nullptr)
                walk(walk, document.parseResult.value, 0);
            });
        if (!result.isError && result.text.empty())
          result.text = "this document declares no symbol";
        return result;
      });
}

namespace {

// The features the engine answers at `offset`, asked as a completion asks
// them: in place of the word the caret ends or stands in, and at the caret
// alone where a whole word answers nothing. `word` is left on what they
// replace, empty for a bare insertion.
std::vector<NextFeature> features_at(const Document& document,
                                     const Parser& parser, TextOffset offset,
                                     CstLeaf& word) {
  const Cst& cst = document.parseResult.cst();
  std::vector<NextFeature> nexts;
  word = word_at(cst, offset);
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
           "The valid targets of the cross-reference expected at a position, "
           "as the scope at that site answers them.",
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
              const AstNode* container = owner_at(cst, offset);
              if (container == nullptr) container = document.parseResult.value;
              if (container == nullptr) return;
              CstLeaf word;
              for (const NextFeature& next :
                   features_at(document, *language.parser, offset, word)) {
                throw_if_cancelled(token);
                if (next.property.empty()) continue;
                std::uint16_t reference = kNoReference;
                for (const AstNode* climb = container; climb != nullptr;
                     climb = climb->parent) {
                  reference = language.astReflection->referenceIndexOf(
                      climb->type, next.property);
                  if (reference != kNoReference) break;
                }
                if (reference == kNoReference) {
                  for (const AstTypeTag* const type :
                       language.astReflection->getAllTypes()) {
                    const std::uint16_t row =
                        language.astReflection->referenceIndexOf(type,
                                                                 next.property);
                    if (row == kNoReference) continue;
                    if (reference != kNoReference && row != reference) {
                      reference = kNoReference;
                      break;
                    }
                    reference = row;
                  }
                }
                if (reference == kNoReference) continue;

                ReferenceInfo context;
                context.container = container;
                context.document = &document;
                context.reference = reference;
                context.narrow = next.narrow;
                out += "candidates for member '";
                out += next.property;
                out += "':\n";
                std::size_t matches = 0;
                (void)language.scopeProvider->visitScopeEntries(
                    context, [&](const AstNodeDescription& entry) {
                      throw_if_cancelled(token);
                      if (++matches > kMaxLines) return false;
                      out += entry.name;
                      if (entry.type != nullptr) {
                        out += " (";
                        out += entry.type->name;
                        out += ')';
                      }
                      out += '\n';
                      return true;
                    });
                if (matches > kMaxLines) out += "... more results elided\n";
                break;
              }
            });
        if (!result.isError && result.text.empty())
          result.text = "no cross-reference is expected at this position";
        return result;
      });
}

}  // namespace cursiv
