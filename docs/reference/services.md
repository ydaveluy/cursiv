# Services

A service implements one part of a language, such as parsing, validation or
an editor feature. The CLI, the language server and tests use the same
services.

## Containers

Services live in four containers:

| | Core | LSP |
|---|---|---|
| **Shared**: one per workspace | `SharedCoreServices` | `SharedLspServices` |
| **Language**: one per language | `CoreServices` | `LspServices` |

- **Shared services** serve the whole workspace and every language in it:
  documents, index, document builder.
- **Language services** belong to one language: parser, scoping, validation,
  editor providers. They reach the shared services through `services.shared()`.
- **Core services** are what the CLI, tests and programs need. **LSP
  services** add the editor features. An LSP container extends the core one,
  so a language server has both.

The scaffold derives the four in `core/CoreServices.hpp`
(`MyLangSharedCoreServices`, `MyLangCoreServices`) and `lsp/LspServices.hpp`
(`MyLangSharedServices`, `MyLangServices`).

## Replace a service

The framework installs the default services, then calls your module
functions. Assign a member there to replace its default. Replace language
services in `install_core_module`:

```cpp title="core/CoreModule.cpp"
void install_core_module(MyLangCoreServices& services) {
  mylang::generated::MyLang::install_generated_module(services);
  services.scopeProvider = std::make_unique<MyScopeProvider>(services);
  register_validation_checks(services);
}
```

Replace editor providers the same way in `install_lsp_module`, after
`cursiv::install_default_lsp_services(services)`.

Replace shared services in the function that registers your languages
(`register_lsp_services` in the scaffold). It runs after the shared defaults
are installed:

```cpp title="lsp/LspModule.cpp"
bool register_lsp_services(cursiv::SharedCoreServices& sharedServices) {
  auto& shared = dynamic_cast<MyLangSharedServices&>(sharedServices);
  shared.executeCommandHandler = std::make_unique<MyCommandHandler>();
  // ...then create and register the language services, as the scaffold does.
}
```

Recipes show complete examples: [scoping](../recipes/scoping.md),
[case-insensitive names](../recipes/case-insensitive.md) (a `MyScopeProvider`),
[validation](../recipes/validation.md) and [formatting](../recipes/formatting.md).

## Service list

The **Default** column says what provides a service when you do not: the
framework, the module generated from your grammar, or nothing.

### Shared core services

