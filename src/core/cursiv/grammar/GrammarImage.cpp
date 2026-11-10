#include "cursiv/grammar/GrammarImage.hpp"

#include <utility>

#include "cursiv/utils/CaseFold.hpp"

namespace cursiv::detail {

std::span<const ImageId> GrammarImage::shape_children(ImageId id) const {
  const ImageNode& n = nodes[id];
  switch (n.kind) {
    case ImageKind::lit:
    case ImageKind::gapHas:
    case ImageKind::gapIs:
    case ImageKind::implied:
      return {};
    default:
      return {children.data() + n.first, n.count};
  }
}

std::uint32_t GrammarImage::list(const ImageRuleDesc* const* descs,
                                 std::size_t count) {
  std::vector<std::uint32_t> ids;
  ids.reserve(count);
  for (std::size_t i = 0; i < count; ++i) ids.push_back(rule(*descs[i]));
  const auto at = static_cast<std::uint32_t>(ruleLists.size());
  ruleLists.insert(ruleLists.end(), ids.begin(), ids.end());
  return at;
}

void GrammarImage::open(const ImageGrammarDesc& g) {
  name = g.name;
  entry = rule(*g.entry);
  ignoreAt = list(g.ignore, g.ignoreCount);
  ignoreCount = g.ignoreCount;
  hiddenAt = list(g.hidden, g.hiddenCount);
  hiddenCount = g.hiddenCount;
  bound = {g.bound.props, g.bound.complement, g.bound.scalars, true};
  plain = {g.plain.props, g.plain.complement, g.plain.scalars, true};
  caseInsensitive = g.caseInsensitive;
}

ImageId GrammarImage::node(const ImageDesc& d, bool folded) {
  return descend(make_node(d, folded));
}

Descent<ImageId> GrammarImage::make_node(const ImageDesc& d, bool folded) {
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
              .row = d.row,
              .tag = d.tag};
  if (d.kind == ImageKind::skip)
    n.a = list(d.rules, static_cast<std::size_t>(d.m + d.n));
  else if (d.rule != nullptr)
    n.a = rule(*d.rule);
  n.first = static_cast<ImageId>(children.size());
  n.count = d.count;
  children.insert(children.end(), childIds.begin(), childIds.end());
  nodes.push_back(n);
  const auto id = static_cast<ImageId>(nodes.size() - 1);
  known.emplace(&d, id);
  co_return id;
}

bool GrammarImage::DerivedKey::operator==(const DerivedKey& o) const {
  return shape.kind == o.shape.kind && shape.flags == o.shape.flags &&
         shape.a == o.shape.a && shape.m == o.shape.m && shape.n == o.shape.n &&
         shape.text.data() == o.shape.text.data() &&
         shape.text.size() == o.shape.text.size() &&
         shape.text2.data() == o.shape.text2.data() &&
         shape.text2.size() == o.shape.text2.size() &&
         shape.row == o.shape.row && shape.tag == o.shape.tag &&
         children == o.children;
}

std::size_t GrammarImage::DerivedHash::operator()(const DerivedKey& k) const {
  std::size_t h = static_cast<std::size_t>(k.shape.kind) * 1000003u +
                  k.shape.flags + k.shape.a * 31u + k.shape.m * 17u +
                  k.shape.n * 13u;
  h ^= reinterpret_cast<std::size_t>(k.shape.text.data()) >> 3;
  h ^= reinterpret_cast<std::size_t>(k.shape.row) >> 3;
  h ^= reinterpret_cast<std::size_t>(k.shape.tag) >> 3;
  for (const ImageId id : k.children) h = h * 1000003u + id;
  return h;
}

ImageId GrammarImage::derive(const ImageNode& shape,
                             std::span<const ImageId> childIds) {
  DerivedKey key{shape, {childIds.begin(), childIds.end()}};
  key.shape.first = 0;
  key.shape.count = 0;
  if (const auto it = _derived.find(key); it != _derived.end())
    return it->second;
  ImageNode n = shape;
  n.first = static_cast<ImageId>(children.size());
  n.count = static_cast<std::uint32_t>(childIds.size());
  children.insert(children.end(), childIds.begin(), childIds.end());
  nodes.push_back(n);
  const auto id = static_cast<ImageId>(nodes.size() - 1);
  _derived.try_emplace(std::move(key), id);
  return id;
}

std::uint32_t GrammarImage::rule(const ImageRuleDesc& d) {
  if (const auto it = _rules.find(&d); it != _rules.end()) return it->second;
  const auto id = static_cast<std::uint32_t>(rules.size());
  ImageRule r;
  r.desc = &d;
  r.name = d.name;
  r.qualified = d.qualified;
  r.anonymous = d.anonymous;
  r.foreign = d.foreign;
  r.inlineTerminal = d.inlineTerminal;
  r.grammar = d.grammar;
  rules.push_back(std::move(r));
  _rules.emplace(&d, id);
  return id;
}

ImageFace GrammarImage::face(const ImageFaceDesc& d, bool folded) {
  ImageFace f;
  f.kind = d.kind;
  f.returns = d.returns;
  if (d.body != nullptr) f.body = node(*d.body, folded);
  if (d.primary != nullptr) f.primary = rule(*d.primary);
  for (int i = 0; i < 3; ++i) f.members[i] = d.members[i];
  f.levelsAt = static_cast<std::uint32_t>(levels.size());
  f.levelCount = d.levelCount;
  for (std::uint32_t i = 0; i < d.levelCount; ++i) {
    const ImageLevelDesc& level = d.levels[i];
    const ImageId op = node(*level.op, folded);
    levels.push_back(
        {level.group, op, level.node, {level.members[0], level.members[1]}});
  }
  f.ignoreAt = list(d.ignore, d.ignoreCount);
  f.ignoreCount = d.ignoreCount;
  f.hiddenAt = list(d.hidden, d.hiddenCount);
  f.hiddenCount = d.hiddenCount;
  if (d.entry != nullptr) f.entry = rule(*d.entry);
  f.bound = d.bound;
  return f;
}

const ImageRule& GrammarImage::ready(std::uint32_t id) {
  if (!rules[id].completed) {
    rules[id].completed = true;
    const ImageRuleDesc& d = *rules[id].desc;
    const bool folded = folds(id);
    const ImageFace written = face(*d.written, folded);
    const ImageFace resolved =
        d.resolved == d.written ? written : face(*d.resolved, folded);
    rules[id].face[0] = written;
    rules[id].face[1] = resolved;
  }
  return rules[id];
}

bool GrammarImage::folds(std::uint32_t rule) const {
  const ImageRuleDesc* ownerGrammar = rules[rule].desc->ownerGrammar;
  return ownerGrammar != nullptr ? ownerGrammar->written->caseInsensitive
                                 : caseInsensitive;
}

}  // namespace cursiv::detail
