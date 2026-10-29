#include "cursiv/core/parser/Parser.hpp"

#include <cassert>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/core/syntax-tree/SourceTranslation.hpp"

namespace cursiv {
namespace {

// A tree holds one translation, and its AST borrows that one's text: binding a
// second would free it.
constexpr const char* kNested =
    "cursiv: translating_parser over a parser that already translates; "
    "compose the phases in one translator";

// The tree as the engine wrote it, over the translated text and at its
// offsets: what the engine's completion reads.
Cst engine_tree(const Cst& cst, const SourceTranslation& translation) {
  Cst tree(translation.text());
  CstBuilder builder(tree);
  CstRun& run = builder.stream();
  run.reserve_for(cst.size());
  for (NodeId id = 0; id < cst.size(); ++id) {
    run.leaf(0, 0, 0);
    run.at(id) = cst[id];
  }
  if (cst.size() != 0) translation.to_text({&run.at(0), cst.size()});
  if (cst.engine_stamped()) builder.bind_stream();
  builder.bind_entry(cst.entry_begin(), cst.entry_end(), cst.entry_root());
  builder.bind_accepted(cst.accepted());
  std::vector<ParseDiagnostic> repairs(cst.diagnostics().begin(),
                                       cst.diagnostics().end());
  translation.to_text(std::span(repairs));
  for (ParseDiagnostic& d : repairs)
    builder.note_repair(d.kind, d.element, d.begin, d.end, std::move(d.also));
  if (cst.grammar() != nullptr) builder.bind_model(*cst.grammar(), cst.owned());
  return tree;
}

class TranslatingParser final : public Parser {
 public:
  TranslatingParser(std::unique_ptr<const Parser> parser,
                    SourceTranslator translate)
      : _parser(std::move(parser)), _translate(translate) {
    assert(_parser != nullptr && _translate != nullptr);
  }

  const Grammar& getGrammar() const noexcept override {
    return _parser->getGrammar();
  }

  ParseResult parse(std::string_view text,
                    const CancellationToken& cancelToken) const override {
    // Both sides of the map are 32-bit offsets.
    if (text.size() > kMaxDocument)
      throw std::overflow_error("cursiv: document exceeds 4 GiB");
    SourceTranslation translation(text);
    _translate(text, translation);
    if (!translation.changed()) return _parser->parse(text, cancelToken);
    translation.finish();
    // On the heap before the parse: the AST borrows the text, and moving a
    // short string afterwards would move its bytes too.
    auto owned =
        std::make_unique<const SourceTranslation>(std::move(translation));
    ParseResult result = _parser->parse(owned->text(), cancelToken);
    // A parser in between hid the inner translation from the wrap.
    if (result.cst().translation() != nullptr)
      throw std::invalid_argument(kNested);
    result.bind_translation(text, std::move(owned));
    return result;
  }

  void nextFeaturesAt(const Cst& cst, TextOffset at, TextOffset resume,
                      std::vector<NextFeature>& out) const override {
    const SourceTranslation* const translation = cst.translation();
    if (translation == nullptr)
      return _parser->nextFeaturesAt(cst, at, resume, out);
    // A caret strictly inside a replaced piece would split its spelling.
    const auto from = translation->translated(at);
    const auto to = translation->translated(resume);
    if (from && to)
      _parser->nextFeaturesAt(engine_tree(cst, *translation), *from, *to, out);
  }

 private:
  std::unique_ptr<const Parser> _parser;
  SourceTranslator _translate;
};

}  // namespace

std::unique_ptr<const Parser> translating_parser(
    std::unique_ptr<const Parser> parser, SourceTranslator translate) {
  if (dynamic_cast<const TranslatingParser*>(parser.get()) != nullptr)
    throw std::invalid_argument(kNested);
  return std::make_unique<const TranslatingParser>(std::move(parser),
                                                   translate);
}

}  // namespace cursiv
