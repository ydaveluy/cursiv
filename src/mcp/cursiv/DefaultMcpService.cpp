#include "cursiv/DefaultMcpService.hpp"

#include <lsp/json/json.h>
#include <lsp/uri.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
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

McpToolResult at_position(
    SharedCoreServices& shared, const McpToolPosition& at, ServiceRequirement requirement,
    const CancellationToken& token, std::string_view empty,
    FunctionRef<void(const Document&, const CoreServices&, TextOffset, std::string&)> render) {
  return routeDocumentTool(shared, at.uri, requirement, token,
      [&](const Document& document, const CoreServices& language, std::string& out) {
        render(document, language, document.textDocument().offsetAt({at.line, at.column}), out);
        if (out.empty()) out = empty;
      });
}

void append_location(std::string& out, const Document& document,
                     const AstNode* node) {
  out += document.uri;
  const NodeId record = node->cstNodeId;
  if (record == kNoNode) return;
  out += ':';
  appendToolPosition(out, document, document.parseResult.cst()[record].begin);
}

// `spelt` counts the sites the answer has spelt, over every document it lists.
void append_diagnostics(std::string& out, const Document& document,
                        unsigned& spelt, const CancellationToken& token) {
  std::vector<const Diagnostic*> all;
  all.reserve(document.diagnostics.size());
  for (const Diagnostic& diagnostic : document.diagnostics)
    all.push_back(&diagnostic);
  const std::vector<JsonValue::Array> spelled =
      detail::spelling_actions(document, all, spelt, token);
  for (std::size_t i = 0; i < all.size(); ++i) {
    throwIfCancelled(token);
    const Diagnostic& diagnostic = *all[i];
    out += '[';
    out += kSeverities[static_cast<unsigned>(diagnostic.severity) - 1];
    out += " at ";
    appendToolPosition(out, document, diagnostic.range.begin);
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

constexpr std::string_view kCancelled = "the request was cancelled";

const std::string* optional_string(const json::Value& arguments,
                                   std::string_view key) {
  const json::Value* const value = toolArgument(arguments, key);
  if (value == nullptr) return nullptr;
  if (!value->isString())
    throw std::invalid_argument("'" + std::string(key) + "' must be a string");
  return &value->string();
}

// Runs `body` once, under a queued read of the workspace at the given state.
// Otherwise answers an error, `never` when that state is not coming.
std::optional<McpToolResult> read_workspace_at(
    const SharedCoreServices& shared, DocumentState target,
    std::string_view never, const CancellationToken& token,
    cursiv::FunctionRef<void()> body) {
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

std::string grammar_uri(std::string_view id) {
  // Dot segments disappear when a client resolves a hierarchical URI.
  const char* base =
      id == "." || id == ".." ? "cursiv://grammar/?id=" : "cursiv://grammar/";
  return base + lsp::Uri::encode(id, "~!$&'()*+,;=:@");
}

const GrammarSource* pick_grammar(const SharedCoreServices& shared,
                                  const json::Value& arguments,
                                  std::string& error) {
  const json::Value* const languageId = toolArgument(arguments, "languageId");
  if (languageId == nullptr || !languageId->isString())
    throw std::invalid_argument(
        "'languageId' names the grammar; the 'languages' tool lists them");
  if (const auto* language =
          shared.serviceRegistry->findServices({}, languageId->string());
      language && language->grammarSource)
    return language->grammarSource.get();
  std::string served;
  for (const CoreServices* const language : shared.serviceRegistry->all()) {
    if (language->grammarSource == nullptr) continue;
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
  const CoreServices* language = nullptr;
};

// Whether `name` stays one file in its slot once the uri is normalised: a dot
// segment would leave the slot.
bool one_file(std::string_view name) {
  return !name.empty() && name != "." && name != ".." &&
         name.find_first_of("/\\") == std::string_view::npos;
}

ScratchInput scratch_input(const json::Value& arguments,
                           const SharedCoreServices& shared) {
  ScratchInput input;
  if (const auto* languageId = optional_string(arguments, "languageId")) {
    input.language = shared.serviceRegistry->findServices({}, *languageId);
    if (input.language == nullptr)
      throw std::invalid_argument("no language is registered as '" +
                                  *languageId + "'");
  }
  if (const auto* file = optional_string(arguments, "file")) {
    std::string_view name = *file;
    if (const auto slash = name.find_last_of("/\\");
        slash != std::string_view::npos)
      name.remove_prefix(slash + 1);
    if (!one_file(name)) throw std::invalid_argument("'file' names a file");
    input.name = name;
    return input;
  }
  if (input.language != nullptr) {
    if (const LanguageMetaData& meta = input.language->languageMetaData;
        !meta.fileExtensions.empty())
      input.name = "text" + normalizeExtension(meta.fileExtensions.front());
    else if (!meta.fileNames.empty())
      input.name = meta.fileNames.front();
    if (!one_file(input.name)) input.name = "text";
    return input;
  }
  throw std::invalid_argument("'languageId' or 'file' picks the language");
}

std::string scratch_uri(std::uint32_t slot, std::string_view name) {
  return pathToFileUri("/.mcp-scratch/" + std::to_string(slot) + "/" +
                          std::string(name));
}

}  // namespace

DefaultMcpService::DefaultMcpService(SharedCoreServices& shared,
                                     McpServiceOptions options)
    : sharedServices(shared), serverOptions(std::move(options)) {}

std::unique_ptr<McpServer> DefaultMcpService::createServer() const {
  auto server = std::make_unique<McpServer>(serverOptions.name, serverOptions.version);
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
      [&shared = sharedServices](const json::Value&, const CancellationToken&) {
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
                     ? "  grammar: " + grammar_uri(meta.languageId)
                     : "  (no grammar text served)";
          out += '\n';
        }
        return McpToolResult{out.empty() ? "no language is registered"
                                         : std::move(out)};
      });
}

void DefaultMcpService::registerGrammarTool(McpServer& server) const {
  json::Object properties;
  properties["languageId"] = toolProperty(
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
           toolObjectSchema(std::move(properties), std::move(required))},
      [&shared = sharedServices](const json::Value& arguments,
                          const CancellationToken&) {
        std::string error;
        const GrammarSource* const grammar =
            pick_grammar(shared, arguments, error);
        if (grammar == nullptr) return McpToolResult{std::move(error), true};
        return McpToolResult{std::string(grammar->grammarText())};
      });
}

