#include "cursiv/engine/parser/LanguageParser.hpp"

#include <algorithm>
#include <span>
#include <utility>

#include "cursiv/engine/completion/Caret.hpp"
#include "cursiv/engine/parser/Parse.hpp"
#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/utils/CaseFold.hpp"

namespace cursiv::engine {

namespace {

// The grammar's answers about the records this language's parser writes.
class RecordGrammar final : public Grammar {
 public:
  explicit RecordGrammar(const Language& language) : _f(language.grammar), _ast(language.ast) {}

  bool recordWrites(const CstNode& node, std::string_view member) const noexcept override {
    const ElementFacts* const e = element(node);
    if (e != nullptr)
      for (auto at = e->writes; at != e->fills; ++at)
        if (_f.names[_f.members[at]] == member) return true;
    return false;
  }
  bool recordFills(const CstNode& node, std::string_view member) const noexcept override {
    return sites(node, member, 0, nullptr).here;
  }
  PropertySites propertySites(const CstNode& node, std::string_view member,
                              std::size_t depth, const AstNode& owner) const noexcept override {
    return sites(node, member, depth, &owner);
  }
  std::string_view recordRule(const CstNode& node) const noexcept override {
    const ElementFacts* const e = element(node);
    return e != nullptr ? _f.names[e->rule] : std::string_view{};
  }
  bool recordIsDataRule(const CstNode& node) const noexcept override {
    const ElementFacts* const e = element(node);
    return e != nullptr && e->dataRule;
  }
  bool recordIsLiteral(const CstNode& node) const noexcept override {
    if (node.subtreeSize != 1 || node.isHidden() || node.element() == kSkippedElement ||
        (node.begin == node.end && !node.isRecovered()))
      return false;
    const ElementFacts* const e = element(node);
    return e == nullptr || !e->terminal;
  }
  std::string_view recordProperty(const CstNode& node) const noexcept override {
    const ElementFacts* const e = element(node);
    return e != nullptr ? _f.names[e->feature] : std::string_view{};
  }
  std::span<const std::string_view> terminalSpellings(
      std::uint16_t terminal) const noexcept override {
    return terminal < _f.terminalCount ? _f.terminals[terminal].spellings
                                       : std::span<const std::string_view>{};
  }
  std::string_view terminalName(std::uint16_t terminal) const noexcept override {
    return terminal < _f.terminalCount ? _f.terminals[terminal].name : std::string_view{};
  }
  std::string_view terminalDoc(std::uint16_t terminal) const noexcept override {
    return terminal < _f.terminalCount ? _f.terminals[terminal].doc : std::string_view{};
  }
  std::string_view spellingDoc(std::string_view text) const noexcept override {
    return spelled(text).doc;
  }
  bool spellingFolds(std::string_view text) const noexcept override {
    return spelled(text).folded;
  }
  std::string_view recordDoc(const CstNode& node) const noexcept override {
    const ElementFacts* const e = element(node);
    return e != nullptr ? e->doc : std::string_view{};
  }

 private:
  [[nodiscard]] const ElementFacts* element(const CstNode& node) const noexcept {
    // A hidden record is read as an element too.
    CstNode shown = node;
    shown.setHidden(false);
    return shown.element() < _f.elementCount ? &_f.elements[shown.element()] : nullptr;
  }
  PropertySites sites(const CstNode& node, std::string_view member, std::size_t depth,
                      const AstNode* owner) const noexcept {
    PropertySites result{false, true};
    if (const ElementFacts* e = element(node))
      for (auto at = e->fills; at != e->end; ++at) {
        const auto id = _f.members[at];
        if (id == 0) {
          if (owner == nullptr) continue;
          if (depth == 0) {
            result.below = false;
            break;
          }
          --depth;
        } else if (id == detail::kNoIndex) {
          const auto field = _ast.memberOf[_f.members[++at]];
          const auto& desc = _ast.meta->fields[field];
          if (depth != 0 || desc.name != member) continue;
          if (owner == nullptr) return {true, true};
          if (owner->type && owner->type->derivesFrom(_ast.meta->types[desc.owner]))
            _ast.meta->yields[field](*owner, [&](std::string_view, std::uint32_t, AstNode& child) {
              result.nested = &child;
            }, 1);
        } else if (depth == 0 && _f.names[id] == member) {
          result.here = true;
          if (owner == nullptr) break;
        }
      }
    return result;
  }
  // Combine the documentation of every literal that accepts this spelling.
  [[nodiscard]] GrammarWord spelled(std::string_view text) const noexcept {
    const auto matches = std::ranges::equal_range(_f.words, GrammarWord::key(text), {},
        [](const GrammarWord& word) { return GrammarWord::key(word.text); });
    GrammarWord result{text};
    for (const auto& word : matches) {
      const auto kind = detail::name_match(word.text, text);
      if (kind == detail::NameMatch::Exact || (word.folded && kind != detail::NameMatch::None))
        result.merge(word);
    }
    return result;
  }

  const GrammarFacts& _f;
  const AstTables& _ast;
};

}  // namespace

LanguageParser::LanguageParser(const Language& language)
    : _language(language), _grammar(std::make_shared<RecordGrammar>(language)) {}

const Grammar& LanguageParser::getGrammar() const noexcept { return *_grammar; }

ParseResult LanguageParser::parse(std::string_view text,
                                  const CancellationToken& cancelToken) const {
  Cst cst;
  engine::parse(_language.tables, text, cst, cancelToken);
  ParseResult out = AstBuilder::build(_language.ast, std::move(cst), cancelToken);
  out.bindGrammar(_grammar);
  return out;
}

void LanguageParser::nextFeaturesAt(const Cst& cst, TextOffset at, TextOffset resume,
                                    std::vector<NextFeature>& out,
                                    const CancellationToken& cancelToken) const {
  next_features(_language, cst, at, resume, out, cancelToken);
}

}  // namespace cursiv::engine
