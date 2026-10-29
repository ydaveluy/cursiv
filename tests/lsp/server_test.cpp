// The server storey: the containers and the handshake, the wire's lifecycle,
// the binary's main, the command table, the hot doors, the frontier of the
// protocol's positions, the configuration pull and a build across languages.

#include <lsp/connection.h>
#include <lsp/io/stream.h>
#include <lsp/json/json.h>
#include <lsp/jsonrpc/jsonrpc.h>
#include <lsp/types.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/core/text/Position.hpp"
#include "cursiv/core/text/Range.hpp"
#include "cursiv/core/text/TextRange.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "cursiv/core/validation/ValidationCategory.hpp"
#include "cursiv/core/validation/ValidationRegistry.hpp"
#include "cursiv/core/workspace/DefaultDocumentFactory.hpp"
#include "cursiv/core/workspace/DefaultWorkspaceLock.hpp"
#include "cursiv/core/workspace/DefaultWorkspaceManager.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/LocalFileSystemProvider.hpp"
#include "cursiv/core/workspace/MemoryFileSystemProvider.hpp"
#include "cursiv/core/workspace/WorkspaceFolder.hpp"
#include "cursiv/core/workspace/WorkspaceLock.hpp"
#include "cursiv/core/workspace/WorkspaceManager.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/lsp/completion/CompletionProvider.hpp"
#include "cursiv/lsp/completion/CompletionProviderOptions.hpp"
#include "cursiv/lsp/content/DefaultTextDocumentContentProvider.hpp"
#include "cursiv/lsp/formatting/Formatter.hpp"
#include "cursiv/lsp/runtime/DefaultLanguageServer.hpp"
#include "cursiv/lsp/runtime/LanguageClient.hpp"
#include "cursiv/lsp/runtime/LanguageServer.hpp"
#include "cursiv/lsp/runtime/LanguageServerMain.hpp"
#include "cursiv/lsp/runtime/LanguageServerRuntime.hpp"
#include "cursiv/lsp/semantic/SemanticTokenProvider.hpp"
#include "cursiv/lsp/services/AbstractExecuteCommandHandler.hpp"
#include "cursiv/lsp/services/ExecuteCommandHandler.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "cursiv/lsp/workspace/ConfigurationProvider.hpp"
#include "cursiv/lsp/workspace/DefaultConfigurationProvider.hpp"
#include "cursiv/lsp/workspace/FileOperationHandler.hpp"
#include "cursiv/lsp/workspace/LspDocumentBuilder.hpp"
#include "cursiv/test/Check.hpp"
#include "doc/generated/DocG/Module.hpp"
#include "doc/generated/DocG/Parser.hpp"
#include "doc/generated/DocG/Table.hpp"
#include "lsp/types.h"
#include "lsp/uri.h"
#include "support/Editor.hpp"
#include "support/WorkspaceWorld.hpp"

// The LSP storey with no wire under it: the containers installed, the
// handshake's promises, the notifications that publish, and the routes that
// wait for a rung before they read. What the server hands a client goes to a
// stand-in that keeps it, which is the whole of what replaces the transport.
namespace language_server {

namespace {

constexpr std::string_view kUri = "file:///w/a.ws";
// A reference to a type nobody declares: the one document that is broken
// without being unparsable, so the verdict comes from validation and not from
// the parser's own recovery.
constexpr std::string_view kBroken = "entity A { x : Missing }\n";
constexpr std::string_view kRepaired =
    "datatype Missing\nentity A { x : Missing }\n";

// Keeps the mode of every write, and serves it as the default lock does.
struct RecordingLock final : cursiv::DefaultWorkspaceLock {
  std::vector<WriteMode> modes;
  void write(WriteAction action, WriteMode mode) override {
    modes.push_back(mode);
    cursiv::DefaultWorkspaceLock::write(action, mode);
  }
};

// Keeps what it is handed, and nothing else: the transport's whole role here
// is to be the place a publication can be read back from.
struct RecordingClient final : cursiv::LanguageClient {
  std::vector<lsp::PublishDiagnosticsParams> published;
  std::vector<lsp::RegistrationParams> registered;
  std::function<void(std::vector<lsp::LSPAny>)> configurationAnswer;

  void publishDiagnostics(lsp::PublishDiagnosticsParams params) override {
    published.push_back(std::move(params));
  }
  void registerCapability(lsp::RegistrationParams params) override {
    registered.push_back(std::move(params));
  }
  void configuration(lsp::ConfigurationParams,
                     std::function<void(std::vector<lsp::LSPAny>)> then) override {
    configurationAnswer = std::move(then);
  }
};

// A language that claims two trigger characters and one commit character. The
// answer itself is empty: what is under test is the handshake, not the list.
struct TriggeringCompletion final : cursiv::CompletionProvider {
  [[nodiscard]] cursiv::CompletionProviderOptions completionOptions()
      const override {
    return {.triggerCharacters = {".", ":"}, .allCommitCharacters = {";"}};
  }

  [[nodiscard]] lsp::CompletionList getCompletion(
      const cursiv::Document&, lsp::Position,
      const cursiv::CancellationToken&) const override {
    return {};
  }
};

struct EchoCommands final : cursiv::AbstractExecuteCommandHandler {
 protected:
  void registerCommands(
      const cursiv::ExecuteCommandAcceptor& acceptor) const override {
    acceptor(
        "cursiv.echo",
        [](const lsp::LSPArray& arguments,
           const cursiv::CancellationToken&) -> std::optional<lsp::LSPAny> {
          if (arguments.empty()) return std::nullopt;
          return arguments.front();
        });
  }
};

// Two arms announced and four left out, which is the whole point of the
// options: the rename pair is the one this world answers. `didRenameFiles`
// keeps what it was handed, `willRenameFiles` answers an edit naming the new
// URI, so both directions are readable from outside.
struct RecordingFiles final : cursiv::FileOperationHandler {
  std::vector<lsp::FileRename> renamed;
  lsp::FileOperationOptions options;

  RecordingFiles() {
    lsp::FileOperationFilter filter;
    filter.pattern.glob = "**/*.ws";
    filter.scheme = "file";
    lsp::FileOperationRegistrationOptions registration;
    registration.filters.push_back(std::move(filter));
    options.didRename = registration;
    options.willRename = std::move(registration);
  }

  [[nodiscard]] const lsp::FileOperationOptions& fileOperationOptions()
      const noexcept override {
    return options;
  }

  void didRenameFiles(const lsp::RenameFilesParams& params) override {
    renamed = params.files;
  }

  std::optional<lsp::WorkspaceEdit> willRenameFiles(
      const lsp::RenameFilesParams& params) const override {
    if (params.files.empty()) return std::nullopt;
    lsp::WorkspaceEdit edit;
    lsp::Map<lsp::DocumentUri, lsp::Array<lsp::TextEdit>> changes;
    changes[params.files.front().newUri].push_back(
        lsp::TextEdit{.range = {}, .newText = std::string("renamed")});
    edit.changes = std::move(changes);
    return edit;
  }
};

// The four storeys, installed as a host would: shared core, shared LSP, the
// language's core, the language's LSP. The workspace fixture because its
// members hold a cross reference -- a linking verdict and a find-references
// answer both need one.
struct World {
  cursiv::SharedLspServices shared;
  cursiv::LspServices* ws = nullptr;
  RecordingClient* client = nullptr;
  cursiv::MemoryFileSystemProvider* disk = nullptr;
  RecordingLock* lock = nullptr;

  // `disk`: the files the scan finds, and what a closed tab falls back to.
  explicit World(bool withDisk = false) {
    if (withDisk) {
      auto memory = std::make_unique<cursiv::MemoryFileSystemProvider>();
      disk = memory.get();
      shared.fileSystemProvider = std::move(memory);
    }
    auto recordingLock = std::make_unique<RecordingLock>();
    lock = recordingLock.get();
    shared.workspaceLock = std::move(recordingLock);
    cursiv::install_default_shared_lsp_services(shared);
    ws = &support::add_workspace<cursiv::LspServices>(shared);
    auto recording = std::make_unique<RecordingClient>();
    client = recording.get();
    shared.languageClient = std::move(recording);
  }

  [[nodiscard]] cursiv::LanguageServer& server() const {
    return *shared.languageServer;
  }

  // The handshake and its acknowledgement: the first subscribes the server
  // to the builder, the second scans the folders the params carried.
  lsp::InitializeResult handshake(
      const lsp::InitializeParams& params = {}) const {
    lsp::InitializeResult result = server().initialize(params);
    server().initialized();
    return result;
  }

  void open(std::string_view text) const {
    support::open(server(), kUri, "workspace", text);
  }

  void change(int version, std::string_view text) const {
    support::change(server(), kUri, version, text);
  }

  void close(std::string_view uri = kUri) const {
    lsp::DidCloseTextDocumentParams params;
    params.textDocument.uri = lsp::Uri::parse(std::string(uri));
    server().didClose(params);
  }

