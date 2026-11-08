#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "cursiv/grammar/GrammarSource.hpp"
#include "cursiv/grammar/GrammarSourceData.hpp"

// Serves the source and dependency graph prepared by the generator.

namespace cursiv {

class DefaultGrammarSource : public GrammarSource {
 public:
  explicit DefaultGrammarSource(const detail::GrammarSourceData& data)
      : _data(data) {}
  DefaultGrammarSource(const DefaultGrammarSource&) = delete;
  DefaultGrammarSource& operator=(const DefaultGrammarSource&) = delete;

  [[nodiscard]] std::string_view grammarText() const noexcept override;
  [[nodiscard]] std::vector<std::string_view> ruleNames() const override;
  [[nodiscard]] std::string_view ruleText(std::string_view name) const override;
  [[nodiscard]] std::string ruleClosure(std::string_view name) const override;

 private:
  detail::GrammarSourceData _data;

  [[nodiscard]] const detail::GrammarSourceRule* find(
      std::string_view name) const;
};

}  // namespace cursiv
