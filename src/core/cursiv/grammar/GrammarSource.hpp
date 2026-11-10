#pragma once

#include <string>
#include <string_view>
#include <vector>

// The grammar as readable text at runtime: all of it, one rule, or a rule with
// every rule it reaches, in the format of `GRAMMAR.txt`.

namespace cursiv {

class GrammarSource {
 public:
  virtual ~GrammarSource() noexcept = default;

  // Byte for byte the committed `GRAMMAR.txt`.
  [[nodiscard]] virtual std::string_view grammarText() const noexcept = 0;

  // In order of first appearance, the entry rule first.
  [[nodiscard]] virtual std::vector<std::string_view> ruleNames() const = 0;

  // One rule as `Name: body;` -- empty when no rule bears the name.
  [[nodiscard]] virtual std::string_view ruleText(
      std::string_view name) const = 0;

  // The rule and every rule it reaches, one per line, in order of first
  // appearance; empty when no rule bears the name.
  [[nodiscard]] virtual std::string ruleClosure(
      std::string_view name) const = 0;
};

}  // namespace cursiv
