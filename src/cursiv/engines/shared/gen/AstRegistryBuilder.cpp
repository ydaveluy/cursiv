#include "cursiv/engines/shared/gen/AstRegistryBuilder.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/TypeSpelling.hpp"

namespace cursiv {
namespace {

// Ids in the order a thing is first met.
template <class T>
class Interned {
 public:
  std::uint32_t id(const T* key) {
    const auto [at, fresh] = _ids.try_emplace(key, _keys.size());
    if (fresh) _keys.push_back(key);
    return at->second;
  }
  std::uint16_t index(const T* key) const {
    const auto at = _ids.find(key);
    return at == _ids.end() ? detail::kNoIndex
                            : static_cast<std::uint16_t>(at->second);
  }
  const std::vector<const T*>& keys() const { return _keys; }

 private:
  std::unordered_map<const T*, std::uint32_t> _ids;
  std::vector<const T*> _keys;
};

}  // namespace

AstRegistry build_ast_registry(std::span<const AstRowDesc* const> rows,
    std::span<const AstTypeDesc* const> bindings,
    std::span<const AstTypeDesc* const> rules, std::string_view grammar) {
  Interned<AstTypeDesc> types;
  Interned<AstFieldDesc> fields;
  // A conversion is numbered, never listed: its count is the map's size.
  std::unordered_map<const AstConvDesc*, std::uint32_t> convs;
  std::vector<std::vector<const AstTypeDesc*>> narrows;

  // Number types in their published order from the start: built classes,
  // named families, targets, then owners. Rows never need provisional ids.
  for (const AstRowDesc* row : rows)
    if (row->field != nullptr) fields.id(row->field);
  for (const AstRowDesc* row : rows)
    if (row->built) types.id(row->decl);
  const std::size_t built = types.keys().size();
  for (const AstRowDesc* row : rows)
    if (row->named) types.id(row->decl);
  for (const AstFieldDesc* f : fields.keys())
    for (std::uint32_t i = 0; i < f->targetCount; ++i) types.id(f->targets[i]);
  for (const AstFieldDesc* f : fields.keys()) types.id(f->owner);
  const auto& typeKeys = types.keys();

  AstRegistry out;
  // Every spelling is qualified here, once.
  const std::string qualified = gen_qualified(grammar);
  // Where each descriptor stands in `list`, the first place where it is
  // listed twice.
  const auto position_in = [](std::span<const AstTypeDesc* const> list) {
    std::unordered_map<const AstTypeDesc*, std::size_t> at;
    at.reserve(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) at.try_emplace(list[i], i);
    return at;
  };
  const auto ruleAt = position_in(rules);
  // A rule and an alias are spelt once, however many rows name them.
  std::map<std::pair<const AstTypeDesc*, std::string_view>, std::string_view>
      spelt;
  const auto rule_source = [&](const AstTypeDesc* type,
                                std::string_view alias) -> std::string_view {
    std::string_view& spelling = spelt[{type, alias}];
    if (spelling.empty()) {
      const auto at = ruleAt.find(type);
      if (at == ruleAt.end())
        throw std::logic_error(
            "a conversion rule is absent from its grammar bindings");
      spelling = out.sources.emplace_front(std::format(
          "::cursiv::detail::{}<{}, {}>", alias, qualified, at->second));
    }
    return spelling;
  };
  for (const AstRowDesc* const row : rows) {
    AstRowGen g = row->gen;
    if (row->convType != nullptr) {
      g.convName = rule_source(row->convType, "rule_bound_t");
      if (row->convOverride)
        g.convOver = rule_source(row->convType, "rule_override_t");
    }
    if (row->textType != nullptr) {
      g.textConv = rule_source(row->textType, "rule_bound_t");
      if (row->textOverride)
        g.textOver = rule_source(row->textType, "rule_override_t");
    }
    if (row->valueType != nullptr)
      g.valueRule = rule_source(row->valueType, "rule_staged_t");
    if (row->decl != nullptr)
      g.declType = types.index(row->decl);
    out.anyNest = out.anyNest || g.kind == WriteKind::nest;
    if (row->field != nullptr) {
      const auto f = fields.id(row->field);
      g.fieldSlot = static_cast<std::uint16_t>(f);
      if (narrows.size() <= f) narrows.resize(f + 1u);
      if (row->narrow != nullptr) {
        auto& list = narrows[f];
        auto at = std::ranges::find(list, row->narrow);
        if (at == list.end()) at = list.insert(list.end(), row->narrow);
        const auto slot = static_cast<std::size_t>(at - list.begin());
        if (slot >= 0xFFu)
          throw std::runtime_error(
              "the grammar narrows one reference member to more than 255 "
              "classes -- narrow it to fewer classes");
        g.narrowSlot = static_cast<std::uint8_t>(slot);
      }
    }
    if (row->conv != nullptr)
      g.convSlot = static_cast<std::uint16_t>(
          convs.try_emplace(row->conv, convs.size()).first->second);
    out.rows.push_back(g);
  }

  if (typeKeys.empty())
    throw std::runtime_error(
        "the grammar uses no AST type: no rule builds a node, no member "
        "points at one and no directive writes one -- make the entry rule "
        "build a node with `create<T>`");
  if (fields.keys().size() > 0xFFFEu)
    throw std::runtime_error(
        "the grammar writes more than 65 534 distinct members -- split the "
        "grammar");
  if (typeKeys.size() > 0xFFFEu)
    throw std::runtime_error(
        "the grammar uses more than 65 534 AST types -- split the grammar");
  if (convs.size() > 0xFFFEu)
    throw std::runtime_error(
        "the grammar uses more than 65 534 distinct conversions -- split the "
        "grammar");

  const auto bindingAt = position_in(bindings);
  const auto source = [&](const AstTypeDesc* type) {
    const auto at = bindingAt.find(type);
    if (at == bindingAt.end())
      throw std::logic_error("an AST type is absent from its grammar bindings");
    return std::format("::cursiv::detail::ast_bound_t<{}, {}>", qualified,
                       at->second);
  };
  for (std::size_t t = 0; t < typeKeys.size(); ++t)
    out.types.emplace_back(t < built, source(typeKeys[t]));

  out.typeWords = (typeKeys.size() + 63) / 64;
  out.derives.assign(typeKeys.size() * out.typeWords, 0);
  const auto derives = [&](std::size_t base, std::size_t sub) {
    const AstTypeTag* const b = typeKeys[base]->tag;
    const AstTypeTag* const s = typeKeys[sub]->tag;
    return b == nullptr || s == nullptr ? base == sub : s->derivesFrom(b);
  };
  for (std::size_t i = 0; i < typeKeys.size(); ++i)
    for (std::size_t j = 0; j < typeKeys.size(); ++j)
      if (derives(i, j))
        out.derives[i * out.typeWords + j / 64] |= std::uint64_t{1} << (j % 64);

  const std::size_t count = fields.keys().size();
  // Only a reference site accepts, so the rows stop past the last reference
  // member and a grammar with none keeps no rows.
  std::size_t bound = 0;
  for (std::size_t i = 0; i < count; ++i)
    if (detail::is_ref_nature(fields.keys()[i]->nature)) bound = i + 1;
  out.accepts.assign(bound * out.typeWords, 0);
  for (std::size_t i = 0; i < count; ++i) {
    const AstFieldDesc& f = *fields.keys()[i];
    AstRegistry::Field& entry = out.fields.emplace_back();
    entry.name = f.name;
    entry.nature = f.nature;
    entry.storage = f.storage;
    entry.owner = types.index(f.owner);
    entry.firstTarget = static_cast<std::uint32_t>(out.targets.size());
    entry.targetCount = f.targetCount;
    for (std::uint32_t t = 0; t < f.targetCount; ++t)
      out.targets.push_back(types.index(f.targets[t]));
    for (const AstTypeDesc* n : narrows[i]) entry.narrows.push_back(source(n));
    entry.offset = f.offset();
    if (detail::is_ref_nature(f.nature))
      for (const std::uint16_t t : out.targets_of(entry))
        for (std::size_t w = 0; w < out.typeWords; ++w)
          out.accepts[i * out.typeWords + w] |=
              out.derives[t * out.typeWords + w];
  }
  out.convCount = convs.size();
  return out;
}

}  // namespace cursiv
