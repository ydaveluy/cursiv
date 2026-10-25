#include "cursiv/core/grammar/DefaultGrammarSource.hpp"

#include <cstddef>

namespace cursiv {

std::string_view DefaultGrammarSource::grammarText() const noexcept {
  return _data.text;
}

std::vector<std::string_view> DefaultGrammarSource::ruleNames() const {
  std::vector<std::string_view> names;
  names.reserve(_data.rules.size());
  for (const auto& rule : _data.rules) names.push_back(rule.name);
  return names;
}

std::string_view DefaultGrammarSource::ruleText(std::string_view name) const {
  const auto* rule = find(name);
  return rule == nullptr ? std::string_view{} : rule->text;
}

std::string DefaultGrammarSource::ruleClosure(std::string_view name) const {
  const auto* first = find(name);
  if (first == nullptr) return {};
  const auto start = static_cast<std::size_t>(first - _data.rules.data());
  std::vector<std::size_t> queue{start};
  std::vector<bool> seen(_data.rules.size());
  seen[start] = true;
  std::string out;
  for (std::size_t i = 0; i < queue.size(); ++i) {
    const auto& rule = _data.rules[queue[i]];
    if (!out.empty()) out += '\n';
    out += rule.text;
    for (const auto reached : rule.dependencies) {
      if (reached >= seen.size() || seen[reached]) continue;
      seen[reached] = true;
      queue.push_back(reached);
    }
  }
  return out;
}

const GrammarSourceRule* DefaultGrammarSource::find(
    std::string_view name) const {
  for (const auto& rule : _data.rules)
    if (rule.name == name) return &rule;
  return nullptr;
}

}  // namespace cursiv
