# Case-insensitive languages

Keyword matching and reference-name matching are independent settings. Both
can ignore the case of ASCII letters (`A`–`Z` and `a`–`z`).

## Keywords in any case

Use `ikw<>` in place of `kw<>` for keywords that accept any letter case:

```cpp
static constexpr auto Reserved = ikw<"SELECT"> | ikw<"FROM">;
```

`select`, `SELECT` and `SeLeCt` all match; the document keeps what it wrote.

## Names that match regardless of case

A different question: should `StrinG` resolve the `String` a document declares?
Configure the scope provider separately:

```cpp
#include "cursiv/core/references/DefaultScopeProvider.hpp"

class MyScopeProvider final : public cursiv::DefaultScopeProvider {
 public:
  using DefaultScopeProvider::DefaultScopeProvider;

 protected:
  [[nodiscard]] bool isCaseSensitive(
      const cursiv::ReferenceInfo&) const noexcept override {
    return false;
  }
};
```

The spelling stays as written — only the match is case-blind, so rename and
go-to-definition keep showing what the author typed.

Install the provider after the generated module:

```cpp
services.scopeProvider = std::make_unique<MyScopeProvider>(services);
```

## Advanced: identifier boundaries

`kw<>` and `ikw<>` check that a keyword does not end inside a longer name.
`word_boundary` describes characters that can continue a name; its default
is `w` (ASCII letters, digits and underscore). Set it when your identifiers
use a different character set. For names that allow hyphens:

```cpp
static constexpr auto word_boundary = cr<"a-zA-Z0-9_-">;
```

This prevents `if` from matching the start of `if-x`. The boundary class is
tested as written, independently of case-insensitive keyword matching. For
identifiers restricted to uppercase letters, use `cr<"A-Z">`.

For name lookup, `isCaseSensitive` receives a `ReferenceInfo` describing the
reference. A custom provider can use it to compare type names without case
while keeping variable names case-sensitive.
