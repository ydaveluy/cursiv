#pragma once

#include <string>
#include <string_view>
#include <vector>

// The grammar as reviewable text, served at runtime: the whole of it, one
// rule, or a rule with everything it reaches. The text is the dump's own
// format, the same one GRAMMAR.txt is written in.

namespace cursiv {

class GrammarSource {
 public:
  virtual ~GrammarSource() noexcept = default;

  // Folded for review, byte for byte what the committed `GRAMMAR.txt` holds.
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
