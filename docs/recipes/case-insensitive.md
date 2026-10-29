# Case-insensitive languages

Keyword matching and name matching are separate settings. Both can ignore the
case of ASCII letters (`A`–`Z` and `a`–`z`).

## Keywords in any case

Use `ikw<>` in place of `kw<>` for keywords that accept any letter case:

```cpp
static constexpr auto Reserved = ikw<"SELECT"> | ikw<"FROM">;
```

`select`, `SELECT` and `SeLeCt` all match, and the document keeps its own
spelling.

## Names that match regardless of case

To let `StrinG` resolve to a declared `String`, override the scope provider:

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

Install it after the generated module:

```cpp
services.scopeProvider = std::make_unique<MyScopeProvider>(services);
```

Only the match ignores case: rename and go-to-definition still show the name
as written. `isCaseSensitive` receives the reference, so a provider can, for
example, ignore case for type names only.

## Advanced: identifier boundaries

`kw<>` and `ikw<>` do not match the start of a longer name. The
`word_boundary` setting defines the characters that continue a name; it
defaults to `w` (ASCII letters, digits and underscore). For names that allow
hyphens:

```cpp
static constexpr auto word_boundary = cr<"a-zA-Z0-9_-">;
```

Then `if` no longer matches the start of `if-x`. See
[Literals and keywords](../reference/grammar.md#literals-and-keywords).