| Service | Purpose | Default |
|---|---|---|
| `documents` | Hold the documents of the workspace | Framework |
| `documentFactory` | Create a document from a URI or a text | Framework |
| `documentBuilder` | Build documents through the [lifecycle](#document-lifecycle), and rebuild them after a change | Framework |
| `indexManager` | Index the exported symbols and references of every document | Framework |
| `serviceRegistry` | Hold the registered languages and find a document's language | Framework |
| `workspaceManager` | Load the documents of the workspace folders | Framework |
| `fileSystemProvider` | Read files and folders | An empty file system; the CLI and the language server read the disk |
| `workspaceLock` | Run workspace updates one at a time, and let requests read the workspace between them | Framework |
| `taskScheduler` | Run builds in parallel; `CURSIV_WORKERS` sets the number of worker threads | Framework |
| `observabilitySink` | Report problems that are not document diagnostics, such as an unreadable file or a check that threw | Standard error |

### Language core services

| Service | Purpose | Default |
|---|---|---|
| `languageMetaData` | Give the language id and file extensions | Generated |
| `parser` | Parse a document's text | Generated |
| `astReflection` | Describe the AST types | Generated |
| `grammarSource` | Give the grammar's text to tools such as MCP | Generated |
| `nameProvider` | Give the name of an AST node | Framework |
| `scopeComputation` | Collect exported and local symbols | Framework |
| `scopeProvider` | Find symbols visible from a reference | Framework |
| `linker` | Resolve references | Framework |
| `references` | Find the references to a declaration | Framework |
| `astNodeLocator` | Give a node's path in its document, and the node at a path | Framework |
| `astNodeDescriptionProvider` | Describe an exported node for the index | Framework |
| `referenceDescriptionProvider` | Describe a reference for the index | Framework |
| `documentationProvider` | Give the documentation shown by hover and completion | Framework |
| `commentProvider` | Find the comment written above a node, by default in a hidden terminal named `ML_COMMENT` | Framework |
| `parserErrorMessageProvider` | Word the syntax errors of a document | Framework |
| `validationRegistry` | Register checks for AST types | Framework |
| `documentValidator` | Report a document's parse errors and run its checks | Framework |

### Shared LSP services

| Service | Purpose | Default |
|---|---|---|
| `languageServer` | Handle the client's requests | Framework |
| `languageClient` | Send messages to the client | Set during a session |
| `configurationProvider` | Read the client's settings | Framework |
| `workspaceSymbolProvider` | Search symbols across the workspace | Framework |
| `nodeKindProvider` | Give the symbol and completion kind of a node | Framework |
| `fuzzyMatcher` | Match typed text against names, for completion and symbol search | Framework |
| `mcpService` | Serve the [MCP tools](mcp.md) | Installed by the language server |
| `executeCommandHandler` | Run the commands a client requests | None |
| `fileOperationHandler` | React when files are created, renamed or deleted | None |
| `documentSaveHandler` | React when the client saves a document, see [Saves and notebooks](#saves-and-notebooks) | None |
| `notebookDocuments` | Sync notebooks, whose cells become documents, see [Saves and notebooks](#saves-and-notebooks) | None |
| `textDocumentContentProvider` | Give the text of virtual documents | None |

### Language LSP services

The server advertises an editor feature only when its provider is installed.
It calls a provider once the document has reached the state in the
**Called at** column, or once every document has, where it says *workspace*.
See [Document lifecycle](#document-lifecycle).

A request sees every edit sent before it. After `didOpen`, `didChange`,
`didClose`, `didChangeWatchedFiles`, or a notebook's `didOpen`, `didChange` or
`didClose`, it waits until the change is applied and the state in **Called at**
is reached, not for the rest of the build, such as validation. After any other
notification, it waits until that notification is handled. A provider runs
beside the rest of the build, so one called before `Validated` must not read
diagnostics.

| Service | Feature | Called at | Default |
|---|---|---|---|
| `completionProvider` | Completion | `Linked` | Framework |
| `hoverProvider` | Hover | `Linked` | Framework |
| `definitionProvider` | Go to definition | `Linked` | Framework |
| `referencesProvider` | Find references | `IndexedReferences`, workspace | Framework |
| `documentHighlightProvider` | Highlight related occurrences | `IndexedReferences`, workspace | Framework |
| `renameProvider` | Rename | `IndexedReferences`, workspace | Framework |
| `documentSymbolProvider` | Document outline | `Parsed` | Framework |
| `foldingRangeProvider` | Folding | `Parsed` | Framework |
| `selectionRangeProvider` | Expand selection | `Parsed` | Framework |
| `semanticTokenProvider` | Semantic highlighting, see [Customize semantic tokens](#customize-semantic-tokens) | `Linked` | None |
| `codeActionProvider` | Quick fixes | `Validated` | Framework |
| `formatter` | Formatting rules, see [Formatting](../recipes/formatting.md) | `Parsed` | None |
| `declarationProvider` | Go to declaration | `Linked` | None |
| `typeDefinitionProvider` | Go to type definition | `Linked` | None |
| `implementationProvider` | Go to implementation | `IndexedReferences`, workspace | None |
| `callHierarchyProvider` | Call hierarchy | `IndexedReferences`, workspace | None |
| `typeHierarchyProvider` | Type hierarchy | `IndexedReferences`, workspace | None |
| `signatureHelpProvider` | Signature help | `IndexedReferences` | None |
| `inlayHintProvider` | Inlay hints | `IndexedReferences` | None |
| `documentLinkProvider` | Document links | `Parsed` | None |
| `codeLensProvider` | Code lenses | `IndexedReferences` | None |

## Customize completion

Derive from `DefaultCompletionProvider` and override what your language changes:

| Hook | Decides |
|---|---|
| `completionFor` | What one possible element offers |
| `completionForCrossReference` | The items of a cross-reference |
| `completionForKeyword` | The item of a keyword |
| `visitReferenceCandidates` | Which targets a cross-reference proposes |
| `createReferenceCompletionItem` | How a target is shown |
| `getReferenceDocumentation` | The documentation of a target |
| `filterKeyword` | Whether a keyword is offered; by default those with a letter |
| `continueCompletion` | Whether to try the next way of reading the caret |
| `deduplicateItems` | Which items are kept |
| `fillCompletionItem`, `buildCompletionTextEdit` | How a value becomes an item and what it replaces |
| `buildContexts`, `findFeaturesAt`, `findDataTypeRuleStart`, `backtrackToAnyToken`, `performCurrentTokenCompletion`, `performNextTokenCompletion` | How the caret is read |

`getCompletion` receives the request's `CompletionParams`. Every hook that
takes a `context` reads how the client asked in `context.trigger`: its
`triggerKind`, and the `triggerCharacter` typed. It is empty when the client
does not say.

Pass a value to `accept(context, value)`. A value with no `textEdit` must match
what is typed. It gets its label from `label`, `node` or `nodeDescription`.
`context.node()` gives the node at the caret, or null at the very start of a
document. Where the caret opens the rule that writes a reference, or the reference
belongs to a class the node at the caret is not, a scope asked for its targets
sees a new node of that class as `container`, with the node at the caret as its
parent and its own members empty. It lasts only for the request, so do not keep
it.

Keywords are always offered. Names stop once an answer holds 1000 items,
keywords included: what the author types next asks again. A terminal offers nothing, whatever it reads. To offer a
choice of words, write it as a data rule. A terminal with `as<V>` on each
alternative is one. Where a [condition](grammar.md#conditions) reads the next
token's text, a terminal such as `ID` is expected when the condition accepts
one of a few sample words, such as `a`, `name`, `0` or `_`.

## Customize semantic tokens

`GrammarSemanticTokenProvider` colours keywords, operators, numbers, strings,
comments, declarations and references from the grammar, with no modifier. To
colour differently, derive from it or from `AbstractSemanticTokenProvider` and
override:

| Hook | Decides |
|---|---|
| `computeHighlighting` | The tokens of a document: each passed to `accept` with its text offsets, type and modifiers |
| `tokenTypes`, `tokenModifiers` | The names the token types and modifiers stand for; by default the protocol's standard ones, `kSemanticTokenTypes` and `kSemanticTokenModifiers` |
| `semanticTokensOptions` | The legend and requests the server announces |

A token's `type` is an index into `tokenTypes()` and its `modifiers` hold one
bit per index of `tokenModifiers()`: `tokenType("keyword")` and
`tokenModifier("readonly")` give them by name. Tokens may come in any order;
where two overlap, the one that starts first wins.

```cpp
// Marks every declaration the grammar colours as `type` or `property`.
class MyTokens : public cursiv::GrammarSemanticTokenProvider {
 public:
  using GrammarSemanticTokenProvider::GrammarSemanticTokenProvider;

 protected:
  void computeHighlighting(const cursiv::Document& document,
                           cursiv::TextOffset begin, cursiv::TextOffset end,
                           cursiv::SemanticTokenAcceptor accept,
                           const cursiv::CancellationToken& token)
      const override {
    const auto declared = tokenModifier("declaration");
    const auto type = tokenType("type");
    const auto property = tokenType("property");
    GrammarSemanticTokenProvider::computeHighlighting(
        document, begin, end,
        [&](cursiv::SemanticToken coloured) {
          if (coloured.type == type || coloured.type == property)
            coloured.modifiers |= declared;
          accept(coloured);
        },
        token);
  }
};
```

The server announces one legend for all its languages. Keep the standard names
at their index and add your own after them: `initialize` fails if two languages
give one index different names.

Every answer is computed for the whole document, so a delta saves only the
transfer, and none is announced by default. To announce it, set `full` to
`SemanticTokensFullDelta{.delta = true}` in `semanticTokensOptions`. Each open
document then keeps its last answer, and the client receives only the changes
from it. The server announces a delta only if every language does.

## Saves and notebooks

To learn of saves, install a `documentSaveHandler` among the shared services.
Its `saveOptions()` says which notifications the client sends, and only those
are announced:

| Option | Hook | Called |
|---|---|---|
| `willSave` | `willSaveDocument` | Before the save, with no lock held |
| `willSaveWaitUntil` | `willSaveDocumentWaitUntil` | Before the save, for an open document, under the read lock. The client applies the returned edits, then saves |
| `didSave` | `didSaveDocument` | After the save, with no lock held |

To sync notebooks, install `notebookDocuments` with the notebook types and cell
languages the client sends:

```cpp
lsp::NotebookDocumentSyncOptions options;
lsp::NotebookDocumentFilterWithNotebook filter;
filter.notebook = std::string("mylang-notebook");
filter.cells = lsp::Array<lsp::NotebookCellLanguage>{{.language = "mylang"}};
options.notebookSelector.push_back(std::move(filter));
shared.notebookDocuments =
    std::make_unique<cursiv::DefaultNotebookDocuments>(std::move(options));
```

Each cell is then an open document of its language: it is built and validated,
answers requests like any other document, and leaves when its notebook closes.
`getNotebookDocument`, `getNotebookCell` and `findNotebookDocumentForCell`
give a notebook and its cells as the client last sent them. To react to a
notebook notification, derive from `DefaultNotebookDocuments` and override
`didOpen`, `didChange`, `didSave` or `didClose`, calling its version first.
They run before the cells change.

## Add your own protocol methods

Register methods the protocol does not define before the session starts:

```cpp
shared.languageServer->onRequest(
    "mylang/generate",
    [&](const lsp::LSPAny& param, const cursiv::CancellationToken& token)
        -> lsp::LSPAny { return generate(param, token); });
shared.languageServer->onNotification(
    "mylang/refresh", [&](const lsp::LSPAny& param) { refresh(param); });
```

A handler receives the value the client sent, or null if it sent none. Requests
can be cancelled, and notifications run in the order they arrive. A request sent
after your notification waits until its handler returns.

To write to the client, call `shared.languageClient->sendNotification` or
`sendRequest` during a session.

To follow workspace folders, derive from `DefaultLanguageServer`, override
`didChangeWorkspaceFolders`, and set `changeNotifications` on the
`workspaceFolders` capability in `initialize`.

## Translate the text before parsing

Some languages rewrite their text before reading tokens: Java decodes
`\u0061` escapes, C joins a line ending in a backslash to the next. Write a
translator and wrap the parser with it:

```cpp title="core/CoreModule.cpp"
// A backslash before a line break joins the two lines.
void join_lines(std::string_view source, cursiv::SourceTranslation& out) {
  for (auto at = source.find("\\\n"); at != std::string_view::npos;
       at = source.find("\\\n", at + 2))
    out.replace(at, at + 2, "");
}

void install_core_module(MyLangCoreServices& services) {
  mylang::generated::MyLang::install_generated_module(services);
  services.parser =
      cursiv::translating_parser(std::move(services.parser), join_lines);
}
```

Call `replace(begin, end, spelling)` in source order, without overlaps; the
text between calls is kept. `out.error(begin, end, message)` reports a
malformed piece. A document the translator leaves untouched is parsed as it
is. Put all the rewrites in one translator: `translating_parser` throws
`std::invalid_argument` for a parser that already translates.

The parser reads the translated text, so AST values hold the decoded text
(`f\u006fo` gives `foo`). Positions in the syntax tree, diagnostics,
completion and the other editor features still refer to the original
document.

## Use a language from a program

`create_language` creates a workspace with your language registered:

```cpp
#include "cursiv/services/CoreLanguage.hpp"
#include "mylang/CoreServices.hpp"

const auto language = cursiv::create_language<
    mylang::MyLangCoreServices, mylang::MyLangSharedCoreServices,
    mylang::install_core_module>();
```

`language.language` gives the language services and `*language.shared` the
shared services. Keep the returned object alive while you use them. See
[Read the AST from a program](../learn/consume-the-ast.md) and
[Several languages](../recipes/multiple-languages.md).

A loaded language does not expose the structure of its rules: `grammarSource`
gives the grammar's text, its rule names and each rule's text, and the syntax
tree tells which rule, member and terminal each node reads.

## Document lifecycle

A new or edited document is `Changed`. The `documentBuilder` moves it
through these states, in order:

| State | Available result |
|---|---|
| `Parsed` | The CST and AST |
| `IndexedContent` | Exported symbols in the workspace index |
| `ComputedScopes` | Local symbols; a reference resolves when you read it |
| `Linked` | Resolved references or linking errors |
| `IndexedReferences` | Reference information for navigation and dependency tracking |
| `Validated` | Built-in diagnostics and registered validation checks |

A build stops at `IndexedReferences` with `BuildOptions{.validation = false}`.
After an edit, `documentBuilder->update` rebuilds the changed documents and
relinks the documents that depend on them. Each `Document` stays the same
object, but the trees of a changed document are replaced.

To customize scopes, override `collectExportedSymbols` (symbols other
documents can see) and `collectLocalSymbols` (symbols visible inside the
document) in a custom `scopeComputation`. They run when the document is parsed,
not when a document it depends on changes, so read only the document itself and
do not resolve references: the scopes of other documents may not be ready yet.
The default `scopeProvider` then looks for a reference's candidates from the
nearest enclosing container outward, then in the global index. The linker and
completion both use these candidates. A reference with none is reported with
the message of the linker's `createLinkingError`: override it to reword the
message, as in [Reword the framework's errors](../recipes/validation.md#reword-the-frameworks-errors).

## Advanced: add a service

Add a member to the scaffold's `MyLangCoreServices` and initialize it in
`install_core_module`, after the services it depends on:

```cpp
// In MyLangCoreServices:
std::unique_ptr<const MyAnalysis> analysis;

// In install_core_module:
services.analysis = std::make_unique<const MyAnalysis>(services);
```

Pass the services container to the constructor and read other services
through it, so that your service uses the ones you replaced. A validator also
registers its methods with `validationRegistry`; see
[Custom validation](../recipes/validation.md).
