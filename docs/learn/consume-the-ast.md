# Read the AST from a program

Read the abstract syntax tree (AST) from your own compiler, code generator
or validator. These examples use the Recipe language from the
[tutorial](walkthrough.md).

## Create the language services

```cpp
#include "cursiv/core/parser/Parser.hpp"       // `parser->parse` needs it
#include "cursiv/core/services/CoreLanguage.hpp"
#include "recipe/core/CoreServices.hpp"
#include "recipe/core/ast.hpp"

const auto language = cursiv::create_language<
    recipe::RecipeCoreServices, recipe::RecipeSharedCoreServices,
    recipe::install_core_module>();
```

`language.language` provides the parser and other Recipe services.
`language.shared` manages documents and names across the workspace.

## Parse only

Call the parser directly when you only need the syntax tree:

```cpp
const std::string text = "unit gram\nrecipe Pancakes {}\n";
const cursiv::ParseResult result = language.language.parser->parse(text);

if (const auto* model = cursiv::ast_cast<recipe::ast::Model>(result.value)) {
  for (const recipe::ast::Recipe* const r : model->recipes)
    if (r != nullptr)
      std::printf("%.*s\n", int(r->name.size()), r->name.data());
}
```

The snippet uses `<string>` and `<cstdio>`. `ast_cast<T>` returns a pointer
when the node has the requested type or a derived type, and null otherwise.

Check `result` for parsing or AST construction failure. For a clean parse,
also require `result.diagnostics.empty()`. Calling the parser alone does not
resolve references or run your validation checks.

`result.cst().recovered()` reports syntax repair. An AST may be available even
when syntax or conversion diagnostics are present.

!!! warning "The tree borrows the text"

    AST text can refer to the input buffer or to memory owned by the
    `ParseResult`. Keep both alive while reading the tree. A `Document`
    owns its source text and parse result together.

!!! note "A list member is not a `std::vector`"

    AST lists use `cursiv::vector<T*>` for nodes. See
    [AST vocabulary](../reference/ast.md#vectort-is-not-stdvectort) for the
    available operations.

## Parse, link and validate

To resolve references and run validation, add a document to the workspace
and build it:

```cpp
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/Documents.hpp"

auto& shared = *language.shared;

std::shared_ptr<cursiv::Document> document =
    shared.documentFactory->fromString(
        "unit gram\ningredient Flour in gram\nrecipe Pancakes { use Flour 200 }\n",
        "file:///a.recipe");
shared.documents->addDocument(document);

cursiv::Document* batch[] = {document.get()};
shared.documentBuilder->build(batch);
```

Read syntax, reference and validation diagnostics from `document->diagnostics`.
Access reference targets with `get()`:

```cpp
if (document->hasAst()) {
  const auto* model = document->parseResult.value_as<recipe::ast::Model>();
  for (const recipe::ast::Recipe* r : model->recipes) {
    if (r == nullptr) continue;
    for (const recipe::ast::Use* use : r->uses) {
      if (use == nullptr) continue;
      if (const auto* target = use->ingredient.get())
        std::printf("%.*s\n", int(target->name.size()), target->name.data());
    }
  }
}
```

`get()` returns null for an unresolved name. For a reference with one target
type, you can also use `operator->`, `operator*` and a boolean test.

`value_as<Model>()` is appropriate here because Recipe's entry rule returns
a `Model`; it does not check the type.

The URI selects the language: `file:///a.recipe` matches Recipe's configured
extension. Validation is enabled by default; pass `BuildOptions` with
`validation = false` to skip it. See
[Document lifecycle](../reference/services.md#document-lifecycle).

## Several documents

Add the documents to the same workspace and build them together:

```cpp
std::vector<cursiv::Document*> all;
for (auto& d : documents) {
  shared.documents->addDocument(d);
  all.push_back(d.get());
}
shared.documentBuilder->build(all);
```

From the command line, use `--root <dir>` to load a folder for cross-file
references.

## Reading a whole folder

`create_language()` installs an `EmptyFileSystemProvider` by default. To read
files from disk, pass a `LocalFileSystemProvider` and ask the workspace manager
to scan a folder URI:

```cpp
#include "cursiv/core/workspace/LocalFileSystemProvider.hpp"
#include "cursiv/core/workspace/WorkspaceManager.hpp"

const auto language = cursiv::create_language<
    recipe::RecipeCoreServices, recipe::RecipeSharedCoreServices,
    recipe::install_core_module>(
    std::make_unique<cursiv::LocalFileSystemProvider>());

const cursiv::WorkspaceFolder folders[] = {{"file:///path/to/project", "project"}};
language.shared->workspaceManager->initializeWorkspace(folders);
```

For tests, `MemoryFileSystemProvider` lets you supply files in memory.
