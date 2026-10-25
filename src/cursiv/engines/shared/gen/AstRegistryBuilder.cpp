#include "cursiv/engines/shared/gen/AstRegistryBuilder.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <unordered_map>
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
  Interned<AstConvDesc> convs;
  std::vector<std::vector<const AstTypeDesc*>> narrows;

  // Number types in their published order from the start: built classes,
  // named families, targets, then owners. Rows never need provisional ids.
  for (const AstRowDesc* row : rows)
    if (row->field != nullptr) fields.id(row->field);
  for (const AstRowDesc* row : rows)
    if (row->built) types.id(row->type);
  const std::size_t built = types.keys().size();
  for (const AstRowDesc* row : rows)
    if (row->named) types.id(row->type);
  for (const AstFieldDesc* f : fields.keys())
    for (std::uint32_t i = 0; i < f->targetCount; ++i) types.id(f->targets[i]);
  for (const AstFieldDesc* f : fields.keys()) types.id(f->owner);
  const auto& typeKeys = types.keys();

  AstRegistry out;
  const auto rule_source = [&](const AstTypeDesc* type, bool over) -> std::string_view {
    const auto at = std::ranges::find(rules, type);
    if (at == rules.end())
      throw std::logic_error("a conversion rule is absent from its grammar bindings");
    out.sources.push_back(std::format("::cursiv::detail::{}<{}, {}>",
        over ? "rule_override_t" : "rule_bound_t", gen_qualified(grammar),
        at - rules.begin()));
    return out.sources.back();
  };
  for (const AstRowDesc* const row : rows) {
    AstRowGen g = row->gen;
    if (row->convType != nullptr) {
      g.convName = rule_source(row->convType, false);
      if (!g.convOver.empty()) g.convOver = rule_source(row->convType, true);
    }
    if (row->textType != nullptr) {
      g.textConv = rule_source(row->textType, false);
      if (!g.textOver.empty()) g.textOver = rule_source(row->textType, true);
    }
    if (row->type != nullptr)
      g.typeSlot = types.index(row->type);
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
              "this grammar declares more narrowings of one member than a "
              "reference's index byte can name");
        g.narrowSlot = static_cast<std::uint8_t>(slot);
      }
    }
    if (row->conv != nullptr)
      g.convSlot = static_cast<std::uint16_t>(convs.id(row->conv));
    out.rows.push_back(g);
  }

  if (typeKeys.empty())
    throw std::runtime_error(
        "this grammar materialises an AST but names no AST type -- no rule "
        "builds one, no member points at one, no directive writes one; a "
        "purely lexical grammar parses, it does not build");
  if (fields.keys().size() > 0xFFFEu)
    throw std::runtime_error(
        "this grammar writes more distinct members than a record's field "
        "index can name");
  if (typeKeys.size() > 0xFFFEu)
    throw std::runtime_error(
        "this grammar names more distinct AST types than a record's type "
        "index can name");
  if (convs.keys().size() > 0xFFFEu)
    throw std::runtime_error(
        "this grammar needs more distinct conversions than a record's "
        "conversion index can name");

  const auto source = [&](const AstTypeDesc* type) {
    const auto at = std::ranges::find(bindings, type);
    if (at == bindings.end())
      throw std::logic_error("an AST type is absent from its grammar bindings");
    return std::format("::cursiv::detail::ast_bound_t<{}, {}>", gen_qualified(grammar),
                       at - bindings.begin());
  };
  for (std::size_t t = 0; t < typeKeys.size(); ++t)
    out.types.push_back({typeKeys[t]->name, t < built, source(typeKeys[t])});
  out.boundTypes = true;

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

  std::vector<std::size_t> from;
  for (const AstFieldDesc* f : fields.keys()) {
    from.push_back(out.targets.size());
    for (std::uint32_t i = 0; i < f->targetCount; ++i)
      out.targets.push_back(types.index(f->targets[i]));
  }

  const std::size_t count = fields.keys().size();
  out.accepts.assign(count == 0 ? 1 : count * out.typeWords, 0);
  for (std::size_t i = 0; i < count; ++i) {
    const AstFieldDesc& f = *fields.keys()[i];
    AstRegistry::Field entry{f.ptr, {}, f.offset()};
    for (const AstTypeDesc* n : narrows[i]) entry.narrows.push_back(source(n));
    out.fields.push_back(std::move(entry));
    detail::FieldDesc d;
    d.name = f.name;
    d.nature = f.nature;
    d.storage = f.storage;
    d.target = f.targetCount == 0 ? detail::kNoIndex : out.targets[from[i]];
    d.owner = types.index(f.owner);
    d.targets = out.targets.data() + from[i];
    d.target_count = static_cast<std::uint16_t>(f.targetCount);
    out.descs.push_back(d);
    if (detail::is_ref_nature(f.nature))
      for (std::uint32_t t = 0; t < f.targetCount; ++t)
        for (std::size_t w = 0; w < out.typeWords; ++w)
          out.accepts[i * out.typeWords + w] |=
              out.derives[out.targets[from[i] + t] * out.typeWords + w];
    out.anyRef = out.anyRef || detail::is_ref_nature(f.nature);
    out.anyVariantField =
        out.anyVariantField || f.nature == detail::FieldNature::variant;
  }
  out.convCount = convs.keys().size();
  return out;
}

}  // namespace cursiv
