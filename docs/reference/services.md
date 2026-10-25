# Services

A service implements one part of a language: parsing, resolving names,
validation or an editor feature. The CLI, server and tests use the same
services.

## Shared and language services

The scaffold declares the service containers in `core/CoreServices.hpp` and
`lsp/LspServices.hpp`:

- **Shared services** belong to the workspace: documents, the symbol index,
  the document builder and the language registry.
- **Language services** belong to one language: its parser, name resolution,
  validation and editor providers.

A workspace can register several languages. Each language keeps its own
services while sharing the documents and index.

## Replace a service

In your existing `install_core_module` function, install custom services after
the generated module. For example, once you have defined `MyScopeProvider`,
replace the default name-lookup service:

`core/CoreModule.cpp`:

```cpp
void install_core_module(MyLangCoreServices& services) {
  mylang::generated::MyLang::install_generated_module(services);
  services.scopeProvider = std::make_unique<MyScopeProvider>(services);
  register_validation_checks(services);
}
```

The framework installs its defaults before calling this function. Assigning a
new implementation to a member such as `scopeProvider` replaces that default.
The [case-insensitive names recipe](../recipes/case-insensitive.md) shows a
complete `MyScopeProvider` class.

Common customization points are:

| Service | Purpose |
|---|---|
| `nameProvider` | Name an AST node |
| `scopeComputation` | Collect exported and local symbols |
| `scopeProvider` | Find symbols visible from a reference |
| `validationRegistry` | Register checks for AST types |
| `documentationProvider` | Supply documentation for hover |
| `formatter` | Define formatting rules; belongs to LSP services |

See the recipes for [scoping](../recipes/scoping.md),
[validation](../recipes/validation.md) and [formatting](../recipes/formatting.md).

## Use a language from a program

`create_language` creates a workspace, installs the language's services and
registers it:

```cpp
#include "cursiv/core/services/CoreLanguage.hpp"
#include "mylang/core/CoreServices.hpp"

const auto language = cursiv::create_language<
    mylang::MyLangCoreServices, mylang::MyLangSharedCoreServices,
    mylang::install_core_module>();
```

Use `language.language` for language services and `*language.shared` for the
workspace. The returned object owns both, so keep it alive while using them.
See [Read the AST from a program](../learn/consume-the-ast.md) for parsing and
building documents, or [Several languages](../recipes/multiple-languages.md)
to register languages in the same workspace.

## Customize editor features

In `install_lsp_module`, assign your provider after installing the defaults:

`lsp/LspModule.cpp`:

```cpp
void install_lsp_module(MyLangServices& services) {
  cursiv::install_default_lsp_services(services);
  services.formatter = std::make_unique<MyLangFormatter>(services);
}
```

This assumes `MyLangFormatter` is your formatter class; the
[formatting recipe](../recipes/formatting.md) provides an implementation.

Defaults include completion, hover, definition, references, rename, symbols,
folding, selection ranges, semantic tokens and diagnostic quick fixes.
Call/type hierarchies, signature help and inlay hints require language-specific
providers. Formatting is advertised only after a formatter is installed.

## Document lifecycle

`DocumentBuilder` parses documents, collects symbols, resolves references and
runs validation. Its public states follow this order:

```text
Changed → Parsed → IndexedContent → ComputedScopes
        → Linked → IndexedReferences → Validated
```

| State | Available result |
|---|---|
| `Parsed` | The CST and AST |
| `IndexedContent` | Exported symbols in the workspace index |
| `ComputedScopes` | Local symbols for each document |
| `Linked` | Resolved references or linking errors |
| `IndexedReferences` | Reference information for navigation and dependency tracking |
| `Validated` | Built-in diagnostics and registered validation checks |

When customizing scopes, use `collectExportedSymbols` for the global index and
`collectLocalSymbols` for visibility inside a document. Do not resolve
references in either method: all documents' scopes must be ready first.

The `ScopeProvider` then supplies visible candidates, and the linker selects
a target. The default provider searches enclosing containers before the global
index. Nearer compatible declarations take priority. Completion uses the same
candidates.

References can resolve lazily from `ComputedScopes`; the linking phase also
resolves them eagerly. A build validates by default. With
`BuildOptions{.validation = false}`, a newly built document stops at
`IndexedReferences`.

After a source edit, `DocumentBuilder.update` rebuilds the changed document
and updates affected references. The document keeps its identity, but its
trees and derived data may be replaced.

## Advanced: add a service

Define your service class, then add a member to the scaffold's
`MyLangCoreServices`. For example, for an analysis class named `MyAnalysis`:

```cpp
std::unique_ptr<const MyAnalysis> analysis;
```

Initialize it in `install_core_module`, after the services it depends on:

```cpp
services.analysis = std::make_unique<const MyAnalysis>(services);
```

A service can receive the language's services container in its constructor
and access other services through it. Read dependencies through that container
so any customized implementations are used. The container owns the service.
For a validator, also register its methods with `validationRegistry`, as
shown in [Custom validation](../recipes/validation.md).
