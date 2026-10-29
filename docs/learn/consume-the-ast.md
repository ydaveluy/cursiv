# Read the AST from a program

This guide shows how to read the abstract syntax tree (AST) from your own
program, such as a compiler or a code generator. The examples use the Recipe
language from the [tutorial](walkthrough.md).

## Create the language services

First create the services of your language:

```cpp
#include <cstdio>
#include <string>

#include "cursiv/core/parser/Parser.hpp"       // `parser->parse` needs it
#include "cursiv/core/services/CoreLanguage.hpp"
#include "recipe/core/CoreServices.hpp"
#include "recipe/core/ast.hpp"

const auto language = cursiv::create_language<
    recipe::RecipeCoreServices, recipe::RecipeSharedCoreServices,
    recipe::install_core_module>();
```

`language.language` holds the services of Recipe, including its parser.
`language.shared` holds the workspace services, such as the document store and
the document builder.

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

`ast_cast<T>` returns the node when it is a `T` or a class derived from it,
and null otherwise. `result` is false when parsing fails; for a clean parse,
also require `result.diagnostics.empty()`. An AST can exist even with
diagnostics, and `result.cst().recovered()` tells whether the parser repaired
the input. The parser alone does not resolve references or run validation.

!!! warning "The tree borrows the text"

    AST text can point into the input buffer or into the `ParseResult`. Keep
    both alive while you read the tree. A `Document` owns both.

AST lists are `cursiv::vector<T*>`, not `std::vector`; see
[AST vocabulary](../reference/ast.md#vectort-is-not-stdvectort).

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

The URI's extension selects the language. `document->diagnostics` holds the
syntax, reference and validation diagnostics. Read reference targets with
`get()`:

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

`get()` returns null for an unresolved name. A reference with one target type
also supports `->`, `*` and a boolean test. `value_as<Model>()` does not check
the type; it is safe here because Recipe's entry rule returns a `Model`.

To skip validation, pass `BuildOptions` with `validation = false`. See
[Document lifecycle](../reference/services.md#document-lifecycle).

## Several documents

Add the documents to the same workspace and build them together. Here,
`documents` holds `std::shared_ptr<cursiv::Document>` values created with
`fromString`:

```cpp
std::vector<cursiv::Document*> all;
for (auto& d : documents) {
  shared.documents->addDocument(d);
  all.push_back(d.get());
}
shared.documentBuilder->build(all);
```

From the command line, `--root <dir>` loads a folder for cross-file
references.

## Read a whole folder

By default, `create_language()` reads no files from disk. Pass a
`LocalFileSystemProvider`, then let the workspace manager scan a folder URI:

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

`initializeWorkspace` loads the files of your language from the folder and
its subfolders, then builds them all. For tests, `MemoryFileSystemProvider`
supplies files from memory.
