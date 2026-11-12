// One case of a suite grammar as a parser of its own: every text is read behind
// the case's prefix, and an offset asked of it counts from the text.
#pragma once

#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/parser/Parser.hpp"

namespace support {

class CaseParser final : public cursiv::Parser {
 public:
  CaseParser(std::unique_ptr<const cursiv::Parser> suite,
             std::string_view prefix)
      : _suite(std::move(suite)), _prefix(prefix) {}

  [[nodiscard]] const cursiv::Grammar& getGrammar() const noexcept override {
    return _suite->getGrammar();
  }
  using cursiv::Parser::parse;
  [[nodiscard]] cursiv::ParseResult parse(
      std::string_view text,
      const cursiv::CancellationToken& stop = cursiv::kNoCancel) const override {
    return _suite->parse(_texts.emplace_back(_prefix + std::string(text)),
                         stop);
  }
  void nextFeaturesAt(
      const cursiv::Cst& cst, cursiv::TextOffset at, cursiv::TextOffset resume,
      std::vector<cursiv::NextFeature>& out,
      const cursiv::CancellationToken& stop = cursiv::kNoCancel) const override {
    const auto shift = static_cast<cursiv::TextOffset>(_prefix.size());
    _suite->nextFeaturesAt(cst, at + shift, resume + shift, out, stop);
  }

 private:
  std::unique_ptr<const cursiv::Parser> _suite;
  std::string _prefix;
  // Every text read, kept for the trees that view it.
  mutable std::deque<std::string> _texts;
};

inline std::unique_ptr<const cursiv::Parser> case_parser(
    std::unique_ptr<const cursiv::Parser> suite, std::string_view prefix) {
  return std::make_unique<CaseParser>(std::move(suite), prefix);
}

}  // namespace support
