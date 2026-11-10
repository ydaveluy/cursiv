#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/syntax-tree/SourceTranslation.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"

// The parse, with the grammar erased: a service holds one of these per
// language and never learns which engine wrote it.

namespace cursiv {

class Parser {
 public:
  virtual ~Parser() noexcept = default;

  // The grammar the element ids of a result refer to. The entry rule is the
  // grammar's `entry`, fixed when the parser is generated.
  [[nodiscard]] virtual const Grammar& getGrammar() const noexcept = 0;

  // The result does not own `text`: the AST's views point into it, so it must
  // outlive the result. Only the view's bytes are read; NULs are content.
  [[nodiscard]] virtual ParseResult parse(
      std::string_view text,
      const CancellationToken& cancelToken = kNoCancel) const = 0;
  // A temporary that owns its text is refused where it is written: it dies at the
  // end of the expression, and the tree would view it. It wins the overload as an
  // exact match.
  template <detail::owning_text_rvalue S>
  ParseResult parse(S&&, const CancellationToken& = kNoCancel) const {
    detail::refuse_owning_bind<S>();
    return {};
  }

  // What may be written at `at`, where the document's text resumes at
  // `resume` (`at == resume` for an insertion). Read from the element ids the
  // engine wrote on the tree; by default there is no answer.
  virtual void nextFeaturesAt(const Cst&, TextOffset, TextOffset,
                              std::vector<NextFeature>&) const {
  }
};

// `parser` reading the text `translate` makes of each document, while
// every offset it answers is the document's. Over a parser that already
// translates, this throws `std::invalid_argument`.
[[nodiscard]] std::unique_ptr<const Parser> translating_parser(
    std::unique_ptr<const Parser> parser, SourceTranslator translate);

}  // namespace cursiv
