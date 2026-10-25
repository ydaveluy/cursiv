#pragma once

#include <string_view>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"

// The parse, with the grammar erased: a service holds one of these per
// language and never learns which engine wrote it.

namespace cursiv {

class Parser {
 public:
  virtual ~Parser() noexcept = default;

  // The door into the grammar model: an element id a result carries means
  // something only through this. `getEntryRule()` lives on it.
  [[nodiscard]] virtual const Grammar& getGrammar() const noexcept = 0;

  // The result borrows `text`: every string in the AST is a view into it, so
  // the caller keeps the bytes alive at least as long as the result. A
  // `Document` owns both, and in that order. Only the view's bytes are read:
  // no NUL terminator or padding is required, and embedded NULs are content.
  [[nodiscard]] virtual ParseResult parse(
      std::string_view text,
      const CancellationToken& cancelToken = kNoCancel) const = 0;

  // What may be written at `at`, where the document's own text resumes at
  // `resume`: what lies between is what the answer replaces, `at == resume`
  // for a bare insertion. Answered from the stamps the engine left on the tree,
  // and a literal only where it can be read there: not swallowed by the token
  // ending at `at`, not run on into the text at `resume`. Answering nothing is
  // legal, and is what a tree no engine stamped gets.
  virtual void nextFeaturesAt(const Cst&, TextOffset, TextOffset,
                              std::vector<NextFeature>&) const {}
};

}  // namespace cursiv
