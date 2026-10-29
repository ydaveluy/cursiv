#include "cursiv/engines/shared/gen/EmitAstMetaData.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numeric>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/engines/shared/gen/EmitOutput.hpp"
#include "cursiv/engines/shared/gen/TypeSpelling.hpp"

namespace cursiv {

namespace {

// The fields in the order the classes declare them: a base's members before
// its derived class's, each class's by position in the class.
std::vector<std::size_t> member_order(const AstRegistry& registry) {
  const auto& fields = registry.fields;
  std::vector<std::size_t> depth(registry.types.size(), 0);
  for (std::size_t base = 0; base < depth.size(); ++base)
    for (std::size_t type = 0; type < depth.size(); ++type)
      if (type != base && registry.subtype(base, type)) ++depth[type];
  std::vector<std::size_t> order(fields.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::ranges::stable_sort(order, {}, [&](std::size_t f) {
    return std::tuple(depth[fields[f].owner], fields[f].owner,
                      fields[f].offset);
  });
  return order;
}

// The member-access aliases of each node type: what the generated module
// names to reach a type's fields and references.
std::string emit_ast_members(const AstRegistry& registry,
                             const std::vector<std::string>& types,
                             const std::vector<std::string>& tags,
                             std::span<const std::size_t> order) {
  const auto& fields = registry.fields;
  std::string out;
  const auto access = [&](std::string_view name, std::string_view type,
                          std::span<const std::size_t> members) {
    out += std::format(
        "using {} = ::cursiv::AstMemberAccess<{}, ::cursiv::TypeList<{}>>;\n",
        name, type,
        join(members, ", ", [&](std::size_t f) { return tags[f]; }));
  };
  std::vector<std::vector<std::size_t>> references(types.size());
  std::vector<std::size_t> refFields;
  bool multi = false;
  for (std::size_t f = 0; f < fields.size(); ++f) {
    if (detail::is_ref_nature(fields[f].nature)) refFields.push_back(f);
    multi |= detail::is_multi_ref_nature(fields[f].nature);
  }
  access("UnknownMembers", "void", order);
  for (std::size_t t = 0; t < types.size(); ++t) {
    std::vector<std::size_t> members;
    for (const std::size_t f : order) {
      if (!registry.subtype(fields[f].owner, t)) continue;
      members.push_back(f);
      if (detail::is_ref_nature(fields[f].nature)) references[t].push_back(f);
    }
    access(std::format("Members{}", t), types[t], members);
  }
  const auto typeIds = std::views::iota(std::size_t{0}, types.size());
  const auto number = [](std::size_t f) { return std::format("{}u", f); };
  out += emit_table(
      "constexpr ::cursiv::detail::WrittenFn kComplete[]", typeIds,
      [](std::size_t t) { return std::format("&Members{}::complete", t); },
      "nullptr");
  out += emit_table(
      "constexpr ::cursiv::detail::YieldFn kMemberVisitors[]", typeIds,
      [](std::size_t t) { return std::format("&Members{}::visit", t); },
      "nullptr");
  // A grammar with no reference member keeps no reference rows.
  if (!refFields.empty()) {
    out += emit_table("constexpr ::std::uint16_t kReferenceFields[]",
                      refFields, number);
    std::vector<std::size_t> typed;
    for (const auto& row : references)
      typed.insert(typed.end(), row.begin(), row.end());
    out += emit_table("constexpr ::std::uint16_t kTypeReferenceFields[]",
                      typed, number);
    std::size_t at = 0;
    out += emit_table(
        "constexpr ::std::span<const ::std::uint16_t> kReferenceRows[]",
        references,
        [&at](const std::vector<std::size_t>& row) {
          return std::format("{{kTypeReferenceFields + {}u, {}u}}",
                             std::exchange(at, at + row.size()), row.size());
        });
    out += std::format("constexpr ::std::size_t kReferenceFieldCount = {}u;\n",
                       refFields.size());
  }
  out += std::format("constexpr bool kAnyMultiReference = {};\n", multi);
  return out;
}

}  // namespace

std::string emit_ast_metadata(const AstRegistry& registry,
                              const std::vector<std::string>& tags) {
  std::vector<std::string> types, narrows;
  for (const auto& type : registry.types) types.push_back(type.source);
  for (const auto& field : registry.fields)
    narrows.push_back(narrow_list(field.narrows));
  const std::span<const AstRegistry::Field> fields = registry.fields;
  const auto order = member_order(registry);
  const auto word = [](std::uint64_t w) { return std::format("{}ull", w); };
  std::string out =
      emit_table("constexpr const ::cursiv::AstTypeTag* kTypes[]", types,
                 [](const std::string& type) {
                   return std::format("&::cursiv::ast_type_tag<{}>", type);
                 });
  out += "constexpr const ::cursiv::AstTypeTag* kSubtypeTags[] = {\n";
  std::vector<std::size_t> counts(types.size(), 1);
  for (std::size_t t = 0; t < types.size(); ++t) {
    out += std::format("    kTypes[{}u],", t);
    for (std::size_t u = 0; u < types.size(); ++u)
      if (u != t && registry.subtype(t, u)) {
        out += std::format("kTypes[{}u],", u);
        ++counts[t];
      }
    out += '\n';
  }
  out += "};\n";
  std::size_t at = 0;
  out += emit_table("constexpr ::cursiv::detail::TagSet kSubtypes[]", counts,
                    [&at](std::size_t count) {
                      return std::format("{{kSubtypeTags + {}u, {}u}}",
                                         std::exchange(at, at + count), count);
                    });
  out += emit_table("constexpr ::std::uint64_t kDerives[]", registry.derives,
                    word);

  if (!fields.empty()) {
    out += "constexpr ::std::uint16_t kTargets[] = {\n";
    for (const auto& field : fields) {
      for (const std::uint16_t target : registry.targets_of(field))
        out += std::format("{}u, ", target);
      if (field.targetCount != 0) out += '\n';
    }
    if (registry.targets.empty()) out += "    0u,\n";
    out += "};\n";
    constexpr const char* natures[] = {"value", "node", "reference",
                                       "multi_reference", "variant"};
    constexpr const char* storages[] = {"direct", "optional", "vector"};
    out += emit_table(
        "constexpr ::cursiv::detail::FieldDesc kFields[]", fields,
        [&](const AstRegistry::Field& field) {
          return std::format(
              "{{\"{}\", ::cursiv::detail::FieldNature::{}, "
              "::cursiv::detail::FieldStorage::{}, {}u, {}u, "
              "kTargets + {}u, {}u}}",
              field.name, natures[static_cast<unsigned>(field.nature)],
              storages[static_cast<unsigned>(field.storage)],
              registry.head_of(field), field.owner, field.firstTarget,
              field.targetCount);
        });
  }
  if (const std::size_t bound = registry.reference_bound(); bound != 0) {
    const auto rows = std::views::iota(std::size_t{0}, bound);
    out += emit_table("constexpr ::cursiv::RefOps kRefOps[]", rows,
                      [&](std::size_t f) {
                        return detail::is_ref_nature(fields[f].nature)
                                   ? std::format(
                                         "::cursiv::detail::ref_ops_of<{}, "
                                         "{}>()",
                                         tags[f], narrows[f])
                                   : std::string("{}");
                      });
    out += emit_table("constexpr ::std::uint64_t kAccepts[]",
                      registry.accepts, word);
    out += emit_table(
        "constexpr const ::cursiv::AstTypeTag* kReferenceTags[]", rows,
        [&](std::size_t f) {
          return fields[f].nature == detail::FieldNature::reference
                     ? std::format("::cursiv::detail::reference_tag_of<{}>()",
                                   tags[f])
                     : std::string("nullptr");
        });
  }
  // `order` keeps each class's members together: a class's row is its run.
  std::vector<std::size_t> starts(types.size());
  counts.assign(types.size(), 0);
  for (std::size_t k = 0; k < order.size(); ++k) {
    const std::uint16_t owner = fields[order[k]].owner;
    if (counts[owner]++ == 0) starts[owner] = k;
  }
  if (!order.empty())
    out += emit_table(
        "constexpr ::cursiv::PropertyMetaData kProperties[]", order,
        [&](std::size_t f) {
          const auto& field = fields[f];
          const std::uint16_t target = registry.head_of(field);
          const bool ref = detail::is_ref_nature(field.nature) &&
                           target != detail::kNoIndex;
          return std::format(
              "{{\"{}\", {}}}", field.name,
              ref ? std::format("kTypes[{}u]", target) : "nullptr");
        });
  const auto typeIds = std::views::iota(std::size_t{0}, types.size());
  out += emit_table(
      "constexpr ::cursiv::detail::PropertyRow kPropertyRows[]", typeIds,
      [&](std::size_t t) {
        return counts[t] == 0 ? std::string("{nullptr, 0}")
                              : std::format("{{kProperties + {}u, {}u}}",
                                            starts[t], counts[t]);
      });
  return out + emit_ast_members(registry, types, tags, order);
}

std::string emit_ast_metadata_record(const AstRegistry& registry) {
  const std::size_t bound = registry.reference_bound();
  const bool ref = bound != 0;
  return std::format(
      R"(constexpr ::cursiv::AstMetaData kAstMetaData{{
    .types = ast_detail::kTypes,
    .subtypes = ast_detail::kSubtypes,
    .derivesBits = ast_detail::kDerives,
    .typeCount = {}u,
    .typeWords = {}u,
    .fields = {},
    .refOps = {},
    .acceptBits = {},
    .propertyRows = ast_detail::kPropertyRows,
    .referenceTags = {},
    .complete = ast_detail::kComplete,
    .memberVisitors = ast_detail::kMemberVisitors,
    .unknownMembers = &ast_detail::UnknownMembers::visitUnknown,
    .referenceFields = {},
    .referenceRows = {},
    .fieldCount = {}u,
    .referenceBound = {}u,
    .anyMultiReference = ast_detail::kAnyMultiReference,
}};

)",
      registry.types.size(), registry.typeWords,
      registry.fields.empty() ? "nullptr" : "ast_detail::kFields",
      ref ? "ast_detail::kRefOps" : "nullptr",
      ref ? "ast_detail::kAccepts" : "nullptr",
      ref ? "ast_detail::kReferenceTags" : "nullptr",
      ref ? "{ast_detail::kReferenceFields, "
            "ast_detail::kReferenceFieldCount}"
          : "{}",
      ref ? "ast_detail::kReferenceRows" : "nullptr", registry.fields.size(),
      bound);
}

}  // namespace cursiv
