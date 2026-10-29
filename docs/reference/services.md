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
  editor providers. They reach the shared services through `services.shared`.
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
| `textDocumentContentProvider` | Give the text of virtual documents | None |

### Language LSP services

The server advertises an editor feature only when its provider is installed.

| Service | Feature | Default |
|---|---|---|
| `completionProvider` | Completion | Framework |
| `hoverProvider` | Hover | Framework |
| `definitionProvider` | Go to definition | Framework |
| `referencesProvider` | Find references | Framework |
| `documentHighlightProvider` | Highlight related occurrences | Framework |
| `renameProvider` | Rename | Framework |
| `documentSymbolProvider` | Document outline | Framework |
| `foldingRangeProvider` | Folding | Framework |
| `selectionRangeProvider` | Expand selection | Framework |
| `semanticTokenProvider` | Semantic highlighting | Framework |
| `codeActionProvider` | Quick fixes | Framework |
| `formatter` | Formatting rules, see [Formatting](../recipes/formatting.md) | None |
| `declarationProvider` | Go to declaration | None |
| `typeDefinitionProvider` | Go to type definition | None |
| `implementationProvider` | Go to implementation | None |
| `callHierarchyProvider` | Call hierarchy | None |
| `typeHierarchyProvider` | Type hierarchy | None |
| `signatureHelpProvider` | Signature help | None |
| `inlayHintProvider` | Inlay hints | None |
| `documentLinkProvider` | Document links | None |
| `codeLensProvider` | Code lenses | None |

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
#include "cursiv/core/services/CoreLanguage.hpp"
#include "mylang/core/CoreServices.hpp"

const auto language = cursiv::create_language<
    mylang::MyLangCoreServices, mylang::MyLangSharedCoreServices,
    mylang::install_core_module>();
```

`language.language` gives the language services and `*language.shared` the
shared services. Keep the returned object alive while you use them. See
[Read the AST from a program](../learn/consume-the-ast.md) and
[Several languages](../recipes/multiple-languages.md).

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
the documents that depend on them. Each `Document` stays the same object, but
its trees may be replaced.

To customize scopes, override `collectExportedSymbols` (symbols other
documents can see) and `collectLocalSymbols` (symbols visible inside the
document) in a custom `scopeComputation`. Do not resolve references in these
methods: the scopes of other documents may not be ready yet. The default
`scopeProvider` then looks for a reference's candidates from the nearest
enclosing container outward, then in the global index. The linker and
completion both use these candidates.

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
