# Formatting

A formatter defines spacing, line breaks and indentation around syntax
elements. Cursiv turns those rules into editor text edits, for a whole document
or a selected range.

## Format a recipe

This example uses the AST from the [language tutorial](../learn/walkthrough.md).
Each callback receives a `FormattingBuilder` and the node to format:

`src/recipe/lsp/RecipeFormatter.hpp`:

```cpp
#pragma once

#include "cursiv/lsp/formatting/AbstractFormatter.hpp"
#include "recipe/core/ast.hpp"

namespace recipe {

class RecipeFormatter final : public cursiv::AbstractFormatter {
 public:
  explicit RecipeFormatter(const cursiv::CoreServices& services)
      : cursiv::AbstractFormatter(services) {
    on<ast::Recipe>(&RecipeFormatter::formatRecipe);
    on<ast::Use>(&RecipeFormatter::formatUse);
  }

 private:
  void formatRecipe(cursiv::FormattingBuilder& builder,
                    const ast::Recipe* node) const {
    auto formatter = builder.getNodeFormatter(node);
    formatter.keyword("recipe").append(oneSpace);
    auto open = formatter.keyword("{");
    auto close = formatter.keyword("}");
    formatBlock(open, close, formatter.interior(open, close));
  }

  void formatUse(cursiv::FormattingBuilder& builder,
                 const ast::Use* node) const {
    auto formatter = builder.getNodeFormatter(node);
    formatter.keyword("use").append(oneSpace);
    formatter.property<&ast::Use::ingredient>().append(oneSpace);
  }
};

}  // namespace recipe
```

`on<T>` registers a callback for that exact AST class. A node formatter selects
**regions**: a keyword, a property, a node, or the content between delimiters.
`prepend`, `append` and `surround` apply actions around a region:

| Action | Effect |
|---|---|
| `noSpace` | Remove the gap |
| `oneSpace` | Use one space |
| `newLine` | Insert a line break |
| `indent` | Insert a line break and increase indentation |
| `keep` | Preserve the existing gap |

`formatBlock` supplies the usual brace spacing, content indentation and closing
line break. Options in `BlockFormatOptions` let a language change those defaults.

## Install it

Include `RecipeFormatter.hpp` in your LSP module and assign the formatter after
installing the default services:

`src/recipe/lsp/LspModule.cpp`:

```cpp
void install_lsp_module(RecipeServices& services) {
  cursiv::install_default_lsp_services(services);
  services.formatter = std::make_unique<RecipeFormatter>(services);
}
```

There is no default formatter. The server advertises formatting support once
one is installed. Whole-document formatting produces no edits when parsing
required syntax error recovery.

## Check it

With your LSP services, test the expected text directly:

```cpp
(void)ct::expectFormatting(*services,
    {.before = "recipe P{use Flour 1}",
     .after = "recipe P {\n    use Flour 1\n}",
     .disposeAfterCheck = true});
```

The helper uses four spaces per indentation level by default; the editor
supplies its own formatting options. See
[Test your language](../learn/testing.md) for the test setup.

## Advanced: comments

For comment formatting, register `onHidden("SL_COMMENT", callback)`; the callback receives a
`HiddenNodeFormatter`. The Domainmodel example shows both line and multiline
comment callbacks in `DomainModelFormatter`.
