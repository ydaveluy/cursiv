# Case-insensitive languages

You set keyword matching and name matching separately. Both ignore letter
case in the same way, for the letters of every script (see
[What matches](#what-matches)).

## Keywords in any case

Declare the grammar case-insensitive:

```cpp
struct Sql : cursiv::GrammarSpec {
  static constexpr bool case_insensitive = true;
  struct Select : rule<kw<"SELECT"> + ref<Column> + kw<"FROM"> + ref<Table>> {};
  // ...
};
```

Every `kw<>` and `lit<>` of the grammar then ignores letter case: `select`,
`SELECT` and `SeLeCt` all match, and the document keeps its own spelling.
Letters beyond ASCII fold too: `kw<"été">` also matches `ÉTÉ`. Spellings
with no letter, such as `lit<"{">`, are unchanged. Character classes keep
their own letters: write `cr<"a-zA-Z">` for both cases. A derived grammar
inherits the setting and may declare its own; a grammar embedded with
`section<>` keeps its own.

In a case-sensitive grammar, `ikw<>` makes one keyword case-insensitive:

```cpp
static constexpr auto keywords = ikw<"SELECT"> | ikw<"FROM">;
```

## Names that match regardless of case

To let `StrinG` resolve to a declared `String`, override the scope provider:

```cpp
#include "cursiv/references/DefaultScopeProvider.hpp"

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

## What matches

Each letter matches its other cases, one letter for one letter:

- `É` matches `é`, and `Σ` matches `σ` and `ς`.
- A letter never matches two: `ß` does not match `SS`.
- `İ` and `ı` match only themselves, so `I` matches `i` alone.
- A case of another length in UTF-8 does not match: `ẞ` and `ß`, the Kelvin
  sign `K` and `k`, the long `ſ` and `s`, and the Ohm `Ω` and Angstrom `Å`
  signs and their letters stay apart. An ASCII letter matches only its two
  ASCII cases.
- Text is not normalized: `é` written as `e` and a combining accent does not
  match `é`.

## Advanced: identifier boundaries

`kw<>` and `ikw<>` do not match the start of a longer name. The
`word_boundary` setting defines the characters that continue a name; it
defaults to `w` (ASCII letters, digits and underscore). For names that allow
hyphens:

```cpp
static constexpr auto word_boundary = cr<"a-zA-Z0-9_-">;
```

Then `if` no longer matches the start of `if-x`. Keywords that end in a
letter beyond ASCII need such letters in the class too, for example
`uc::XID_Continue`. See
[Literals and keywords](../reference/grammar.md#literals-and-keywords).