void DefaultMcpService::registerGrammarResources(McpServer& server) const {
  for (const CoreServices* const language : sharedServices.serviceRegistry->all()) {
    const GrammarSource* const grammar = language->grammarSource.get();
    if (grammar == nullptr) continue;
    const std::string& id = language->languageMetaData.languageId;
    server.registerResource(
        {.uri = grammar_uri(id),
         .name = id + " grammar",
         .description = "the " + id + " grammar, as reviewable text",
         .mimeType = "text/plain"},
        [grammar] { return std::string(grammar->grammarText()); });
  }
}

void DefaultMcpService::registerRuleTool(McpServer& server) const {
  json::Object properties;
  properties["name"] = toolProperty("string", "the rule's name");
  properties["languageId"] = toolProperty(
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
           toolObjectSchema(std::move(properties), std::move(required))},
      [&shared = sharedServices](const json::Value& arguments,
                          const CancellationToken&) {
        const json::Value* const name = toolArgument(arguments, "name");
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
  properties["directory"] = toolProperty(
      "string",
      "restrict to this directory (omit or leave empty for all files)");
  server.registerTool(
      {.name = "get-diagnostics",
       .title = "Get Diagnostics",
       .description =
           "Retrieves diagnostics from the language server for all files in "
           "the given directory, ordered by file.",
       .inputSchema = toolObjectSchema(std::move(properties), {})},
      [&shared = sharedServices](const json::Value& arguments,
                          const CancellationToken& token) {
        const auto* directory = optional_string(arguments, "directory");
        std::string out;
        if (std::optional<McpToolResult> unread = read_workspace_at(
                shared, DocumentState::Validated,
                "the workspace never reached validation", token, [&] {
                  auto documents = directory && !directory->empty()
                                       ? shared.documents->getDocuments(
                                             toolFileUri(*directory))
                                       : shared.documents->all();
                  std::erase_if(documents, [&](const auto& document) {
                    throwIfCancelled(token);
                    return !document || document->diagnostics.empty();
                  });
                  std::ranges::sort(documents, {}, &Document::uri);
                  unsigned spelt = 0;
                  for (const std::shared_ptr<Document>& document : documents) {
                    out += document->uri;
                    out += '\n';
                    append_diagnostics(out, *document, spelt, token);
                  }
                }))
          return std::move(*unread);
        return McpToolResult{out.empty() ? "No diagnostics found."
                                         : std::move(out)};
      });
}

void DefaultMcpService::registerValidateTool(McpServer& server) const {
  json::Object properties;
  properties["text"] = toolProperty("string", "the document text to validate");
  properties["languageId"] =
      toolProperty("string", "language of the text (or pass 'file')");
  properties["file"] =
      toolProperty("string", "a file name whose extension picks the language");
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
           toolObjectSchema(std::move(properties), std::move(required))},
      [&shared = sharedServices](const json::Value& arguments,
                          const CancellationToken& token) {
        const json::Value* const text = toolArgument(arguments, "text");
        if (text == nullptr || !text->isString())
          throw std::invalid_argument("'text' is the document to validate");
        const ScratchInput input = scratch_input(arguments, shared);
        ScratchLease lease;
        const std::string uri = scratch_uri(lease.slot(), input.name);
        const CoreServices* const language =
            input.language ? input.language
                           : shared.serviceRegistry->findServices(uri);
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
        std::optional<McpToolResult> result;
        try {
          // Each attempt waits for the writes already queued, then runs one
          // build: what a superseded build left, or else the text with every
          // category, read in that write. The call retries until one answers.
          while (!result) {
            if (token.stop_requested() ||
                !shared.workspaceLock->read([] {}, token)) {
              result = McpToolResult{std::string(kCancelled), true};
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
                    result = McpToolResult{"the text to validate was removed",
                                           true};
                    return;
                  }
                  Document* const built = document.get();
                  builder.build({&built, 1}, {}, either);
                  const bool failed = document->buildFailed;
                  if (!failed &&
                      document->state.load() < DocumentState::Validated) {
                    result = McpToolResult{
                        "the validation build did not finish", true};
                    return;
                  }
                  std::string out;
                  unsigned spelt = 0;
                  append_diagnostics(out, *document, spelt, either);
                  result = McpToolResult{
                      out.empty() ? "No diagnostics: the text is valid."
                                  : std::move(out),
                      failed};
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
        return std::move(*result);
      });
}

void DefaultMcpService::registerSymbolsTool(McpServer& server) const {
  json::Object properties;
  properties["query"] =
      toolProperty("string",
                    "case-insensitive substring of the name (optional; empty "
                    "lists every exported symbol)");
  server.registerTool(
      {.name = "symbols",
       .title = "Workspace Symbols",
       .description = "Lists the symbols every document exports, with their "
                      "declared type and location.",
       .inputSchema = toolObjectSchema(std::move(properties), {})},
      [&shared = sharedServices](const json::Value& arguments,
                          const CancellationToken& token) {
        const auto* query = optional_string(arguments, "query");
        // Both sides lowered by the Unicode mapping, as a client's
        // `toLowerCase` lowers them: `école` finds `École`.
        const std::string needle = query ? detail::lower_case(*query) : "";
        std::string out;
        std::size_t matches = 0;
        if (std::optional<McpToolResult> unread = read_workspace_at(
                shared, DocumentState::IndexedContent,
                "the workspace never reached indexing", token, [&] {
                  (void)shared.indexManager->visitElements(
                      nullptr, {}, [&](const AstNodeDescription& description) {
                        throwIfCancelled(token);
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
                            appendToolPosition(out, *owningDocument,
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
       .inputSchema = toolPositionSchema()},
      [&shared = sharedServices](const json::Value& arguments,
                          const CancellationToken& token) {
        const McpToolPosition at = toolPosition(arguments);
        return at_position(shared, at, WorkspaceState::kIndexedReferences, token,
            "nothing is denoted at this position",
            [&](const Document& document, const CoreServices& language, TextOffset offset, std::string& out) {
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
              for (const ReferenceDescription& reference :
                   language.references->findReferences(
                       declaration.targets, {.includeDeclaration = true},
                       token)) {
                throwIfCancelled(token);
                const std::shared_ptr<Document> owningDocument =
                    shared.documents->getDocument(reference.sourceDocumentId);
                if (owningDocument == nullptr) continue;
                out += owningDocument->uri;
                out += ':';
                appendToolPosition(out, *owningDocument,
                                     reference.sourceBegin);
                out += '\n';
              }
            });
      });
}

void DefaultMcpService::registerHoverTool(McpServer& server) const {
  server.registerTool(
      {.name = "hover",
       .title = "Hover",
       .description =
           "What the name at a position denotes: its declared type, where it "
           "is declared, and the documentation its author wrote.",
       .inputSchema = toolPositionSchema()},
      [&shared = sharedServices](const json::Value& arguments,
                          const CancellationToken& token) {
        const McpToolPosition at = toolPosition(arguments);
        return at_position(shared, at, DocumentState::Linked, token,
            "nothing is denoted at this position",
            [&](const Document& document, const CoreServices& language, TextOffset offset, std::string& out) {
              const Declaration declaration =
                  language.references->findDeclarations(document, offset,
                                                        token);
              if (!declaration) return;
              const std::string_view name =
                  document.textDocument().getText().substr(
                      declaration.span.begin,
                      declaration.span.end - declaration.span.begin);
              for (const AstNode* const target : declaration.targets) {
                throwIfCancelled(token);
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
      });
}

void DefaultMcpService::registerRenameTool(McpServer& server) const {
  json::Value schema = toolPositionSchema();
  schema.object().get("properties").object()["newName"] =
      toolProperty("string", "the replacement name");
  schema.object().get("required").array().emplace_back("newName");
  server.registerTool(
      {.name = "rename",
       .title = "Rename",
       .description =
           "Every span to edit when renaming the name at a position, "
           "declaration included. Nothing is applied, and the language's own "
           "naming refusals are not checked: apply the edits yourself and "
           "validate.",
       .inputSchema = std::move(schema)},
      [&shared = sharedServices](const json::Value& arguments,
                          const CancellationToken& token) {
        const McpToolPosition at = toolPosition(arguments);
        const json::Value* const newName = toolArgument(arguments, "newName");
        if (newName == nullptr || !newName->isString())
          throw std::invalid_argument("'newName' is the replacement");
        return at_position(shared, at, WorkspaceState::kIndexedReferences, token,
            "nothing renamable at this position",
            [&](const Document& document, const CoreServices& language, TextOffset offset, std::string& out) {
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
                appendToolPosition(out, owningDocument, begin);
                out += '-';
                appendToolPosition(out, owningDocument, end);
                out += '\n';
              };
              // A typo that resolved to nothing still renames itself.
              if (declaration.targets.empty()) {
                edit(document, declaration.span.begin, declaration.span.end);
                return;
              }
              for (const ReferenceDescription& reference :
                   language.references->findReferences(
                       declaration.targets, {.includeDeclaration = true},
                       token)) {
                throwIfCancelled(token);
                const std::shared_ptr<Document> owningDocument =
                    shared.documents->getDocument(reference.sourceDocumentId);
                if (owningDocument != nullptr)
                  edit(*owningDocument, reference.sourceBegin,
                       reference.sourceEnd);
              }
            });
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
      [&shared = sharedServices](const json::Value&, const CancellationToken& token) {
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
      toolProperty("string", "path or file:// uri of the document");
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
           toolObjectSchema(std::move(properties), std::move(required))},
      [&shared = sharedServices](const json::Value& arguments,
                          const CancellationToken& token) {
        const json::Value* const file = toolArgument(arguments, "file");
        if (file == nullptr || !file->isString())
          throw std::invalid_argument("'file' names the document");
        const std::string key = toolFileUri(file->string());
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
      toolProperty("string", "path or file:// uri of the document");
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
           toolObjectSchema(std::move(properties), std::move(required))},
      [&shared = sharedServices](const json::Value& arguments,
                          const CancellationToken& token) {
        const json::Value* const file = toolArgument(arguments, "file");
        if (file == nullptr || !file->isString())
          throw std::invalid_argument("'file' names the document");
        McpToolResult result = routeDocumentTool(
            shared, toolFileUri(file->string()), DocumentState::Parsed, token,
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
                throwIfCancelled(token);
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
                appendToolPosition(out, document, cst[node.cstNodeId].begin);
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
                                     CstLeaf& word,
                                     const CancellationToken& token) {
  const Cst& cst = document.parseResult.cst();
  std::vector<NextFeature> nexts;
  word = detail::word_at(cst, offset);
  if (word) {
    parser.nextFeaturesAt(cst, word.begin, word.end, nexts, token);
    if (!nexts.empty() || offset != word.end || offset == word.begin)
      return nexts;
    word = {};
  }
  parser.nextFeaturesAt(cst, offset, offset, nexts, token);
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
       .inputSchema = toolPositionSchema()},
      [&shared = sharedServices](const json::Value& arguments,
                          const CancellationToken& token) {
        const McpToolPosition at = toolPosition(arguments);
        return at_position(shared, at, DocumentState::Parsed, token,
            "nothing may be written at this position",
            [&](const Document& document, const CoreServices& language, TextOffset offset, std::string& out) {
              const Grammar& grammar = language.parser->getGrammar();
              std::unordered_set<std::string> seen;
              CstLeaf word;
              for (const NextFeature& next : features_at(
                       document, *language.parser, offset, word, token)) {
                throwIfCancelled(token);
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
                const auto append = [&](std::string line) {
                  throwIfCancelled(token);
                  line += tail;
                  const auto [row, fresh] = seen.insert(std::move(line));
                  if (!fresh) return;
                  if (word && out.empty()) {
                    out += "in place of '";
                    out += document.parseResult.cst().text().substr(
                        word.begin, word.end - word.begin);
                    out += "'\n";
                  }
                  out += *row;
                  out += '\n';
                };
                const auto spellings = grammar.terminalSpellings(next.feature);
                if (!spellings.empty()) {
                  for (std::string_view spelled : spellings)
                    append('\'' + std::string(spelled) + '\'');
                } else if (const auto name = grammar.terminalName(next.feature);
                           !name.empty()) {
                  append(std::string(name) + " (token)");
                }
              }
            });
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
       .inputSchema = toolPositionSchema()},
      [&shared = sharedServices](const json::Value& arguments,
                          const CancellationToken& token) {
        const McpToolPosition at = toolPosition(arguments);
        return at_position(shared, at, DocumentState::Linked, token,
            "no cross-reference is expected at this position",
            [&](const Document& document, const CoreServices& language, TextOffset offset, std::string& out) {
              const Cst& cst = document.parseResult.cst();
              CstLeaf word;
              const std::vector<NextFeature> features =
                  features_at(document, *language.parser, offset, word, token);
              // From the word's own node when the caret is in a word, as
              // completion reads; else from the node before the caret.
              AstNode* const container =
                  word ? ownerOfLeaf(cst, word.id) : ownerAt(cst, offset);
              // One list per reference member, in the order the grammar names
              // them: every site the position admits is read, and the sites
              // of one member add up their offers, as a completion's do.
              std::map<std::pair<std::string_view, std::uint16_t>, std::size_t>
                  members;
              std::vector<std::string> groups;
              using Seen =
                  std::tuple<std::size_t, std::string_view, const AstTypeTag*>;
              std::set<Seen> seen;
              std::set<std::tuple<std::string_view, std::uint8_t,
                                  const AstTypeTag*, bool>>
                  asked;
              for (const NextFeature& next : features) {
                throwIfCancelled(token);
                if (seen.size() > kMaxLines) break;
                // Two terminals filling one site ask the same scope.
                const auto site = std::tie(next.property, next.narrow,
                                           next.type, next.entered);
                if (next.property.empty() || !asked.insert(site).second)
                  continue;
                AstNodePtr entered;
                const std::optional<ReferenceInfo> context =
                    detail::reference_at_caret(*language.astReflection,
                                               document, container, next,
                                               entered);
                if (!context) continue;
                const auto [member, fresh] = members.try_emplace(
                    std::pair{next.property, context->reference},
                    groups.size());
                if (fresh)
                  groups.push_back("candidates for member '" +
                                   std::string(next.property) + "':\n");
                const auto index = member->second;
                (void)language.scopeProvider->visitScopeEntries(
                    *context, [&](const AstNodeDescription& entry) {
                      throwIfCancelled(token);
                      if (!seen.emplace(index, entry.name, entry.type).second)
                        return true;
                      if (seen.size() > kMaxLines) return false;
                      std::string& lines = groups[index];
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
              for (const auto& lines : groups) out += lines;
              if (seen.size() > kMaxLines) out += "... more results elided\n";
            });
      });
}

}  // namespace cursiv
