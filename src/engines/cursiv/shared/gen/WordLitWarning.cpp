#include "cursiv/shared/gen/WordLitWarning.hpp"

#include <algorithm>
#include <span>

#include "cursiv/grammar/word_final_in.hpp"
#include "cursiv/shared/gen/WarningFound.hpp"

namespace cursiv {

std::vector<WordLitWarning> word_lit_warnings(WarningFound& found) {
  std::vector<WordLitWarning> out;
  detail::GrammarImage& image = found.image();
  const detail::ImageClass bound = image.bound;
  if (!bound.known) return out;
  const auto walk = [&](std::uint32_t rule,
                        std::span<const detail::ImageId> roots) {
    found.each_node(roots, [&](detail::ImageId id) {
      const detail::ImageNode n = image.nodes[id];
      // A condition tests the text without reading it, and `through` reads
      // one token whatever its parts spell.
      if (n.kind == detail::ImageKind::andOf ||
          n.kind == detail::ImageKind::notOf ||
          n.kind == detail::ImageKind::through)
        return false;
      if (n.kind != detail::ImageKind::lit) return true;
      if ((n.flags & (detail::kLitGuardWord | detail::kLitGuardExpr)) != 0 ||
          !detail::word_final_in(n.text, bound.props, bound.complement,
                                 bound.scalars,
                                 (n.flags & detail::kLitFolded) != 0))
        return false;
      if (WordLitWarning w{found.name(rule), found.spell(id),
                           std::string(n.text)};
          std::ranges::find(out, w) == out.end())
        out.push_back(std::move(w));
      return false;
    });
  };
  for (const std::uint32_t rule : found.rules()) {
    const detail::ImageId body = found.body(rule);
    walk(rule, {&body, 1});
  }
  std::vector<detail::ImageId> ops;
  for (const std::uint32_t rule : found.tables()) {
    const detail::ImageFace f = image.ready(rule).face[1];
    ops.clear();
    for (std::uint32_t i = 0; i < f.levelCount; ++i)
      ops.push_back(image.levels[f.levelsAt + i].op);
    walk(rule, ops);
  }
  return out;
}

std::string word_lit_warning_text(const WordLitWarning& w) {
  std::string text;
  for (const char c : w.text) {
    if (c == '"' || c == '\\') text += '\\';
    text += c;
  }
  return "rule `" + w.rule + "`: `" + w.lit +
         "` is a literal with no guard that ends in a word character, so it "
         "also matches the start of a longer word, such as `" + w.text +
         "x` -- write `kw<\"" + text +
         "\">`, or give the literal a guard (`lit<\"" + text +
         "\", G>`) if that is intended";
}

}  // namespace cursiv
