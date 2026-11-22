// A language's parser as the services hold it: the text read into records,
// then the tree built from them.
#pragma once

#include <memory>
#include <string_view>

#include "cursiv/engine/parser/Language.hpp"
#include "cursiv/parser/Parser.hpp"

namespace cursiv::engine {

class LanguageParser final : public Parser {
 public:
  explicit LanguageParser(const Language& language);

  [[nodiscard]] const Grammar& getGrammar() const noexcept override;
  using Parser::parse;
  [[nodiscard]] ParseResult parse(
      std::string_view text,
      const CancellationToken& cancelToken = kNoCancel) const override;
  void nextFeaturesAt(const Cst& cst, TextOffset at, TextOffset resume,
                      std::vector<NextFeature>& out,
                      const CancellationToken& cancelToken = kNoCancel) const override;

 private:
  const Language& _language;
  // Shared with every result: a tree outlives its parser.
  std::shared_ptr<const Grammar> _grammar;
};

}  // namespace cursiv::engine
