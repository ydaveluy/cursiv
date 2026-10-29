# Formatting

A formatter sets the spacing, line breaks and indentation around syntax
elements. Cursiv turns it into editor edits for a whole document or a
selected range.

## Format a recipe

This example uses the AST from the [tutorial](../learn/walkthrough.md). Each
callback receives a `FormattingBuilder` and the node to format:

```cpp title="src/recipe/lsp/RecipeFormatter.hpp"
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

`on<T>` registers a callback for exactly the class `T`. A node formatter
selects **regions**: a keyword, a property, a node, or the content between
delimiters. `prepend`, `append` and `surround` place an action around a
region:

| Action | Effect |
|---|---|
| `noSpace` | Remove the gap |
| `oneSpace` | Use one space |
| `newLine` | Insert a line break |
| `indent` | Insert a line break and increase indentation |
| `keep` | Preserve the existing gap |

`formatBlock` applies the usual block layout: a space before the brace,
indented content and the closing brace on its own line. `BlockFormatOptions`
changes those defaults.

## Install it

Include `RecipeFormatter.hpp` in your LSP module and install the formatter
after the default services:

```cpp title="src/recipe/lsp/LspModule.cpp"
void install_lsp_module(RecipeServices& services) {
  cursiv::install_default_lsp_services(services);
  services.formatter = std::make_unique<RecipeFormatter>(services);
}
```

There is no default formatter: the server offers formatting once you install
one. A document with syntax errors is not formatted as a whole.

## Check it

Test the expected text directly:

```cpp
(void)ct::expectFormatting(*services,
    {.before = "recipe P{use Flour 1}",
     .after = "recipe P {\n    use Flour 1\n}",
     .disposeAfterCheck = true});
```

The helper indents with four spaces by default; an editor sends its own
options. See [Test your language](../learn/testing.md) for the test setup.

## Advanced: comments

To format comments, register `onHidden("SL_COMMENT", callback)`; the callback
receives a `HiddenNodeFormatter`. The Domainmodel example's
`DomainModelFormatter` formats both line and block comments.
