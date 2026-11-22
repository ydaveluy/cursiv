#include "cursiv/engine/grammar/GrammarImage.hpp"

#include "cursiv/utils/CaseFold.hpp"

namespace cursiv::engine::gen {

void GrammarImage::open(const ImageGrammarDesc& g) {
  // The entry is the first rule read: the rules are numbered from it.
  rule(*g.entry);
  name = g.name;
  trivia = g.trivia;
  bound = g.bound;
  plain = g.plain;
  caseInsensitive = g.caseInsensitive;
  if (g.indentOpen != nullptr) indentOpen = node(*g.indentOpen, caseInsensitive);
  if (g.indentClose != nullptr) indentClose = node(*g.indentClose, caseInsensitive);
  if (g.indentNewline != nullptr) indentNewline = rule(*g.indentNewline);
}

ImageId GrammarImage::node(const ImageDesc& d, bool folded) {
  return detail::descend(make_node(d, folded));
}

detail::Descent<ImageId> GrammarImage::make_node(const ImageDesc& d, bool folded) {
  auto& known = folded ? _foldedNodes : _nodes;
  if (const auto it = known.find(&d); it != known.end()) co_return it->second;
  std::vector<ImageId> childIds;
  childIds.reserve(d.count);
  for (std::uint32_t i = 0; i < d.count; ++i)
    childIds.push_back(co_await make_node(*d.children[i], folded));
  const bool folds = folded && d.kind == ImageKind::lit &&
                     detail::has_cased_letter(d.text);
  ImageNode n{.kind = d.kind,
              .flags = static_cast<std::uint8_t>(
                  d.flags | (folds ? kLitFolded : 0)),
              .m = d.m,
              .n = d.n,
              .text = d.text,
              .text2 = d.text2,
              .row = d.row};
  if (d.kind == ImageKind::skip)
    n.trivia = d.trivia;
  else if (d.rule != nullptr)
    n.a = rule(*d.rule);
  if (d.kind == ImageKind::lit && (d.flags & kLitKeyword) && (d.flags & kLitGuardExpr)) {
    const ImageNode& guard = nodes[childIds.front()];
    if (bound == nodes[children[guard.first]].characterClass()) {
      n.flags = (n.flags & ~kLitGuardExpr) | kLitGuardWord;
      childIds.clear();
    }
  }
  n.first = static_cast<ImageId>(children.size());
  n.count = static_cast<std::uint32_t>(childIds.size());
  children.insert(children.end(), childIds.begin(), childIds.end());
  nodes.push_back(n);
  const auto id = static_cast<ImageId>(nodes.size() - 1);
  known.emplace(&d, id);
  co_return id;
}

ImageId GrammarImage::group(ImageKind kind, std::span<const ImageId> parts) {
  ImageNode n{};
  n.kind = kind;
  n.first = static_cast<ImageId>(children.size());
  n.count = static_cast<std::uint32_t>(parts.size());
  children.insert(children.end(), parts.begin(), parts.end());
  nodes.push_back(n);
  return static_cast<ImageId>(nodes.size() - 1);
}

std::uint32_t GrammarImage::rule(const ImageRuleDesc& d) {
  if (const auto it = _rules.find(&d); it != _rules.end()) return it->second;
  const auto id = static_cast<std::uint32_t>(rules.size());
  rules.push_back(&d);
  _rules.emplace(&d, id);
  return id;
}

bool GrammarImage::folds(std::uint32_t rule) const {
  const ImageRuleDesc* ownerGrammar = rules[rule]->ownerGrammar;
  return ownerGrammar != nullptr ? ownerGrammar->caseInsensitive
                                 : caseInsensitive;
}

}  // namespace cursiv::engine::gen