  [[nodiscard]] lsp::InitializeParams folderParams() const {
    lsp::InitializeParams params;
    lsp::Array<lsp::WorkspaceFolder> folders;
    folders.push_back(
        lsp::WorkspaceFolder{.uri = lsp::Uri::parse("file:///w"), .name = "w"});
    params.workspaceFolders = std::move(folders);
    return params;
  }
};

// The last publication about `uri`, if any.
const lsp::PublishDiagnosticsParams* lastSnapshotOf(const RecordingClient& of,
                                                    std::string_view uri) {
  for (auto it = of.published.rbegin(); it != of.published.rend(); ++it)
    if (it->uri.toString() == uri) return &*it;
  return nullptr;
}

// The last thing the client was told about the one URI of the world.
const lsp::PublishDiagnosticsParams* lastSnapshot(const RecordingClient& of) {
  return of.published.empty() ? nullptr : &of.published.back();
}

// A host that installs the four storeys owes nothing more: every required slot
// of every container answers.
void the_installed_defaults_leave_no_slot_owed() {
  const World world;
  CHECK(world.shared.firstMissingService().empty());
  CHECK(world.ws->firstMissingService().empty());
}

// The handshake promises what the containers can keep, and stays silent about
// the rest -- an absent capability being the protocol's own "unsupported".
void the_handshake_promises_only_what_is_installed() {
  const World world;
  const lsp::InitializeResult result = world.handshake();

  CHECK(result.capabilities.documentSymbolProvider.has_value());
  CHECK(result.capabilities.definitionProvider.has_value());
  CHECK(result.capabilities.referencesProvider.has_value());
  CHECK(result.capabilities.workspaceSymbolProvider.has_value());
  CHECK(result.capabilities.renameProvider.has_value());
  // Announced, and silent about its triggers: the default claims no character,
  // so only an explicit invocation opens the list.
  CHECK(result.capabilities.completionProvider.has_value());
  CHECK(result.capabilities.completionProvider.has_value() &&
        !result.capabilities.completionProvider->triggerCharacters.has_value());

  // The optional slots the defaults leave empty: no language installed one, so
  // no route is promised.
  CHECK(!result.capabilities.declarationProvider.has_value());
  CHECK(!result.capabilities.implementationProvider.has_value());
  CHECK(!result.capabilities.typeDefinitionProvider.has_value());
  CHECK(!result.capabilities.callHierarchyProvider.has_value());
  CHECK(!result.capabilities.typeHierarchyProvider.has_value());
  CHECK(!result.capabilities.signatureHelpProvider.has_value());
  CHECK(!result.capabilities.inlayHintProvider.has_value());
  CHECK(!result.capabilities.documentLinkProvider.has_value());
  CHECK(!result.capabilities.codeLensProvider.has_value());
  CHECK(!result.capabilities.executeCommandProvider.has_value());
  CHECK(!result.capabilities.documentFormattingProvider.has_value());
  // The workspace arm exists for the folders, and says nothing about files.
  CHECK(result.capabilities.workspace.has_value());
  CHECK(!result.capabilities.workspace->fileOperations.has_value());
}

// What a provider claims reaches the client verbatim: a character absent from
// the handshake is one the client never reopens the list on.
void the_completion_triggers_are_the_providers_own() {
  World world;
  world.ws->completionProvider = std::make_unique<TriggeringCompletion>();

  const lsp::InitializeResult result = world.handshake();
  CHECK(result.capabilities.completionProvider.has_value());
  const lsp::CompletionOptions& announced =
      *result.capabilities.completionProvider;
  CHECK(announced.triggerCharacters.has_value() &&
        announced.triggerCharacters->size() == 2);
  CHECK(announced.allCommitCharacters.has_value() &&
        announced.allCommitCharacters->size() == 1 &&
        announced.allCommitCharacters->front() == ";");
  // Never promised: no provider of this storey answers a resolve request.
  CHECK(!announced.resolveProvider.has_value());
}

// The provider builds the edit the client declared it applies: both ranges
// where it chooses between inserting and replacing, the insert range alone
// otherwise. `x : Mi|ssing` offers `Missing` over the whole word.
void completion_edits_follow_insert_replace_support() {
  for (const std::optional<bool> support :
       {std::optional<bool>{}, std::optional<bool>{false},
        std::optional<bool>{true}}) {
    World world;
    lsp::InitializeParams params;
    if (support.has_value())
      params.capabilities.textDocument.emplace()
          .completion.emplace()
          .completionItem.emplace()
          .insertReplaceSupport = *support;
    (void)world.handshake(params);
    world.open(kRepaired);
    lsp::CompletionParams query;
    query.textDocument.uri = lsp::Uri::parse(kUri);
    query.position = {1, 17};
    const lsp::CompletionList list = world.server().completion(query);
    const auto at = std::ranges::find(list.items, std::string("Missing"),
                                      &lsp::CompletionItem::label);
    CHECK(at != list.items.end() && at->textEdit.has_value());
    if (at == list.items.end() || !at->textEdit.has_value()) continue;
    if (support.value_or(false)) {
      const auto* edit = std::get_if<lsp::InsertReplaceEdit>(&*at->textEdit);
      CHECK(edit != nullptr && edit->insert.start.character == 15 &&
            edit->insert.end.character == 17 &&
            edit->replace.end.character == 22);
    } else {
      const auto* edit = std::get_if<lsp::TextEdit>(&*at->textEdit);
      CHECK(edit != nullptr && edit->range.start.character == 15 &&
            edit->range.end.character == 17 && edit->newText == "Missing");
    }
  }
}

// Hover and completion prose go in the format the client prefers, the first
// one it lists: markdown only when that is markdown, plain text otherwise,
// and when it lists nothing. Each feature reads its own list. The fixture
// grammar documents its keywords.
void prose_follows_the_clients_preferred_format() {
  using Formats = std::optional<lsp::Array<lsp::MarkupKindEnum>>;
  using enum lsp::MarkupKind;
  const lsp::MarkupKindEnum markdown(Markdown);
  const lsp::MarkupKindEnum plain(PlainText);
  const Formats none;
  const Formats plainOnly{{plain}};
  const Formats plainFirst{{plain, markdown}};
  const Formats markdownFirst{{markdown, plain}};
  const Formats markdownOnly{{markdown}};
  struct Case {
    Formats hover;
    Formats completion;
    lsp::MarkupKind hoverKind;
    lsp::MarkupKind completionKind;
  };
  const Case cases[] = {{none, none, PlainText, PlainText},
                        {plainOnly, plainOnly, PlainText, PlainText},
                        {plainFirst, plainFirst, PlainText, PlainText},
                        {markdownFirst, markdownFirst, Markdown, Markdown},
                        {markdownOnly, none, Markdown, PlainText},
                        {none, markdownOnly, PlainText, Markdown}};
  for (const Case& each : cases) {
    World world;
    lsp::InitializeParams params;
    auto& text = params.capabilities.textDocument.emplace();
    text.hover.emplace().contentFormat = each.hover;
    text.completion.emplace().completionItem.emplace().documentationFormat =
        each.completion;
    (void)world.handshake(params);
    world.open("");

    lsp::CompletionParams query;
    query.textDocument.uri = lsp::Uri::parse(kUri);
    query.position = {0, 0};
    const lsp::CompletionList list = world.server().completion(query);
    std::size_t documented = 0;
    for (const lsp::CompletionItem& item : list.items) {
      if (!item.documentation.has_value()) continue;
      const auto* doc = std::get_if<lsp::MarkupContent>(&*item.documentation);
      CHECK(doc != nullptr && doc->kind == each.completionKind &&
            !doc->value.empty());
      ++documented;
    }
    CHECK(documented != 0);

    world.change(2, "entity E {}");
    lsp::HoverParams hoverQuery;
    hoverQuery.textDocument.uri = lsp::Uri::parse(kUri);
    hoverQuery.position = {0, 1};
    const std::optional<lsp::Hover> hover = world.server().hover(hoverQuery);
    CHECK(hover.has_value());
    if (!hover.has_value()) continue;
    const auto* content = std::get_if<lsp::MarkupContent>(&hover->contents);
    CHECK(content != nullptr && content->kind == each.hoverKind &&
          content->value.starts_with("Declares an entity"));
  }
}

void virtual_content_waits_for_the_workspace_scan(bool withFolders) {
  struct DeferredWorkspace final : cursiv::DefaultWorkspaceManager {
    std::shared_future<void> loaded;
    DeferredWorkspace(cursiv::SharedCoreServices& shared,
                      std::shared_future<void> ready)
        : DefaultWorkspaceManager(shared), loaded(std::move(ready)) {}
    std::shared_future<void> ready() const override { return loaded; }
  };
  World world;
  std::promise<void> loaded;
  world.shared.workspaceManager = std::make_unique<DeferredWorkspace>(
      world.shared, loaded.get_future().share());
  world.shared.textDocumentContentProvider =
      std::make_unique<cursiv::DefaultTextDocumentContentProvider>(
          world.shared, std::vector<std::string>{"builtin"});
  (void)world.server().initialize(withFolders ? world.folderParams()
                                              : lsp::InitializeParams{});
  lsp::TextDocumentContentParams params;
  params.uri = lsp::Uri::parse("builtin:/library.ws");
  std::latch started(1);
  auto answer = std::async(std::launch::async, [&]() {
    started.count_down();
    return world.server().textDocumentContent(params);
  });
  started.wait();
  CHECK(answer.wait_for(std::chrono::milliseconds(30)) ==
        std::future_status::timeout);
  world.shared.workspaceLock->write([&](const cursiv::CancellationToken&) {
    world.shared.documents->addDocument(
        world.shared.documentFactory->fromString("datatype Library",
                                                 params.uri.toString()));
  });
  loaded.set_value();
  CHECK(answer.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  const auto content = answer.get();
  CHECK(content.has_value() && content->text == "datatype Library");
}

// A library on a scheme the content provider serves has no file behind it:
// its text lives in the store. Closing its tab keeps it there, and the
// reference into it still resolves.
void a_closed_virtual_document_stays() {
  struct LibraryWorkspace final : cursiv::DefaultWorkspaceManager {
    using DefaultWorkspaceManager::DefaultWorkspaceManager;

   protected:
    void loadAdditionalDocuments(
        std::span<const cursiv::WorkspaceFolder>,
        cursiv::function_ref<void(std::string_view, std::string)> add)
        override {
      add("builtin:/library.ws", "datatype Library\n");
    }
  };
  constexpr std::string_view kLibrary = "builtin:/library.ws";
  World world;
  world.shared.workspaceManager =
      std::make_unique<LibraryWorkspace>(world.shared);
  world.shared.textDocumentContentProvider =
      std::make_unique<cursiv::DefaultTextDocumentContentProvider>(
          world.shared, std::vector<std::string>{"builtin"});
  (void)world.handshake();
  world.open("entity A { x : Library }\n");
  support::open(world.server(), kLibrary, "workspace", "datatype Library\n");

  world.close(kLibrary);
  const auto library = world.shared.documents->getDocument(kLibrary);
  CHECK(library != nullptr && !library->isOpen);
  lsp::DefinitionParams definition;
  definition.textDocument.uri = lsp::Uri::parse(std::string(kUri));
  definition.position = {0, 16};
  const auto links = world.server().definition(definition, cursiv::kNoCancel);
  CHECK(links.size() == 1 && links.front().targetUri.toString() == kLibrary);
  lsp::TextDocumentContentParams content;
  content.uri = lsp::Uri::parse(std::string(kLibrary));
  const auto text = world.server().textDocumentContent(content);
  CHECK(text.has_value() && text->text == "datatype Library\n");
}

// The handler's options are the announcement and the routing: the two arms it
// declares are promised, the four it does not stay unsaid, and the server's own
// six doors answer through it.
void the_file_operations_are_the_handlers_own() {
  World world;
  auto files = std::make_unique<RecordingFiles>();
  RecordingFiles* handler = files.get();
  world.shared.fileOperationHandler = std::move(files);

  const lsp::InitializeResult result = world.handshake();
  CHECK(result.capabilities.workspace.has_value());
  CHECK(result.capabilities.workspace->fileOperations.has_value());
  const lsp::FileOperationOptions& announced =
      *result.capabilities.workspace->fileOperations;
  CHECK(announced.didRename.has_value() && announced.willRename.has_value());
  CHECK(!announced.didCreate.has_value() && !announced.willCreate.has_value());
  CHECK(!announced.didDelete.has_value() && !announced.willDelete.has_value());
  CHECK(!announced.didRename->filters.empty() &&
        announced.didRename->filters.front().pattern.glob == "**/*.ws");

  lsp::RenameFilesParams params;
  params.files.push_back(
      lsp::FileRename{.oldUri = lsp::DocumentUri::parse(std::string(kUri)),
                      .newUri = lsp::DocumentUri::parse("file:///w/b.ws")});

  const std::optional<lsp::WorkspaceEdit> edit =
      world.server().willRenameFiles(params, cursiv::kNoCancel);
  CHECK(edit.has_value() && edit->changes.has_value());
  CHECK(edit.has_value() && edit->changes.has_value() &&
        edit->changes->size() == 1);

  world.server().didRenameFiles(params);
  CHECK(handler->renamed.size() == 1);
  CHECK(!handler->renamed.empty() &&
        handler->renamed.front().newUri.toString() == "file:///w/b.ws");

  // Without a handler the same six doors answer nothing rather than refusing.
  const World bare;
  CHECK(!bare.server().willRenameFiles(params, cursiv::kNoCancel).has_value());
  bare.server().didRenameFiles(params);
}

// A tab opened on a broken text is diagnosed; the keystroke that repairs it
// publishes the empty snapshot that clears the squiggle.
void an_edit_republishes_what_it_repaired() {
  const World world;
  (void)world.handshake();

  world.open(kBroken);
  const lsp::PublishDiagnosticsParams* broken = lastSnapshot(*world.client);
  CHECK(broken != nullptr && !broken->diagnostics.empty());

  world.change(2, kRepaired);
  const lsp::PublishDiagnosticsParams* repaired = lastSnapshot(*world.client);
  CHECK(repaired != nullptr && repaired->diagnostics.empty());
  CHECK(repaired->version.has_value() && *repaired->version == 2);
}

using support::CountingParser;

void opening_parses_once_and_unchanged_edits_keep_the_tree() {
  World world;
  auto parser = std::make_unique<CountingParser>(std::move(world.ws->parser));
  const auto* count = parser.get();
  world.ws->parser = std::move(parser);
  (void)world.handshake();
  constexpr std::string_view broken = "datatype String\nentity A { x  String }\n";
  world.open(broken);
  CHECK(count->calls == 1);
  const auto document = world.shared.documents->getDocument(kUri);
  const auto* tree = document->parseResult.value;
  lsp::CodeActionParams params;
  params.textDocument.uri = lsp::Uri::parse(kUri);
  params.range = {{0, 0}, {10, 0}};
  CHECK(!world.server().codeAction(params).empty());
  const unsigned calls = count->calls;
  world.change(2, broken);
  CHECK(document->textDocument().version() == 2);
  CHECK(document->parseResult.value == tree);
  CHECK(count->calls == calls);
  CHECK(!world.server().codeAction(params).empty());
  const auto* snapshot = lastSnapshot(*world.client);
  CHECK(snapshot != nullptr && snapshot->version == 2);
  // A delayed notification must not overwrite the newer version, even when
  // the newer notification kept identical bytes.
  world.change(1, kRepaired);
  CHECK(document->textDocument().getText() == broken);
  CHECK(document->textDocument().version() == 2);
}

void requests_use_the_language_attached_to_an_untitled_document() {
  World world;
  (void)world.handshake();
  lsp::DidOpenTextDocumentParams open;
  open.textDocument.uri = lsp::Uri::parse("untitled:Example");
  open.textDocument.languageId = "workspace";
  open.textDocument.version = 1;
  open.textDocument.text =
      "/** The type. */\ndatatype String\nentity A { x : String }\n";
  world.server().didOpen(open);
  lsp::DocumentSymbolParams symbols;
  symbols.textDocument.uri = open.textDocument.uri;
  CHECK(!world.server().documentSymbol(symbols).empty());
  lsp::DefinitionParams definition;
  definition.textDocument.uri = open.textDocument.uri;
  definition.position = {2, 17};
  const auto links = world.server().definition(definition);
  CHECK(links.size() == 1);
  if (!links.empty()) {
    CHECK(links.front().targetSelectionRange.start.line == 1);
    CHECK(links.front().targetSelectionRange.start.character == 9);
  }
  lsp::HoverParams hover;
  hover.textDocument.uri = open.textDocument.uri;
  hover.position = definition.position;
  CHECK(world.server().hover(hover).has_value());
}

void opening_can_override_the_language_of_an_existing_document() {
  World world;
  auto parser = std::make_unique<CountingParser>(
      ws::generated::Workspace::create_parser());
  const auto* counter = parser.get();
  const auto* alternate = &support::add_language<cursiv::LspServices>(
      world.shared, {"alternate", {".alt"}, {}}, std::move(parser),
      ws::generated::Workspace::create_ast_reflection());
  (void)world.handshake();
  // A file may already have been loaded under its extension's language.
  auto document = world.shared.documentFactory->fromString(
      std::string(kRepaired), kUri);
  world.shared.documents->addDocument(document);
  CHECK(document->language == world.ws);
  support::open(world.server(), kUri, "alternate", kRepaired);
  CHECK(document->language == alternate);
  CHECK(document->textDocument().languageId() == "alternate");
  CHECK(counter->calls == 1);
}

void opening_uses_the_document_factorys_construction_hook() {
  struct CustomDocument final : cursiv::Document {
    using Document::Document;
  };
  struct Factory final : cursiv::DefaultDocumentFactory {
    using DefaultDocumentFactory::DefaultDocumentFactory;
    mutable const cursiv::Document* created = nullptr;
    std::shared_ptr<cursiv::Document> createDocument(
        cursiv::TextDocument text, std::string_view uri) const override {
      auto document =
          std::make_shared<CustomDocument>(std::move(text), std::string(uri));
      created = document.get();
      return document;
    }
  };
  World world;
  auto factory = std::make_unique<Factory>(world.shared);
  const auto* observer = factory.get();
  world.shared.documentFactory = std::move(factory);
  (void)world.handshake();
  world.open(kRepaired);
  const auto document = world.shared.documents->getDocument(kUri);
  CHECK(document != nullptr && document.get() == observer->created);
}

// The one door that is not a document's: the query crosses the index, so a
// declaration of the world answers it.
void a_query_finds_the_declarations_of_the_workspace() {
  const World world;
  (void)world.handshake();
  world.open(kRepaired);

  lsp::WorkspaceSymbolParams params;
  params.query = "Missing";
  const std::vector<lsp::WorkspaceSymbol> found =
      world.server().workspaceSymbol(params, cursiv::kNoCancel);
  CHECK(found.size() == 1);
  CHECK(!found.empty() && found.front().name == "Missing");
}

// The rung this route waits for is already behind the document the build just
// carried, so the wait costs nothing and the answer arrives.
void a_reference_request_does_not_wait_on_a_built_document() {
  const World world;
  (void)world.handshake();
  world.open(kRepaired);

  lsp::ReferenceParams params;
  params.textDocument.uri = lsp::Uri::parse(std::string(kUri));
  params.position = {1, 15};  // the `Missing` written inside the entity
  params.context.includeDeclaration = true;
  const std::vector<lsp::Location> locations =
      world.server().references(params, cursiv::kNoCancel);
  CHECK(!locations.empty());
}

// A URI no language claims answers empty rather than waiting for a rung no
// build will ever take it to.
void a_request_on_an_unknown_document_answers_empty() {
  const World world;
  (void)world.handshake();

  lsp::DocumentSymbolParams params;
  params.textDocument.uri = lsp::Uri::parse("file:///w/nobody.ws");
  CHECK(world.server().documentSymbol(params, cursiv::kNoCancel).empty());
}

// The names are read off the handler at the handshake, and the handler is what
// answers: without one, neither the promise nor the answer exists.
void a_command_is_promised_and_answered_only_with_its_handler() {
  World world;
  world.shared.executeCommandHandler = std::make_unique<EchoCommands>();
  const lsp::InitializeResult result = world.handshake();
  CHECK(result.capabilities.executeCommandProvider.has_value());
  CHECK(result.capabilities.executeCommandProvider->commands ==
        std::vector<std::string>{"cursiv.echo"});

  lsp::ExecuteCommandParams params;
  params.command = "cursiv.echo";
  lsp::LSPArray arguments;
  arguments.emplace_back(std::string("hello"));
  params.arguments = std::move(arguments);
  const std::optional<lsp::LSPAny> answer =
      world.server().executeCommand(params, cursiv::kNoCancel);
  CHECK(answer.has_value() && answer->isString() &&
        answer->string() == "hello");

  // An unknown name is the caller's mistake, and it answers the same null.
  params.command = "cursiv.nothing";
  CHECK(!world.server().executeCommand(params, cursiv::kNoCancel).has_value());

  const World bare;
  CHECK(!bare.handshake().capabilities.executeCommandProvider.has_value());
  lsp::ExecuteCommandParams asked;
  asked.command = "cursiv.echo";
  CHECK(!bare.server().executeCommand(asked, cursiv::kNoCancel).has_value());
}

// A file the scan found and never opened publishes its verdict all the same:
// the client's problems view has no other door.
void a_scanned_document_publishes_without_a_tab() {
  // The last case interrupts indexing before the configuration arrives.
  struct CutScan final : cursiv::BuildPhaseListener {
    cursiv::WorkspaceLock& lock;
    bool cut = false;
    explicit CutScan(cursiv::WorkspaceLock& value) : lock(value) {}
    void onBuildPhase(cursiv::DocumentState reached,
                      std::span<cursiv::Document* const>,
                      const cursiv::CancellationToken&) override {
      if (reached == cursiv::DocumentState::IndexedContent && !cut) {
        cut = true;
        lock.cancelWrite();
      }
    }
  };
  for (unsigned root = 0; root != 6; ++root) {
    const World world(true);
    const bool configuration = root == 0 || root == 5;
    if (configuration)
      world.ws->validationRegistry->registerAfterDocument(
          [](const cursiv::Document& document, cursiv::ValidationAcceptor& accept,
             const cursiv::CancellationToken&) {
            if (document.textDocument().getText().starts_with("entity lower"))
              accept.accept(cursiv::TextRange{7, 12},
                            cursiv::DiagnosticSeverity::Warning,
                            "Type name should start with a capital.");
          },
          &cursiv::fast);
    world.disk->addFile("file:///w/b.ws",
                       configuration ? "entity lower { }\n" : std::string(kBroken));
    lsp::InitializeParams params = world.folderParams();
    if (configuration) params.capabilities.workspace.emplace().configuration = true;
    params.rootUri = lsp::Uri::parse(root == 0 ? "file:///elsewhere" : "file:///w");
    params.rootPath = root == 2 ? "/w" : "/elsewhere";
    if (root == 1 || root == 2) params.workspaceFolders.reset();
    if (root == 2) params.rootUri = nullptr;
    if (root == 3) params.workspaceFolders = lsp::Array<lsp::WorkspaceFolder>{};
    if (root == 4) params.workspaceFolders = nullptr;
    CutScan cut(*world.shared.workspaceLock);
    if (root == 5) world.shared.documentBuilder->addBuildPhaseListener(cut);
    (void)world.handshake(params);
    if (root == 5) {
      world.shared.documentBuilder->removeBuildPhaseListener(cut);
      CHECK(cut.cut);
      CHECK(world.shared.documents->getDocument("file:///w/b.ws")->state.load() ==
            cursiv::DocumentState::IndexedContent);
    }

    const lsp::PublishDiagnosticsParams* b =
        lastSnapshotOf(*world.client, "file:///w/b.ws");
    if (root < 3)
      CHECK(b != nullptr && !b->diagnostics.empty());
    else
      CHECK(b == nullptr);  // Empty folders or a scan not yet validated.
    if (configuration) {
      const auto* tree = world.shared.documents->getDocument("file:///w/b.ws")
                             ->parseResult.value;
      CHECK(static_cast<bool>(world.client->configurationAnswer));
      if (!world.client->configurationAnswer) continue;
      world.client->configurationAnswer(
          {lsp::json::parse(R"({"validation":{"categories":["built-in"]}})")});
      b = lastSnapshotOf(*world.client, "file:///w/b.ws");
      CHECK(b != nullptr && b->diagnostics.empty());

      lsp::DidChangeConfigurationParams removed;
      removed.settings = lsp::LSPAny(lsp::json::Object{});
      world.server().didChangeConfiguration(removed);
      world.client->configurationAnswer({lsp::LSPAny(nullptr)});
      b = lastSnapshotOf(*world.client, "file:///w/b.ws");
      CHECK(b != nullptr && !b->diagnostics.empty());
      CHECK(world.shared.documents->getDocument("file:///w/b.ws")
                ->parseResult.value == tree);
    }
  }
}

// Closing a tab hands the document back to its file: a repaired buffer over a
// broken file leaves the file's own verdict on the client, and a buffer with
// no file leaves the workspace with the empty, versionless snapshot.
void a_closed_tab_keeps_the_verdict_of_its_file() {
  const World world(true);
  world.disk->addFile(std::string(kUri), std::string(kBroken));
  (void)world.handshake(world.folderParams());
  world.open(kRepaired);
  const lsp::PublishDiagnosticsParams* repaired =
      lastSnapshotOf(*world.client, kUri);
  CHECK(repaired != nullptr && repaired->diagnostics.empty());

  world.close();
  const lsp::PublishDiagnosticsParams* onDisk =
      lastSnapshotOf(*world.client, kUri);
  CHECK(onDisk != nullptr && !onDisk->diagnostics.empty());
  CHECK(world.shared.documents->getDocument(kUri) != nullptr);

  const World bare;
  (void)bare.handshake();
  bare.open(kBroken);
  bare.close();
  const lsp::PublishDiagnosticsParams* gone =
      lastSnapshotOf(*bare.client, kUri);
  CHECK(gone != nullptr && gone->diagnostics.empty() &&
        !gone->version.has_value());
  CHECK(bare.shared.documents->getDocument(kUri) == nullptr);
}

// A file created outside the editor reaches the workspace through the
// watcher, and its verdict reaches the client.
void a_watched_file_joins_the_workspace() {
  const World world(true);
  (void)world.handshake(world.folderParams());
  world.disk->addFile("file:///w/c.ws", std::string(kBroken));

  lsp::DidChangeWatchedFilesParams params;
  params.changes.push_back(
      lsp::FileEvent{.uri = lsp::Uri::parse("file:///w/c.ws"),
                     .type = lsp::FileChangeType::Created});
  world.server().didChangeWatchedFiles(params);

  CHECK(world.shared.documents->getDocument("file:///w/c.ws") != nullptr);
  const lsp::PublishDiagnosticsParams* c =
      lastSnapshotOf(*world.client, "file:///w/c.ws");
  CHECK(c != nullptr && !c->diagnostics.empty());
}

// The names a workspace query answers.
std::vector<std::string> symbol_names(const World& world,
                                      std::string_view query) {
  lsp::WorkspaceSymbolParams params;
  params.query = std::string(query);
  std::vector<std::string> names;
  for (const lsp::WorkspaceSymbol& symbol :
       world.server().workspaceSymbol(params, cursiv::kNoCancel))
    names.push_back(symbol.name);
  return names;
}

// A watched event brings in only what the scan would: a file under a folder
// the scan leaves out -- hidden, `node_modules`, `out`, at any depth below the
// root -- stays out, and so does what a folder event on one would expand to.
// The folders above a root are not asked: a workspace opened inside a hidden
// folder still takes its files.
void a_watched_file_in_an_excluded_folder_stays_out() {
  const World world(true);
  (void)world.handshake(world.folderParams());
  const std::string_view evented[] = {
      "file:///w/node_modules/v.ws", "file:///w/src/node_modules/pkg/v.ws",
      "file:///w/out/o.ws", "file:///w/.cache/h.ws"};
  constexpr std::string_view kUnderFolder = "file:///w/node_modules/lib/x.ws";
  constexpr std::string_view kKept = "file:///w/src/k.ws";
  lsp::DidChangeWatchedFilesParams params;
  for (const std::string_view uri : evented) {
    world.disk->addFile(std::string(uri), "datatype Vendor\n");
    params.changes.push_back(
        lsp::FileEvent{.uri = lsp::Uri::parse(std::string(uri)),
                       .type = lsp::FileChangeType::Changed});
  }
  world.disk->addFile(std::string(kUnderFolder), "datatype Vendor\n");
  params.changes.push_back(
      lsp::FileEvent{.uri = lsp::Uri::parse("file:///w/node_modules"),
                     .type = lsp::FileChangeType::Created});
  world.disk->addFile(std::string(kKept), "datatype Kept\n");
  params.changes.push_back(
      lsp::FileEvent{.uri = lsp::Uri::parse(std::string(kKept)),
                     .type = lsp::FileChangeType::Created});
  world.server().didChangeWatchedFiles(params);

  for (const std::string_view uri : evented)
    CHECK(world.shared.documents->getDocument(uri) == nullptr);
  CHECK(world.shared.documents->getDocument(kUnderFolder) == nullptr);
  CHECK(world.shared.documents->getDocument(kKept) != nullptr);
  CHECK(symbol_names(world, "Vendor").empty());

  const World hidden(true);
  lsp::InitializeParams folders;
  folders.workspaceFolders =
      lsp::Array<lsp::WorkspaceFolder>{lsp::WorkspaceFolder{
          .uri = lsp::Uri::parse("file:///.config/w"), .name = "w"}};
  (void)hidden.handshake(folders);
  constexpr std::string_view kInside = "file:///.config/w/k.ws";
  hidden.disk->addFile(std::string(kInside), "datatype Kept\n");
  lsp::DidChangeWatchedFilesParams created;
  created.changes.push_back(
      lsp::FileEvent{.uri = lsp::Uri::parse(std::string(kInside)),
                     .type = lsp::FileChangeType::Created});
  hidden.server().didChangeWatchedFiles(created);
  CHECK(hidden.shared.documents->getDocument(kInside) != nullptr);
}

// A file outside the workspace folders closes like one inside: its saved
// content replaces the buffer, and the document stays indexed.
void a_closed_file_outside_the_folders_keeps_its_saved_content() {
  const World world(true);
  constexpr std::string_view kOutside = "file:///elsewhere/b.ws";
  world.disk->addFile(std::string(kUri), "datatype Main\n");
  world.disk->addFile(std::string(kOutside), "datatype Saved\n");
  (void)world.handshake(world.folderParams());
  CHECK(world.shared.documents->getDocument(kOutside) == nullptr);

  support::open(world.server(), kOutside, "workspace", "datatype Buffered\n");
  CHECK(symbol_names(world, "Buffered").size() == 1);
  world.close(kOutside);

  const std::shared_ptr<cursiv::Document> closed =
      world.shared.documents->getDocument(kOutside);
  CHECK(closed != nullptr && !closed->isOpen);
  CHECK(closed != nullptr &&
        closed->textDocument().getText() == "datatype Saved\n");
  CHECK(symbol_names(world, "Saved").size() == 1);
  CHECK(symbol_names(world, "Buffered").empty());
}

// A buffer the file system cannot name -- `untitled:` under the local
// provider -- still closes, and with nothing saved behind it, it leaves: its
// symbols go, and a reference to it breaks.
void a_closed_untitled_buffer_leaves_the_workspace() {
  World world;
  world.shared.fileSystemProvider =
      std::make_unique<cursiv::LocalFileSystemProvider>();
  (void)world.handshake();
  constexpr std::string_view kUntitled = "untitled:Unsaved-1";
  support::open(world.server(), kUntitled, "workspace", "datatype Ghost\n");
  world.open("entity A { x : Ghost }\n");
  lsp::DefinitionParams definition;
  definition.textDocument.uri = lsp::Uri::parse(std::string(kUri));
  definition.position = {0, 16};
  const auto before = world.server().definition(definition, cursiv::kNoCancel);
  CHECK(before.size() == 1 && before.front().targetUri.toString() == kUntitled);

  bool closed = true;
  try {
    world.close(kUntitled);
  } catch (const cursiv::FileSystemError&) {
    closed = false;
  }
  CHECK(closed);
  CHECK(world.shared.documents->getDocument(kUntitled) == nullptr);
  CHECK(symbol_names(world, "Ghost").empty());
  CHECK(world.server().definition(definition, cursiv::kNoCancel).empty());
  const lsp::PublishDiagnosticsParams* gone =
      lastSnapshotOf(*world.client, kUntitled);
  CHECK(gone != nullptr && gone->diagnostics.empty());
  const lsp::PublishDiagnosticsParams* broken =
      lastSnapshotOf(*world.client, kUri);
  CHECK(broken != nullptr && !broken->diagnostics.empty());
}

// Answers a keystroke with the character typed, under the one trigger it
// names: what reaches it is readable from the edit.
struct KeystrokeFormatter final : cursiv::Formatter {
  std::string trigger;
  explicit KeystrokeFormatter(std::string ch) : trigger(std::move(ch)) {}

  [[nodiscard]] std::vector<lsp::TextEdit> formatDocument(
      const cursiv::Document&, const lsp::DocumentFormattingParams&,
      const cursiv::CancellationToken&) const override {
    return {};
  }
  [[nodiscard]] std::vector<lsp::TextEdit> formatDocumentRange(
      const cursiv::Document&, const lsp::DocumentRangeFormattingParams&,
      const cursiv::CancellationToken&) const override {
    return {};
  }
  [[nodiscard]] std::vector<lsp::TextEdit> formatDocumentOnType(
      const cursiv::Document&,
      const lsp::DocumentOnTypeFormattingParams& params,
      const cursiv::CancellationToken&) const override {
    return {lsp::TextEdit{.range = {}, .newText = params.ch}};
  }
  [[nodiscard]] std::optional<lsp::DocumentOnTypeFormattingOptions>
  formatOnTypeOptions() const noexcept override {
    return lsp::DocumentOnTypeFormattingOptions{.firstTriggerCharacter =
                                                    trigger};
  }
};

// The on-type characters are a union over the languages, like the completion
// and signature ones, since the session announces one set. Each language
// still answers only the characters it named.
void the_on_type_triggers_are_a_union_over_the_languages() {
  World world;
  world.ws->formatter = std::make_unique<KeystrokeFormatter>("}");
  (void)support::add_language<cursiv::LspServices>(
      world.shared, {"second", {".second"}, {}},
      ws::generated::Workspace::create_parser(),
      ws::generated::Workspace::create_ast_reflection(),
      [](cursiv::LspServices& second) {
        second.formatter = std::make_unique<KeystrokeFormatter>(";");
      });

  const lsp::InitializeResult result = world.handshake();
  const auto& announced = result.capabilities.documentOnTypeFormattingProvider;
  CHECK(announced.has_value() && announced->firstTriggerCharacter == "}" &&
        announced->moreTriggerCharacter ==
            std::optional<lsp::Array<lsp::String>>{{";"}});

  constexpr std::string_view kSecond = "file:///w/b.second";
  world.open("entity A { }\n");
  support::open(world.server(), kSecond, "second", "entity B { }\n");
  const auto typed = [&](std::string_view uri, std::string ch) {
    lsp::DocumentOnTypeFormattingParams params;
    params.textDocument.uri = lsp::Uri::parse(std::string(uri));
    params.ch = std::move(ch);
    return world.server().onTypeFormatting(params, cursiv::kNoCancel).size();
  };
  CHECK(typed(kUri, "}") == 1);
  CHECK(typed(kUri, ";") == 0);
  CHECK(typed(kSecond, ";") == 1);
  CHECK(typed(kSecond, "}") == 0);
}

// Every write a notification makes is durable: what the client already
// committed on its side is installed whatever arrives behind it.
void every_notification_writes_durably() {
  const World world(true);
  (void)world.handshake(world.folderParams());
  world.open(kBroken);
  world.change(2, kRepaired);
  world.close();
  lsp::DidChangeWatchedFilesParams params;
  params.changes.push_back(
      lsp::FileEvent{.uri = lsp::Uri::parse(std::string(kUri)),
                     .type = lsp::FileChangeType::Changed});
  world.server().didChangeWatchedFiles(params);

  CHECK(world.lock->modes.size() >= 5);
  for (const cursiv::WorkspaceLock::WriteMode mode : world.lock->modes)
    CHECK(mode == cursiv::WorkspaceLock::WriteMode::Durable);
}

// An open on bytes the scan already read installs nothing, and still builds:
// the build is what carries a document a cut build left behind.
void an_open_on_known_bytes_still_builds() {
  const World world(true);
  world.disk->addFile(std::string(kUri), std::string(kRepaired));
  (void)world.handshake(world.folderParams());
  const std::shared_ptr<cursiv::Document> document =
      world.shared.documents->getDocument(kUri);
  CHECK(document != nullptr);
  world.shared.workspaceLock->write([&](const cursiv::CancellationToken&) {
    world.shared.documentBuilder->resetToState(*document,
                                               cursiv::DocumentState::Changed);
  });
  CHECK(document->state.load() == cursiv::DocumentState::Changed);

  world.open(kRepaired);
  CHECK(document->state.load() == cursiv::DocumentState::Validated);
}

// A command runs with no lock held, so one that writes to the workspace
// returns instead of waiting on itself.
struct WritingCommands final : cursiv::AbstractExecuteCommandHandler {
  cursiv::SharedLspServices& shared;
  explicit WritingCommands(cursiv::SharedLspServices& s) : shared(s) {}

 protected:
  void registerCommands(
      const cursiv::ExecuteCommandAcceptor& acceptor) const override {
    acceptor(
        "cursiv.write",
        [this](const lsp::LSPArray&,
               const cursiv::CancellationToken&) -> std::optional<lsp::LSPAny> {
          bool ran = false;
          shared.workspaceLock->write(
              [&](const cursiv::CancellationToken&) { ran = true; });
          return lsp::LSPAny(ran);
        });
  }
};

void a_command_may_write_to_the_workspace() {
  World world;
  world.shared.executeCommandHandler =
      std::make_unique<WritingCommands>(world.shared);
  (void)world.handshake();
  lsp::ExecuteCommandParams params;
  params.command = "cursiv.write";
  const std::optional<lsp::LSPAny> answer =
      world.server().executeCommand(params, cursiv::kNoCancel);
  CHECK(answer.has_value() && answer->isBoolean() && answer->boolean());
}

// The watcher is registered only with a client that said it accepts one.
void the_watcher_follows_the_clients_capability() {
  const World silent;
  (void)silent.handshake();
  CHECK(silent.client->registered.empty());

  const World world;
  lsp::InitializeParams params;
  lsp::WorkspaceClientCapabilities workspace;
  lsp::DidChangeWatchedFilesClientCapabilities watched;
  watched.dynamicRegistration = true;
  workspace.didChangeWatchedFiles = std::move(watched);
  params.capabilities.workspace = std::move(workspace);
  (void)world.handshake(params);
  CHECK(world.client->registered.size() == 1);
  CHECK(!world.client->registered.empty() &&
        world.client->registered.front().registrations.front().method ==
            "workspace/didChangeWatchedFiles");
}

}  // namespace

void run() {
  the_installed_defaults_leave_no_slot_owed();
  the_handshake_promises_only_what_is_installed();
  the_completion_triggers_are_the_providers_own();
  completion_edits_follow_insert_replace_support();
  prose_follows_the_clients_preferred_format();
  virtual_content_waits_for_the_workspace_scan(false);
  virtual_content_waits_for_the_workspace_scan(true);
  a_closed_virtual_document_stays();
  the_file_operations_are_the_handlers_own();
  an_edit_republishes_what_it_repaired();
  opening_parses_once_and_unchanged_edits_keep_the_tree();
  requests_use_the_language_attached_to_an_untitled_document();
  opening_can_override_the_language_of_an_existing_document();
  opening_uses_the_document_factorys_construction_hook();
  a_query_finds_the_declarations_of_the_workspace();
  a_reference_request_does_not_wait_on_a_built_document();
  a_request_on_an_unknown_document_answers_empty();
  a_command_is_promised_and_answered_only_with_its_handler();
  a_scanned_document_publishes_without_a_tab();
  a_closed_tab_keeps_the_verdict_of_its_file();
  a_watched_file_joins_the_workspace();
  a_watched_file_in_an_excluded_folder_stays_out();
  a_closed_file_outside_the_folders_keeps_its_saved_content();
  a_closed_untitled_buffer_leaves_the_workspace();
  the_on_type_triggers_are_a_union_over_the_languages();
  every_notification_writes_durably();
  an_open_on_known_bytes_still_builds();
  a_command_may_write_to_the_workspace();
  the_watcher_follows_the_clients_capability();
}

}  // namespace language_server

namespace language_server_runtime {

namespace {
using namespace std::chrono_literals;
struct Channel {
  std::mutex mutex;
  std::condition_variable ready;
  std::string bytes;
  bool closed = false;
  void close() {
    const std::lock_guard lock(mutex);
    closed = true;
    ready.notify_all();
  }
};
struct Stream final : lsp::io::Stream {
  Channel& input;
  Channel& output;
  Stream(Channel& in, Channel& out) : input(in), output(out) {}
  void read(char* buffer, std::size_t size) override {
    std::unique_lock lock(input.mutex);
    input.ready.wait(
        lock, [&] { return input.closed || input.bytes.size() >= size; });
    if (input.bytes.size() < size) throw lsp::io::Error("closed test stream");
    std::memcpy(buffer, input.bytes.data(), size);
    input.bytes.erase(0, size);
  }
  void write(const char* buffer, std::size_t size) override {
    const std::lock_guard lock(output.mutex);
    if (output.closed) throw lsp::io::Error("closed test stream");
    output.bytes.append(buffer, size);
    output.ready.notify_all();
  }
};
struct StopLock final : cursiv::DefaultWorkspaceLock {
  std::latch stopped{1};
  std::atomic<bool> notified{false};
  std::function<void()> beforeCancel;
  void cancelWrite() noexcept override {
    if (beforeCancel) beforeCancel();
    cursiv::DefaultWorkspaceLock::cancelWrite();
    if (!notified.exchange(true)) stopped.count_down();
  }
};
struct Server final : cursiv::DefaultLanguageServer {
  cursiv::SharedLspServices& shared;
  explicit Server(cursiv::SharedLspServices& services)
      : DefaultLanguageServer(services), shared(services) {}
  std::latch entered{1};
  std::latch release{1};
  std::promise<void> writeStarted;
  bool writeOnOpen = false;
  std::atomic<bool> openCancelled{false};
  mutable std::atomic<unsigned> symbols{0};
  lsp::InitializeResult initialize(const lsp::InitializeParams&) override {
    return {};
  }
  void didOpen(const lsp::DidOpenTextDocumentParams&) override {
    if (writeOnOpen) {
      shared.workspaceLock->write(
          [&](const cursiv::CancellationToken& token) {
            shared.workspaceLock->settle();
            entered.count_down();
            writeStarted.set_value();
            release.wait();
            openCancelled = token.stop_requested();
          },
          cursiv::WorkspaceLock::WriteMode::Durable);
      return;
    }
    entered.count_down();
    release.wait();
  }
  std::vector<lsp::DocumentSymbol> documentSymbol(
      const lsp::DocumentSymbolParams&,
      const cursiv::CancellationToken&) const override {
    ++symbols;
    return outline();
  }
  // `Outer` holds `Inner`, which holds `Deep`; `Next` stands beside `Outer`.
  static std::vector<lsp::DocumentSymbol> outline() {
    const auto symbol = [](std::string name, lsp::Range range) {
      lsp::DocumentSymbol out;
      out.name = std::move(name);
      out.kind = lsp::SymbolKindEnum(lsp::SymbolKind::Class);
      out.range = range;
      out.selectionRange = {range.start, range.start};
      return out;
    };
    lsp::DocumentSymbol inner = symbol("Inner", {{1, 2}, {2, 3}});
    inner.children.emplace().push_back(symbol("Deep", {{2, 4}, {2, 8}}));
    lsp::DocumentSymbol outer = symbol("Outer", {{0, 0}, {3, 1}});
    outer.children.emplace().push_back(std::move(inner));
    std::vector<lsp::DocumentSymbol> top;
    top.push_back(std::move(outer));
    top.push_back(symbol("Next", {{4, 0}, {4, 9}}));
    return top;
  }
  // A quick fix with an edit, a bare command, and a disabled command. Asked
  // on line 1, only the quick fix.
  std::vector<lsp::CodeAction> codeAction(
      const lsp::CodeActionParams& params,
      const cursiv::CancellationToken&) const override {
    lsp::CodeAction fix;
    fix.title = "Fix";
    fix.kind = lsp::CodeActionKindEnum(lsp::CodeActionKind::QuickFix);
    fix.edit = lsp::WorkspaceEdit{};
    if (params.range.start.line == 1) return {fix};
    lsp::CodeAction run;
    run.title = "Run";
    run.command = lsp::Command{.title = "Run it", .command = "cursiv.run"};
    lsp::CodeAction disabled = run;
    disabled.title = "Not now";
    disabled.disabled = lsp::CodeActionDisabled{.reason = "busy"};
    return {fix, run, disabled};
  }
  static std::vector<lsp::LocationLink> navigation() {
    return {{.targetUri = lsp::Uri::parse("file:///target.ws"),
             .targetRange = {{3, 0}, {3, 20}},
             .targetSelectionRange = {{3, 5}, {3, 9}},
             .originSelectionRange = lsp::Range{{1, 2}, {1, 4}}}};
  }
  std::vector<lsp::LocationLink> definition(
      const lsp::DefinitionParams&, const cursiv::CancellationToken&) const override {
    return navigation();
  }
  std::vector<lsp::LocationLink> declaration(
      const lsp::DeclarationParams&, const cursiv::CancellationToken&) const override {
    return navigation();
  }
  std::vector<lsp::LocationLink> typeDefinition(
      const lsp::TypeDefinitionParams&, const cursiv::CancellationToken&) const override {
    return navigation();
  }
  std::vector<lsp::LocationLink> implementation(
      const lsp::ImplementationParams&, const cursiv::CancellationToken&) const override {
    return navigation();
  }
};
struct Session {
  cursiv::SharedLspServices shared;
  Channel requests, responses;
  Stream serverStream{requests, responses};
  Stream clientStream{responses, requests};
  lsp::Connection client{clientStream};
  Server* server;
  StopLock* lock;
  std::future<int> running;
  Session() {
    cursiv::install_default_shared_lsp_services(shared);
    auto heldLock = std::make_unique<StopLock>();
    lock = heldLock.get();
    shared.workspaceLock = std::move(heldLock);
    auto held = std::make_unique<Server>(shared);
    server = held.get();
    shared.languageServer = std::move(held);
    running = std::async(std::launch::async, [&] {
      return cursiv::start_language_server(shared, serverStream);
    });
  }
  void request(int id, std::string_view method,
               std::string_view params = "{}") {
    auto sender = client.request(method, lsp::json::Integer(id));
    sender.writeParams(lsp::json::parse(params));
    sender.submit();
  }
  void notify(std::string_view method, std::string_view params = "{}") {
    auto sender = client.notification(method);
    sender.writeParams(lsp::json::parse(params));
    sender.submit();
  }
  auto answer() {
    return std::async(std::launch::async, [&] {
      auto message = client.readMessage();
      return std::get<lsp::jsonrpc::Response>(
          std::get<lsp::jsonrpc::Message>(std::move(message)));
    });
  }
  void initialize(std::string_view capabilities = "{}") {
    request(1, "initialize", std::string(
        R"({"processId":null,"rootUri":null,"capabilities":)") +
        std::string(capabilities) + "}");
    CHECK(!answer().get().error.has_value());
  }
  void open() {
    notify(
        "textDocument/didOpen",
        R"({"textDocument":{"uri":"untitled:one","languageId":"test","version":1,"text":"x"}})");
    server->entered.wait();
  }
  ~Session() {
    requests.close();
    if (running.valid()) running.wait();
    responses.close();
  }
};

void shutdown_obeys_the_handshake() {
  for (unsigned mode = 0; mode != 4; ++mode) {
    Session s;
    s.request(1, "shutdown");
    auto reply = s.answer().get();
    CHECK(reply.error.has_value() && reply.error->code == -32002);
    const std::string_view capabilities[] = {
        "{}",
        R"({"textDocument":{"definition":{"linkSupport":false},"declaration":{"linkSupport":false},"typeDefinition":{"linkSupport":false},"implementation":{"linkSupport":false}}})",
        R"({"textDocument":{"definition":{"linkSupport":true},"declaration":{"linkSupport":false},"typeDefinition":{"linkSupport":false},"implementation":{"linkSupport":true}}})",
        R"({"textDocument":{"definition":{"linkSupport":false},"declaration":{"linkSupport":true},"typeDefinition":{"linkSupport":true},"implementation":{"linkSupport":false}}})"};
    s.initialize(capabilities[mode]);
    const std::string_view methods[] = {
        "textDocument/definition", "textDocument/declaration",
        "textDocument/typeDefinition", "textDocument/implementation"};
    for (unsigned method = 0; method != 4; ++method) {
      s.request(10 + method, methods[method],
                R"({"textDocument":{"uri":"file:///source.ws"},"position":{"line":1,"character":2}})");
      const auto answer = s.answer().get();
      CHECK(answer.result.has_value() && answer.result->isArray() &&
            answer.result->array().size() == 1);
      if (!answer.result.has_value() || !answer.result->isArray() ||
          answer.result->array().size() != 1) continue;
      const auto& target = answer.result->array().front().object();
      const bool links = (mode == 2 && (method == 0 || method == 3)) ||
                         (mode == 3 && (method == 1 || method == 2));
      CHECK(target.contains(links ? "targetUri" : "uri"));
      CHECK(target.contains("originSelectionRange") == links);
      const auto range = target.find(links ? "targetSelectionRange" : "range");
      CHECK(range != nullptr);
      if (range != nullptr)
        CHECK(range->object().get("start").object().get("character").integer() == 5);
    }
    s.request(2, "shutdown");
    CHECK(!s.answer().get().error.has_value());
    s.request(3, "shutdown");
    reply = s.answer().get();
    CHECK(reply.error.has_value() && reply.error->code == -32600);
    s.notify("exit");
    CHECK(s.running.get() == 0);
  }
}

// The outline and the code actions take the shapes the client declares it
// reads: the tree and the action literals when it says so. Otherwise the flat
// symbol list, each symbol naming its parent, and bare commands, where an
// action with an edit or marked disabled has no place.
void answers_take_the_shapes_the_client_reads() {
  const std::string_view capabilities[] = {
      "{}",
      R"({"textDocument":{"documentSymbol":{"hierarchicalDocumentSymbolSupport":false},"codeAction":{"dynamicRegistration":true}}})",
      R"({"textDocument":{"documentSymbol":{"hierarchicalDocumentSymbolSupport":true},"codeAction":{"codeActionLiteralSupport":{"codeActionKind":{"valueSet":["quickfix"]}}}}})"};
  for (unsigned mode = 0; mode != 3; ++mode) {
    const bool rich = mode == 2;
    Session s;
    s.initialize(capabilities[mode]);

    s.request(2, "textDocument/documentSymbol",
              R"({"textDocument":{"uri":"file:///source.ws"}})");
    const auto outline = s.answer().get();
    CHECK(outline.result.has_value() && outline.result->isArray());
    if (outline.result.has_value() && outline.result->isArray()) {
      const auto& symbols = outline.result->array();
      const auto name = [&](std::size_t i) {
        return symbols[i].object().get("name").string();
      };
      if (rich) {
        CHECK(symbols.size() == 2 && name(0) == "Outer" && name(1) == "Next");
        CHECK(symbols.front().object().contains("children"));
      } else {
        CHECK(symbols.size() == 4);
        const char* const names[] = {"Outer", "Inner", "Deep", "Next"};
        const char* const containers[] = {nullptr, "Outer", "Inner", nullptr};
        for (std::size_t i = 0; i != symbols.size() && i != 4; ++i) {
          const auto& symbol = symbols[i].object();
          CHECK(name(i) == names[i]);
          CHECK(!symbol.contains("children"));
          const auto* container = symbol.find("containerName");
          CHECK((container != nullptr) == (containers[i] != nullptr));
          if (container != nullptr && containers[i] != nullptr)
            CHECK(container->string() == containers[i]);
          const auto* location = symbol.find("location");
          CHECK(location != nullptr);
          if (location == nullptr || i != 2) continue;
          CHECK(location->object().get("uri").string() == "file:///source.ws");
          const auto& range = location->object().get("range").object();
          CHECK(range.get("end").object().get("character").integer() == 8);
        }
      }
    }

    s.request(
        3, "textDocument/codeAction",
        R"({"textDocument":{"uri":"file:///source.ws"},"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":0}},"context":{"diagnostics":[]}})");
    const auto actions = s.answer().get();
    CHECK(actions.result.has_value() && actions.result->isArray());
    if (actions.result.has_value() && actions.result->isArray()) {
      const auto& offered = actions.result->array();
      if (rich) {
        CHECK(offered.size() == 3 && offered.front().object().contains("edit"));
        for (const auto& action : offered) {
          const auto* command = action.object().find("command");
          CHECK(command == nullptr || command->isObject());
        }
      } else {
        CHECK(offered.size() == 1);
        if (offered.size() == 1) {
          CHECK(offered.front().object().get("title").string() == "Run it");
          CHECK(offered.front().object().get("command").string() ==
                "cursiv.run");
        }
      }
    }

    // Nothing a bare command can carry: the null of "nothing to offer".
    s.request(
        4, "textDocument/codeAction",
        R"({"textDocument":{"uri":"file:///source.ws"},"range":{"start":{"line":1,"character":0},"end":{"line":1,"character":0}},"context":{"diagnostics":[]}})");
    const auto editsOnly = s.answer().get();
    CHECK(editsOnly.result.has_value());
    if (editsOnly.result.has_value())
      CHECK(rich ? editsOnly.result->isArray() &&
                       editsOnly.result->array().size() == 1
                 : editsOnly.result->isNull());

    s.request(5, "shutdown");
    CHECK(!s.answer().get().error.has_value());
    s.notify("exit");
    CHECK(s.running.get() == 0);
  }
}

void cancellation_does_not_wait_for_an_earlier_notification() {
  Session s;
  s.initialize();
  s.open();
  s.request(2, "textDocument/documentSymbol",
            R"({"textDocument":{"uri":"untitled:one"}})");
  s.notify("$/cancelRequest", R"({"id":2})");
  auto pending = s.answer();
  const bool cancelled = pending.wait_for(2s) == std::future_status::ready;
  CHECK(cancelled);
  // Always release on failure, so the old implementation reports rather
  // than hanging the test process.
  s.server->release.count_down();
  const auto reply = pending.get();
  CHECK(reply.error.has_value() && reply.error->code == -32800);
  CHECK(s.server->symbols == 0);
  s.notify("exit");
  CHECK(s.running.get() == 1);
}

void exit_cancels_a_request_behind_a_dropped_notification() {
  Session s;
  s.initialize();
  s.open();
  s.request(2, "textDocument/documentSymbol",
            R"({"textDocument":{"uri":"untitled:one"}})");
  s.notify("exit");
  // Stop has marked the notification queue before the held notification
  // finishes. Its waiter must leave as cancelled, without calling a provider.
  s.lock->stopped.wait();
  s.server->release.count_down();
  CHECK(s.running.get() == 1);
  CHECK(s.server->symbols == 0);
  CHECK(s.shared.languageClient == nullptr);
}

// Answers still in flight when the client goes away fail to write; the
// session ends on the read side, without the endpoint answering them twice.
void a_client_gone_mid_request_ends_the_session() {
  Session s;
  s.initialize();
  s.open();
  for (int id = 2; id < 10; ++id)
    s.request(id, "textDocument/documentSymbol",
              R"({"textDocument":{"uri":"untitled:one"}})");
  s.responses.close();
  s.requests.close();
  s.server->release.count_down();
  CHECK(s.running.get() == 1);
}

void a_notification_does_not_cancel_its_own_write() {
  Session s;
  s.initialize();
  s.shared.workspaceManager->initializeWorkspace({});
  s.server->writeOnOpen = true;
  auto started = s.server->writeStarted.get_future();
  std::atomic<bool> first{true};
  s.lock->beforeCancel = [&] {
    // Give a wrongly published task time to begin its write before
    // cancellation. With the correct order the task is still unpublished and
    // cannot begin.
    if (first.exchange(false)) (void)started.wait_for(100ms);
  };
  s.open();
  s.lock->stopped.wait();
  s.server->release.count_down();
  s.request(2, "textDocument/documentSymbol",
            R"({"textDocument":{"uri":"untitled:one"}})");
  CHECK(!s.answer().get().error.has_value());
  CHECK(!s.server->openCancelled);
  s.notify("exit");
  CHECK(s.running.get() == 1);
}
}  // namespace

void run() {
  shutdown_obeys_the_handshake();
  answers_take_the_shapes_the_client_reads();
  cancellation_does_not_wait_for_an_earlier_notification();
  exit_cancels_a_request_behind_a_dropped_notification();
  a_client_gone_mid_request_ends_the_session();
  a_notification_does_not_cancel_its_own_write();
}

}  // namespace language_server_runtime

// The server binary's `main`, driven with `--complete-at`: the workspace
// scanned is the `--folder` given, else the file's own directory, and a
// name declared one folder up is offered only through the former.
namespace language_server_main {

namespace {

bool register_workspace(cursiv::SharedLspServices& shared) {
  (void)support::add_workspace<cursiv::LspServices>(shared);
  return true;
}

// `a.ws` declares a type; `sub/b.ws` asks for one at its caret.
[[nodiscard]] std::filesystem::path scratch() {
  const std::filesystem::path root =
      std::filesystem::path(CURSIV_TEST_SCRATCH_DIR) / "cursiv main # % lane";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "sub");
  std::ofstream(root / "a.ws") << "datatype Alpha\n";
  std::ofstream(root / "sub" / "b.ws") << "entity E { f : \n}\n";
  return root;
}

// What the binary prints for one command line.
[[nodiscard]] std::string ran(std::vector<std::string> args,
                            int expectedStatus = 0) {
  std::vector<char*> argv;
  argv.push_back(const_cast<char*>("srv"));
  for (std::string& arg : args) argv.push_back(arg.data());
  std::ostringstream captured;
  std::streambuf* const out = std::cout.rdbuf(captured.rdbuf());
  cursiv::SharedLspServices shared;
  const int status = cursiv::detail::run_language_server_main_on(
      static_cast<int>(argv.size()), argv.data(), "srv", &register_workspace,
      shared);
  std::cout.rdbuf(out);
  CHECK(status == expectedStatus);
  return captured.str();
}

void the_folder_given_is_the_workspace_scanned() {
  const std::filesystem::path root = scratch();
  const std::string caret = (root / "sub" / "b.ws").string() + ":0:15";

  const std::string wide =
      ran({"--folder=" + root.string(), "--complete-at=" + caret});
  CHECK(wide.find("Alpha") != std::string::npos);

  // Without it, the file's own directory: the type one folder up is unseen.
  const std::string narrow = ran({"--complete-at=" + caret});
  CHECK(narrow.find("Alpha") == std::string::npos);
  CHECK(narrow.find("item(s)") != std::string::npos);
}

void invalid_completion_coordinates_are_rejected() {
  for (const std::string_view suffix :
       {"4294967296:15", "0:4294967296", "-1:15", "0:15x", "0:"})
    (void)ran({"--complete-at=file.ws:" + std::string(suffix)}, 2);
}

}  // namespace

void run() {
  the_folder_given_is_the_workspace_scanned();
  invalid_completion_coordinates_are_rejected();
}

}  // namespace language_server_main

// The command table: names announced in registration order, the last
// registration of a name winning in its first place, an unknown name
// answering nothing.
namespace execute_command_handler {

namespace {

[[nodiscard]] cursiv::ExecuteCommandFunction answering(std::string what) {
  return [what = std::move(what)](
             const lsp::LSPArray&,
             const cursiv::CancellationToken&) -> std::optional<lsp::LSPAny> {
    return lsp::LSPAny(std::string(what));
  };
}

// Three names out of alphabetical order, the first one registered twice.
struct ThreeCommands final : cursiv::AbstractExecuteCommandHandler {
 protected:
  void registerCommands(
      const cursiv::ExecuteCommandAcceptor& acceptor) const override {
    acceptor("zeta", answering("z1"));
    acceptor("alpha", answering("a"));
    acceptor("mid", answering("m"));
    acceptor("zeta", answering("z2"));
  }
};

void the_names_keep_their_registration_order() {
  const ThreeCommands handler;
  const std::vector<std::string> expected = {"zeta", "alpha", "mid"};
  CHECK(handler.commands() == expected);
}

void the_last_registration_of_a_name_wins_in_place() {
  const ThreeCommands handler;
  const std::optional<lsp::LSPAny> answer =
      handler.executeCommand("zeta", {}, cursiv::kNoCancel);
  CHECK(answer.has_value() && answer->isString() && answer->string() == "z2");
  CHECK(!handler.executeCommand("none", {}, cursiv::kNoCancel).has_value());
}

}  // namespace

void run() {
  the_names_keep_their_registration_order();
  the_last_registration_of_a_name_wins_in_place();
}

}  // namespace execute_command_handler

// The two hot doors -- colouring and completion -- asked of two service
// containers built the same way over the nested-document fixture. What is
// pinned is that neither answer depends on the container it was asked of.
namespace lsp_services {

namespace {

// One world per engine, everything else identical: the providers, the document
// factory and the builder are the shipped ones, but for the keyword filter.
struct World : support::World<cursiv::SharedLspServices> {
  cursiv::LspServices* doc = &support::add_language<cursiv::LspServices>(
      shared, {"doc", {".doc"}, {}}, fdoc::generated::DocG::create_parser(),
      fdoc::generated::DocG::create_ast_reflection());

  mutable unsigned minted = 0;

  cursiv::Document* built(std::string text) const {
    return support::built(shared,
                          "file:///w/a" + std::to_string(minted++) + ".doc",
                          std::move(text));
  }
};

const std::string kDocs[] = {
    R"({"name":"cursiv","tags":["peg",true,null],"n":{"deep":[[1.5]]}})",
    R"([1,2,3,{"a":"b"},false])",
    R"({"empty":{},"list":[],"neg":-2.5e3})",
};

[[nodiscard]] std::vector<std::string> labels(const lsp::CompletionList& list) {
  std::vector<std::string> out;
  for (const lsp::CompletionItem& item : list.items) out.push_back(item.label);
  std::sort(out.begin(), out.end());
  return out;
}

[[nodiscard]] lsp::Position at(const cursiv::Document& document,
                               cursiv::TextOffset offset) {
  return cursiv::to_lsp(document.textDocument().positionAt(offset));
}

// The colours are the same colours, byte for byte on the wire.
void the_colours_are_the_same_colours() {
  const World one, two;
  for (const std::string& text : kDocs) {
    const cursiv::Document* const a = one.built(text);
    const cursiv::Document* const b = two.built(text);
    CHECK(a->hasAst());
    CHECK(b->hasAst());
    const lsp::SemanticTokens x =
        one.doc->semanticTokenProvider->semanticHighlight(*a,
                                                          cursiv::kNoCancel);
    const lsp::SemanticTokens y =
        two.doc->semanticTokenProvider->semanticHighlight(*b,
                                                          cursiv::kNoCancel);
    CHECK(!x.data.empty());
    CHECK(x.data == y.data);
  }
}

// What remains pinned is determinism: two worlds built the same way answer
// every caret identically.
void the_two_answers_never_cross() {
  const World one, two;
  std::size_t asked = 0;
  for (const std::string& text : kDocs) {
    const cursiv::Document* const a = one.built(text);
    const cursiv::Document* const b = two.built(text);
    for (cursiv::TextOffset off = 1; off < text.size(); ++off) {
      const std::vector<std::string> x =
          labels(one.doc->completionProvider->getCompletion(*a, at(*a, off),
                                                            cursiv::kNoCancel));
      const std::vector<std::string> y =
          labels(two.doc->completionProvider->getCompletion(*b, at(*b, off),
                                                            cursiv::kNoCancel));
      ++asked;
      CHECK(x == y);
    }
  }
  CHECK(asked > 100);
}

// What a state names is what may be written there: an opened object offers
// its closer and not the value openers, which belong one position further in.
void the_state_names_exactly_what_may_be_written() {
  const World world;
  const std::string object = R"({"a":1})";
  const cursiv::Document* const o = world.built(object);
  const std::vector<std::string> opened =
      labels(world.doc->completionProvider->getCompletion(*o, at(*o, 1),
                                                          cursiv::kNoCancel));
  CHECK(std::find(opened.begin(), opened.end(), "}") != opened.end());
  CHECK(std::find(opened.begin(), opened.end(), "{") == opened.end());
  CHECK(std::find(opened.begin(), opened.end(), "[") == opened.end());

  const std::string array = R"([1,2,3,{"a":"b"},false])";
  const cursiv::Document* const t = world.built(array);
  const std::vector<std::string> closed =
      labels(world.doc->completionProvider->getCompletion(*t, at(*t, 16),
                                                          cursiv::kNoCancel));
  // The object closed at this caret, so what stands open is the array: the
  // closer of a construct already closed is not written here.
  CHECK(std::find(closed.begin(), closed.end(), "}") == closed.end());
  CHECK(std::find(closed.begin(), closed.end(), "]") != closed.end());
}

// A tree the tables did not write states no state. `CstBuilder` is public, so
// a tape reaches the table parser's caret door; every leaf on one reads state
// zero (the start state), and answering from it would offer the entry rule's
// openers at every caret. The door declines, falling back to the walk instead.
void a_tape_is_declined_and_not_answered_from_the_start_state() {
  const std::string text = R"({"a":1})";
  cursiv::Cst tape(text);
  {
    cursiv::CstBuilder builder(tape);
    const cursiv::CstBuilder::Open all = builder.enter();
    builder.leaf(0, 1, cursiv::kNoElement);
    builder.leaf(1, 4, cursiv::kNoElement);
    builder.leaf(4, 5, cursiv::kNoElement);
    builder.leaf(5, 6, cursiv::kNoElement);
    builder.leaf(6, 7, cursiv::kNoElement);
    builder.exit(all, 0, cursiv::kNoElement);
  }
  CHECK(!tape.engine_stamped());
  CHECK(tape.grammar() == nullptr);
  std::vector<cursiv::NextFeature> out;
  const std::unique_ptr<cursiv::Parser> engine =
      fdoc::generated::DocG::create_parser();
  // Answers nothing rather than answering wrong: the offered set stays empty.
  engine->nextFeaturesAt(tape, 5, 5, out);
  CHECK(out.empty());

  // ...and the same door answers a tree the engine did write.
  namespace g = fdoc::generated::DocG;
  cursiv::Cst stream;
  cursiv::lr_parser<g::kTable>.parse(text, stream, nullptr, &g::kDrive);
  CHECK(stream.engine_stamped());
  CHECK(stream.grammar() ==
        nullptr);  // the engine wrote it, no parser finished it
  engine->nextFeaturesAt(stream, 5, 5, out);
  CHECK(!out.empty());
}

// The other half of the same hole: a tape that DOES claim to be an engine's,
// carrying a state no table has a row for. Both stamps the door reads are
// subscripts, so a wild one on either must stop the answer, not index past
// the rows.
void a_stamp_past_the_table_is_declined() {
  const std::string text = R"({"a":1})";
  namespace g = fdoc::generated::DocG;
  // The first state past the DocG table's rows.
  const std::uint32_t wild =
      g::kTable.stamp.make(cursiv::kNoElement, g::kTable.nstates);
  // State 1 is one the DocG table names; a build that renumbers re-pins it.
  const std::uint32_t named = g::kTable.stamp.make(cursiv::kNoElement, 1);
  const std::unique_ptr<cursiv::Parser> engine =
      fdoc::generated::DocG::create_parser();

  // `at` stamps the leaf the caret sits behind, `before` the one before it.
  const auto answers = [&](std::uint32_t at, std::uint32_t before) {
    cursiv::Cst tape(text);
    {
      cursiv::CstBuilder builder(tape);
      builder.bind_stream();
      const cursiv::CstBuilder::Open all = builder.enter();
      builder.leaf(0, 1, before);
      builder.leaf(1, 4, before);
      builder.leaf(4, 5, at);
      builder.leaf(5, 6, at);
      builder.leaf(6, 7, at);
      builder.exit(all, 0, at);
    }
    CHECK(tape.engine_stamped());
    std::vector<cursiv::NextFeature> out;
    engine->nextFeaturesAt(tape, 5, 5, out);
    return out.size();
  };

  // A state the table names answers; the same tape with either stamp past the
  // rows answers nothing at all.
  CHECK(answers(named, named) > 0);
  CHECK(answers(wild, wild) == 0);
  CHECK(answers(wild, named) == 0);
  CHECK(answers(named, wild) == 0);
}

// This storey owes nothing: a language that installs no provider is complete.
void a_language_with_no_provider_owes_no_slot() {
  cursiv::SharedLspServices shared;
  cursiv::install_default_shared_lsp_services(shared);
  cursiv::LspServices language(shared);
  language.languageMetaData = {"doc", {".doc"}, {}};
  language.parser = fdoc::generated::DocG::create_parser();
  language.astReflection = fdoc::generated::DocG::create_ast_reflection();
  cursiv::install_default_core_services(language);
  CHECK(language.hoverProvider == nullptr);
  CHECK(language.firstMissingService().empty());
}

// A grammar that declares no reference keeps no reference tables: every
// member answers as no reference, and no lookup finds one.
void a_grammar_without_references_answers_none() {
  const std::unique_ptr<cursiv::AstReflection> reflection =
      fdoc::generated::DocG::create_ast_reflection();
  CHECK(!reflection->hasMultiReference());
  std::uint16_t fields = 0;
  for (; !reflection->fieldName(fields).empty(); ++fields) {
    CHECK(!reflection->isMultiReference(fields));
    CHECK(reflection->referenceNarrowTarget(fields, 0) == nullptr);
    CHECK(reflection->referenceAccepts(fields).empty());
  }
  CHECK(fields != 0);
  for (const cursiv::AstTypeTag* const type : reflection->getAllTypes()) {
    CHECK(reflection->referenceIndexOf(type) == cursiv::kNoReference);
    for (std::uint16_t f = 0; f < fields; ++f)
      CHECK(reflection->referenceIndexOf(type, reflection->fieldName(f)) ==
            cursiv::kNoReference);
  }
}

}  // namespace

void run() {
  a_language_with_no_provider_owes_no_slot();
  a_grammar_without_references_answers_none();
  the_colours_are_the_same_colours();
  the_two_answers_never_cross();
  the_state_names_exactly_what_may_be_written();
  a_tape_is_declined_and_not_answered_from_the_start_state();
  a_stamp_past_the_table_is_declined();
}

}  // namespace lsp_services

// Pins the one type frontier of the tree: cursiv::Position/Range crossing into
// the protocol's types and back, and a didChange's variant becoming the
// core's own event.
namespace position {

namespace {

void a_position_round_trips() {
  const cursiv::Position zero{0, 0};
  const cursiv::Position big{UINT32_MAX, UINT32_MAX};
  for (const cursiv::Position p : {zero, big}) {
    const cursiv::Position back = cursiv::from_lsp(cursiv::to_lsp(p));
    CHECK(back == p);
  }
  const cursiv::Range r{{1, 2}, {3, 4}};
  const cursiv::Range back = cursiv::from_lsp(cursiv::to_lsp(r));
  CHECK(back == r);
}

// The protocol's whole-document/ranged variant becomes the core's optional
// range, texts copied.
void a_change_event_crosses_the_frontier() {
  std::vector<lsp::TextDocumentContentChangeEvent> changes;
  lsp::TextDocumentContentChangePartial ranged;
  ranged.range = lsp::Range{{1, 2}, {3, 4}};
  ranged.text = "mid";
  changes.emplace_back(std::move(ranged));
  lsp::TextDocumentContentChangeWholeDocument whole;
  whole.text = "all";
  changes.emplace_back(std::move(whole));

  const std::vector<cursiv::TextDocumentContentChangeEvent> converted =
      cursiv::from_lsp(changes);
  CHECK(converted.size() == 2);
  CHECK(converted[0].range.has_value());
  CHECK(converted[0].range->start == cursiv::Position{1, 2});
  CHECK(converted[0].range->end == cursiv::Position{3, 4});
  CHECK(converted[0].text == "mid");
  CHECK(!converted[1].range.has_value());
  CHECK(converted[1].text == "all");
}

}  // namespace

void run() {
  a_position_round_trips();
  a_change_event_crosses_the_frontier();
}

}  // namespace position

// Pins the configuration pull: subscribe-then-fetch (an edit racing the pull
// is not lost), the immediate readiness when there is nothing to pull, the
// clamp against a short answer, the silence of the pull against the
// announcement of a change, and the typed view `validation_categories` hands
// the build path.
namespace configuration_provider {

namespace {

struct World : support::CoreWorkspace {
  cursiv::ValidationRegistry* registry = ws->validationRegistry.get();
};

// Keeps what it is asked, answers when told to.
struct RecordingClient final : cursiv::LanguageClient {
  std::vector<std::string> calls;
  std::function<void(std::vector<lsp::LSPAny>)> answer;
  std::vector<std::string> asked;

  void publishDiagnostics(lsp::PublishDiagnosticsParams) override {}
  void registerCapability(lsp::RegistrationParams) override {
    calls.emplace_back("register");
  }
  void configuration(
      lsp::ConfigurationParams params,
      std::function<void(std::vector<lsp::LSPAny>)> then) override {
    calls.emplace_back("fetch");
    for (const lsp::ConfigurationItem& item : params.items)
      if (item.section.has_value()) asked.push_back(*item.section);
    answer = std::move(then);
  }
};

struct CountingListener final : cursiv::ConfigurationSectionListener {
  std::vector<std::string> sections;
  void onConfigurationSectionUpdate(std::string_view section,
                                    const lsp::LSPAny&) override {
    sections.emplace_back(section);
  }
};

// A section holding one named value.
[[nodiscard]] lsp::LSPAny section_with(std::string_view name,
                                       std::string value) {
  lsp::json::Object section;
  section[name] = lsp::LSPAny(std::move(value));
  return lsp::LSPAny(std::move(section));
}

// A client that pulls `configuration` when told, and accepts the
// `didChangeConfiguration` registration unless told otherwise.
lsp::InitializeParams supporting(
    bool configuration, std::optional<bool> dynamicRegistration = true) {
  lsp::InitializeParams params;
  lsp::WorkspaceClientCapabilities workspace;
  workspace.configuration = configuration;
  if (dynamicRegistration.has_value())
    workspace.didChangeConfiguration.emplace().dynamicRegistration =
        *dynamicRegistration;
  params.capabilities.workspace = std::move(workspace);
  return params;
}

// Subscribe first, fetch second; ready only once the answer lands; the
// section is then readable.
void the_pull_subscribes_before_it_fetches() {
  World world;
  cursiv::DefaultConfigurationProvider provider(world.shared);
  provider.initialize(supporting(true));
  RecordingClient client;
  provider.initialized(&client);

  const std::vector<std::string> expected = {"register", "fetch"};
  CHECK(client.calls == expected);
  CHECK(client.asked == std::vector<std::string>{"workspace"});
  CHECK(!provider.isReady());  // the client has not answered yet
  CHECK(!provider.getConfiguration("workspace", "greeting").has_value());

  std::vector<lsp::LSPAny> values;
  values.emplace_back(section_with("greeting", "hello"));
  client.answer(std::move(values));
  CHECK(provider.isReady());
  const auto greeting = provider.getConfiguration("workspace", "greeting");
  CHECK(greeting.has_value() && greeting->isString() &&
        greeting->string() == "hello");
}

// The pull follows `configuration` and the registration follows its own
// capability: a client that refuses dynamic registration, or never mentions
// it, is still asked for its settings but is never sent the registration.
void the_registration_follows_its_own_capability() {
  for (const std::optional<bool> dynamicRegistration :
       {std::optional<bool>{false}, std::optional<bool>{}}) {
    World world;
    cursiv::DefaultConfigurationProvider provider(world.shared);
    provider.initialize(supporting(true, dynamicRegistration));
    RecordingClient client;
    provider.initialized(&client);
    CHECK(client.calls == std::vector<std::string>{"fetch"});
    CHECK(client.asked == std::vector<std::string>{"workspace"});
    CHECK(!provider.isReady());
    if (client.answer) client.answer({section_with("greeting", "hello")});
    CHECK(provider.isReady());
    const auto greeting = provider.getConfiguration("workspace", "greeting");
    CHECK(greeting.has_value() && greeting->isString() &&
          greeting->string() == "hello");
  }
}

// Nothing to pull is ready at once: unsupported client, and absent client.
void nothing_to_pull_is_ready_at_once() {
  World world;
  cursiv::DefaultConfigurationProvider unsupported(world.shared);
  unsupported.initialize(supporting(false));
  RecordingClient client;
  unsupported.initialized(&client);
  CHECK(unsupported.isReady());
  CHECK(client.calls.empty());  // never asked

  cursiv::DefaultConfigurationProvider sessionless(world.shared);
  sessionless.initialize(supporting(true));
  sessionless.initialized(nullptr);
  CHECK(sessionless.isReady());
}

// A short answer is clamped, never read past.
void a_short_answer_is_clamped() {
  World world;
  cursiv::DefaultConfigurationProvider provider(world.shared);
  provider.initialize(supporting(true));
  RecordingClient client;
  provider.initialized(&client);
  client.answer({});  // the client answered nothing at all
  CHECK(provider.isReady());
  CHECK(!provider.getConfiguration("workspace", "greeting").has_value());
}

void empty_changes_pull_fresh_configuration() {
  for (const std::string_view settings : {"null", "{}"}) {
    World world;
    cursiv::DefaultConfigurationProvider provider(world.shared);
    provider.initialize(supporting(true));
    RecordingClient client;
    provider.initialized(&client);
    client.answer({section_with("state", "initial")});

    lsp::DidChangeConfigurationParams change;
    change.settings = lsp::json::parse(settings);
    provider.updateConfiguration(change);

    const std::vector<std::string> expected = {"register", "fetch", "fetch"};
    CHECK(client.calls == expected);
    CHECK(!provider.isReady());
    CHECK(provider.getConfiguration("workspace", "state")->string() ==
          "initial");

    client.answer({section_with("state", "refreshed")});
    CHECK(provider.isReady());
    CHECK(provider.getConfiguration("workspace", "state")->string() ==
          "refreshed");

    provider.updateConfiguration(change);
    auto stale = client.answer;
    provider.updateConfiguration(change);
    stale({section_with("state", "superseded")});
    CHECK(!provider.isReady());
    CHECK(provider.getConfiguration("workspace", "state")->string() ==
          "refreshed");
    client.answer({lsp::LSPAny(nullptr)});
    CHECK(provider.isReady());
    CHECK(!provider.getConfiguration("workspace", "state").has_value());

    provider.updateConfiguration(change);
    auto detached = client.answer;
    provider.initialized(nullptr);
    detached({section_with("state", "detached")});
    CHECK(!provider.getConfiguration("workspace", "state").has_value());
  }
}

void a_late_pull_preserves_a_newer_change() {
  World world;
  cursiv::DefaultConfigurationProvider provider(world.shared);
  provider.initialize(supporting(true));
  RecordingClient client;
  provider.initialized(&client);

  lsp::DidChangeConfigurationParams change;
  lsp::json::Object settings;
  settings["workspace"] = section_with("state", "changed");
  change.settings = lsp::LSPAny(std::move(settings));
  provider.updateConfiguration(change);
  client.answer({section_with("state", "old")});
  CHECK(provider.isReady());
  CHECK(provider.getConfiguration("workspace", "state")->string() == "changed");
}

void readers_can_observe_configuration_updates() {
  World world;
  cursiv::DefaultConfigurationProvider provider(world.shared);
  const std::string first(512, 'a');
  const std::string second(512, 'b');
  const auto update = [&](const std::string& value) {
    lsp::DidChangeConfigurationParams change;
    lsp::json::Object settings;
    settings["workspace"] = section_with("state", value);
    change.settings = lsp::LSPAny(std::move(settings));
    provider.updateConfiguration(change);
  };
  update(first);
  std::barrier start(2);
  std::atomic<bool> coherent{true};
  std::jthread reader([&] {
    start.arrive_and_wait();
    for (int i = 0; i < 1000; ++i) {
      const auto value = provider.getConfiguration("workspace", "state");
      if (!value || !value->isString() ||
          (value->string() != first && value->string() != second))
        coherent = false;
    }
  });
  start.arrive_and_wait();
  for (int i = 0; i < 1000; ++i) update(i % 2 == 0 ? first : second);
  reader.join();
  CHECK(coherent);
}

void listeners_can_read_the_updated_configuration() {
  World world;
  cursiv::DefaultConfigurationProvider provider(world.shared);
  struct Listener final : cursiv::ConfigurationSectionListener {
    cursiv::ConfigurationProvider& provider;
    bool called = false;
    explicit Listener(cursiv::ConfigurationProvider& value) : provider(value) {}
    void onConfigurationSectionUpdate(std::string_view section,
                                      const lsp::LSPAny&) override {
      called = true;
      CHECK(provider.getConfiguration(section, "state")->string() == "changed");
    }
  } listener(provider);
  provider.addConfigurationListener(listener);
  lsp::DidChangeConfigurationParams change;
  lsp::json::Object settings;
  settings["workspace"] = section_with("state", "changed");
  change.settings = lsp::LSPAny(std::move(settings));
  provider.updateConfiguration(change);
  CHECK(listener.called);
  provider.removeConfigurationListener(listener);
}

// A change replaces the section whole; a non-object `settings` changes
// nothing; a name the section lacks, or a section that is no object, answers
// nothing.
void a_change_replaces_the_section_whole() {
  World world;
  cursiv::DefaultConfigurationProvider provider(world.shared);

  lsp::DidChangeConfigurationParams change;
  lsp::json::Object settings;
  settings["workspace"] = section_with("v", "v1");
  change.settings = lsp::LSPAny(std::move(settings));
  provider.updateConfiguration(change);
  CHECK(provider.getConfiguration("workspace", "v")->string() == "v1");
  CHECK(!provider.getConfiguration("workspace", "w").has_value());

  lsp::DidChangeConfigurationParams noise;
  noise.settings = lsp::LSPAny(nullptr);
  provider.updateConfiguration(noise);
  CHECK(provider.getConfiguration("workspace", "v")->string() == "v1");

  lsp::DidChangeConfigurationParams flat;
  lsp::json::Object scalar;
  scalar["workspace"] = lsp::LSPAny(std::string("v2"));
  flat.settings = lsp::LSPAny(std::move(scalar));
  provider.updateConfiguration(flat);
  CHECK(!provider.getConfiguration("workspace", "v").has_value());
}

// Both pulled and pushed values reach the same listeners.
void pulled_and_pushed_values_are_announced() {
  World world;
  cursiv::DefaultConfigurationProvider provider(world.shared);
  CountingListener listener;
  provider.addConfigurationListener(listener);
  provider.initialize(supporting(true));
  RecordingClient client;
  provider.initialized(&client);
  std::vector<lsp::LSPAny> values;
  values.emplace_back(section_with("state", "pulled"));
  client.answer(std::move(values));
  CHECK(provider.isReady());
  CHECK(provider.getConfiguration("workspace", "state").has_value());
  CHECK(listener.sections == std::vector<std::string>{"workspace"});

  lsp::DidChangeConfigurationParams change;
  lsp::json::Object settings;
  settings["workspace"] = lsp::LSPAny(std::string("changed"));
  change.settings = lsp::LSPAny(std::move(settings));
  provider.updateConfiguration(change);
  CHECK(listener.sections == (std::vector<std::string>{"workspace", "workspace"}));
  provider.removeConfigurationListener(listener);
}

// The typed view: both spellings, built-ins and registry names resolved by
// identity, unknown names dropped, duplicates folded, garbage empty.
void validation_categories_reads_both_spellings() {
  World world;
  const cursiv::ValidationRegistry& registry = *world.registry;

  const auto parse = [&](std::string_view json) {
    return cursiv::validation_categories(lsp::json::parse(std::string(json)),
                                         registry);
  };

  const auto flat = parse(R"(["fast", "slow", "fast"])");
  CHECK(flat.size() == 2);
  CHECK(flat[0] == &cursiv::fast && flat[1] == &cursiv::slow);

  const auto nested = parse(R"({"categories": ["built-in"]})");
  CHECK(nested.size() == 1 && nested[0] == &cursiv::built_in);

  CHECK(parse(R"(["no-such-category"])").empty());
  CHECK(parse(R"({"other": 1})").empty());
  CHECK(parse(R"("not a list")").empty());
}

}  // namespace

void run() {
  the_pull_subscribes_before_it_fetches();
  the_registration_follows_its_own_capability();
  nothing_to_pull_is_ready_at_once();
  a_short_answer_is_clamped();
  empty_changes_pull_fresh_configuration();
  a_late_pull_preserves_a_newer_change();
  readers_can_observe_configuration_updates();
  listeners_can_read_the_updated_configuration();
  a_change_replaces_the_section_whole();
  pulled_and_pushed_values_are_announced();
  validation_categories_reads_both_spellings();
}

}  // namespace configuration_provider

// Two languages configured differently: a keystroke in one relinks a document
// of the other, which is revalidated with its own language's categories.
namespace lsp_document_builder {

namespace {

constexpr std::string_view kA = "file:///w/a.wsa";
constexpr std::string_view kB = "file:///w/b.wsb";

struct World : support::World<cursiv::SharedLspServices> {
  // How many times language b's slow pass ran.
  std::atomic<int> slowRuns{0};

  World() {
    add("wsa", ".wsa");
    add("wsb", ".wsb", [this](cursiv::ValidationRegistry& registry) {
      registry.registerAfterDocument(
          [this](const cursiv::Document&, cursiv::ValidationAcceptor& accept,
                 const cursiv::CancellationToken&) {
            ++slowRuns;
            accept.accept(cursiv::TextRange{0, 0},
                          cursiv::DiagnosticSeverity::Warning, "slow check");
          },
          &cursiv::slow);
    });
    lsp::DidChangeConfigurationParams settings;
    settings.settings = lsp::json::parse(
        R"({"wsa": {"validation": {"categories": ["built-in", "fast"]}},
            "wsb": {"validation": {"categories": ["built-in", "fast", "slow"]}}})");
    shared.configurationProvider->updateConfiguration(settings);
    (void)shared.languageServer->initialize({});
    shared.languageServer->initialized();
  }

  void add(
      std::string id, std::string extension,
      const std::function<void(cursiv::ValidationRegistry&)>& checks = {}) {
    (void)support::add_language<cursiv::LspServices>(
        shared, {id, {extension}, {}},
        ws::generated::Workspace::create_parser(),
        ws::generated::Workspace::create_ast_reflection(),
        [&](cursiv::LspServices& language) {
          if (checks) checks(*language.validationRegistry);
        });
  }


  [[nodiscard]] bool warned(std::string_view uri) const {
    for (const cursiv::Diagnostic& d :
         shared.documents->getDocument(uri)->diagnostics)
      if (d.message == "slow check") return true;
    return false;
  }
};

void a_relinked_document_keeps_its_own_languages_validation() {
  World world;
  support::open(*world.shared.languageServer, kA, "wsa", "datatype T\n");
  support::open(*world.shared.languageServer, kB, "wsb",
                "entity B { x : T }\n");
  CHECK(world.warned(kB));
  const int before = world.slowRuns;
  support::change(*world.shared.languageServer, kA, 2,
                  "datatype T\ndatatype U\n");
  CHECK(world.slowRuns > before);
  CHECK(world.warned(kB));
  CHECK(!world.warned(kA));
}

}  // namespace

void run() {
  a_relinked_document_keeps_its_own_languages_validation();
}

}  // namespace lsp_document_builder

int main() {
  cursiv::test::count_expectations();
  language_server::run();
  language_server_runtime::run();
  language_server_main::run();
  execute_command_handler::run();
  lsp_services::run();
  position::run();
  configuration_provider::run();
  lsp_document_builder::run();
  return cursiv::test::report("server");
}
