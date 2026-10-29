# Services

A service implements one part of a language, such as parsing, validation or
an editor feature; the CLI, the language server and tests share them.
**Shared services** (documents, symbol index, document builder, language
registry) belong to the workspace, which several languages can share.
**Language services** (parser, name resolution, validation, editor providers)
belong to one language. The scaffold declares both in `core/CoreServices.hpp`
and `lsp/LspServices.hpp`.

## Replace a service

The framework installs default services, then calls your
`install_core_module`, where assigning a member replaces its default:

```cpp title="core/CoreModule.cpp"
void install_core_module(MyLangCoreServices& services) {
  mylang::generated::MyLang::install_generated_module(services);
  services.scopeProvider = std::make_unique<MyScopeProvider>(services);
  register_validation_checks(services);
}
```

Editor providers are replaced the same way in `install_lsp_module`, after
`cursiv::install_default_lsp_services(services)`.

| Service | Purpose |
|---|---|
| `nameProvider` | Name an AST node |
| `scopeComputation` | Collect exported and local symbols |
| `scopeProvider` | Find symbols visible from a reference |
| `validationRegistry` | Register checks for AST types |
| `documentationProvider` | Supply documentation for hover |
| `formatter` | Define formatting rules (LSP services) |

Recipes show complete examples: [scoping](../recipes/scoping.md),
[case-insensitive names](../recipes/case-insensitive.md) (a `MyScopeProvider`),
[validation](../recipes/validation.md) and [formatting](../recipes/formatting.md).

The default editor providers cover completion, hover, definition, references,
rename, symbols, folding, selection ranges, semantic tokens and quick fixes.
Call and type hierarchies, signature help, inlay hints and formatting need a
provider of your own; formatting is advertised only once a formatter is
installed.

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

Call `replace(begin, end, spelling)` in source order; the text between calls
is kept. `out.error(begin, end, message)` reports a malformed piece. A
document the translator leaves untouched is parsed as it is. A language
composes all its phases in one translator: `translating_parser` refuses a
parser that already translates.

The parser reads the translated text, so AST values are decoded (`f\u006fo`
reads `foo`). Syntax-tree spans, diagnostics, completion and the other editor
features keep the document's positions.

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
workspace. Keep the returned object alive while using them. See
[Read the AST from a program](../learn/consume-the-ast.md) and
[Several languages](../recipes/multiple-languages.md).

## Document lifecycle

`DocumentBuilder` builds each document from `Changed` through these states,
in order:

| State | Available result |
|---|---|
| `Parsed` | The CST and AST |
| `IndexedContent` | Exported symbols in the workspace index |
| `ComputedScopes` | Local symbols; references can now resolve on access |
| `Linked` | Resolved references or linking errors |
| `IndexedReferences` | Reference information for navigation and dependency tracking |
| `Validated` | Built-in diagnostics and registered validation checks |

A build stops at `IndexedReferences` with `BuildOptions{.validation = false}`.
After an edit, `DocumentBuilder.update` rebuilds the changed document and the
references it affects; the `Document` stays the same object, but its trees may
be replaced.

To customize scopes, collect workspace-wide symbols in
`collectExportedSymbols` and document-local ones in `collectLocalSymbols`.
Never resolve references in these methods: other documents' scopes may not be
ready. The default `ScopeProvider` then lists the candidates visible from a
reference, from the nearest enclosing container outward, then the global
index; the linker and completion both use them.

## Advanced: add a service

Add a member to the scaffold's `MyLangCoreServices` and initialize it in
`install_core_module`, after the services it depends on:

```cpp
// In MyLangCoreServices:
std::unique_ptr<const MyAnalysis> analysis;

// In install_core_module:
services.analysis = std::make_unique<const MyAnalysis>(services);
```

Pass the services container to the constructor and read other services through
it, so customized implementations are used. A validator also registers its
methods with `validationRegistry`; see [Custom validation](../recipes/validation.md).
