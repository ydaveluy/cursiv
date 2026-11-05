#include "cursiv/shared/gen/EmitAstMetaData.hpp"

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

#include "cursiv/shared/gen/Concat.hpp"
#include "cursiv/shared/gen/EmitTable.hpp"
#include "cursiv/shared/gen/TypeSpelling.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/text/TextBuilder.hpp"

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
  TextBuilder out;
  const auto access = [&](std::string_view name, std::string_view type,
                          std::span<const std::size_t> members) {
    out.line("using ", name, " = ::cursiv::AstMemberAccess<", type,
             ", ::cursiv::TypeList<",
             joined(members, ", ", [&](std::size_t f) { return tags[f]; }),
             ">>;");
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
    access(concat("Members", t), types[t], members);
  }
  const auto typeIds = std::views::iota(std::size_t{0}, types.size());
  const auto number = [](std::size_t f) { return concat(f, "u"); };
  out.write(emit_table(
      "constexpr ::cursiv::detail::YieldFn kMemberVisitors[]", typeIds,
      [](std::size_t t) { return concat("&Members", t, "::visit"); },
      "nullptr"));
  out.write(emit_table(
      "constexpr ::cursiv::detail::NewNodeFn kNewNodes[]", typeIds,
      [&](std::size_t t) -> std::string {
        return registry.types[t].built
                   ? concat("&::cursiv::detail::new_ast_node<", types[t], ">")
                   : "nullptr";
      },
      "nullptr"));
  // A grammar with no reference member keeps no reference rows.
  if (!refFields.empty()) {
    out.write(emit_table("constexpr ::std::uint16_t kReferenceFields[]",
                         refFields, number));
    std::vector<std::size_t> typed;
    for (const auto& row : references)
      typed.insert(typed.end(), row.begin(), row.end());
    out.write(emit_table("constexpr ::std::uint16_t kTypeReferenceFields[]",
                         typed, number));
    std::size_t at = 0;
    out.write(emit_table(
        "constexpr ::std::span<const ::std::uint16_t> kReferenceRows[]",
        references, [&at](const std::vector<std::size_t>& row) {
          return concat("{kTypeReferenceFields + ",
                        std::exchange(at, at + row.size()), "u, ", row.size(),
                        "u}");
        }));
    out.line("constexpr ::std::size_t kReferenceFieldCount = ",
             refFields.size(), "u;");
  }
  out.line("constexpr bool kAnyMultiReference = ", multi, ";");
  return out.take();
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
  const auto word = [](std::uint64_t w) { return concat(w, "ull"); };
  TextBuilder out;
  out.write(emit_table("constexpr const ::cursiv::AstTypeTag* kTypes[]", types,
                       [](const std::string& type) {
                         return concat("&::cursiv::ast_type_tag<", type, ">");
                       }));
  out.line("constexpr const ::cursiv::AstTypeTag* kSubtypeTags[] = {");
  std::vector<std::size_t> counts(types.size(), 1);
  for (std::size_t t = 0; t < types.size(); ++t) {
    out.write("    kTypes[", t, "u],");
    for (std::size_t u = 0; u < types.size(); ++u)
      if (u != t && registry.subtype(t, u)) {
        out.write("kTypes[", u, "u],");
        ++counts[t];
      }
    out.write('\n');
  }
  out.line("};");
  std::size_t at = 0;
  out.write(emit_table("constexpr ::cursiv::detail::TagSet kSubtypes[]", counts,
                       [&at](std::size_t count) {
                         return concat("{kSubtypeTags + ",
                                       std::exchange(at, at + count), "u, ",
                                       count, "u}");
                       }));
  out.write(emit_table("constexpr ::std::uint64_t kDerives[]", registry.derives,
                       word));

  if (!fields.empty()) {
    out.line("constexpr ::std::uint16_t kTargets[] = {");
    for (const auto& field : fields) {
      for (const std::uint16_t target : registry.targets_of(field))
        out.write(target, "u, ");
      if (field.targetCount != 0) out.write('\n');
    }
    if (registry.targets.empty()) out.line("    0u,");
    out.line("};");
    constexpr const char* natures[] = {"value", "node", "reference",
                                       "multi_reference", "variant"};
    constexpr const char* storages[] = {"direct", "optional", "vector"};
    out.write(emit_table(
        "constexpr ::cursiv::detail::FieldDesc kFields[]", fields,
        [&](const AstRegistry::Field& field) {
          return concat("{\"", field.name,
                        "\", ::cursiv::detail::FieldNature::",
                        natures[static_cast<unsigned>(field.nature)],
                        ", "
                        "::cursiv::detail::FieldStorage::",
                        storages[static_cast<unsigned>(field.storage)], ", ",
                        registry.head_of(field), "u, ", field.owner,
                        "u, "
                        "kTargets + ",
                        field.firstTarget, "u, ", field.targetCount, "u}");
        }));
  }
  if (const std::size_t bound = registry.reference_bound(); bound != 0) {
    const auto rows = std::views::iota(std::size_t{0}, bound);
    out.write(emit_table("constexpr ::cursiv::RefOps kRefOps[]", rows,
                         [&](std::size_t f) {
                           return detail::is_ref_nature(fields[f].nature)
                                      ? concat("::cursiv::detail::ref_ops_of<",
                                               tags[f], ", ", narrows[f], ">()")
                                      : std::string("{}");
                         }));
    out.write(emit_table("constexpr ::std::uint64_t kAccepts[]",
                         registry.accepts, word));
    out.write(
        emit_table("constexpr const ::cursiv::AstTypeTag* kReferenceTags[]",
                   rows, [&](std::size_t f) {
                     return fields[f].nature == detail::FieldNature::reference
                                ? concat("::cursiv::detail::reference_tag_of<",
                                         tags[f], ">()")
                                : std::string("nullptr");
                   }));
  }
  // `order` keeps each class's members together: a class's row is its run.
  std::vector<std::size_t> starts(types.size());
  counts.assign(types.size(), 0);
  for (std::size_t k = 0; k < order.size(); ++k) {
    const std::uint16_t owner = fields[order[k]].owner;
    if (counts[owner]++ == 0) starts[owner] = k;
  }
  if (!order.empty())
    out.write(emit_table(
        "constexpr ::cursiv::PropertyMetaData kProperties[]", order,
        [&](std::size_t f) {
          const auto& field = fields[f];
          const std::uint16_t target = registry.head_of(field);
          const bool ref = detail::is_ref_nature(field.nature) &&
                           target != detail::kNoIndex;
          return concat("{\"", field.name, "\", ",
                        ref ? std::format("kTypes[{}u]", target) : "nullptr",
                        "}");
        }));
  const auto typeIds = std::views::iota(std::size_t{0}, types.size());
  out.write(
      emit_table("constexpr ::cursiv::detail::PropertyRow kPropertyRows[]",
                 typeIds, [&](std::size_t t) {
                   return counts[t] == 0 ? std::string("{nullptr, 0}")
                                         : concat("{kProperties + ", starts[t],
                                                  "u, ", counts[t], "u}");
                 }));
  return out.write(emit_ast_members(registry, types, tags, order)).take();
}

std::string emit_ast_metadata_record(const AstRegistry& registry) {
  const std::size_t bound = registry.reference_bound();
  const bool ref = bound != 0;
  return concat(R"(constexpr ::cursiv::AstMetaData kAstMetaData{
    .types = ast_detail::kTypes,
    .subtypes = ast_detail::kSubtypes,
    .derivesBits = ast_detail::kDerives,
    .typeCount = )",
                registry.types.size(), R"(u,
    .typeWords = )",
                registry.typeWords, R"(u,
    .fields = )",
                registry.fields.empty() ? "nullptr" : "ast_detail::kFields",
                R"(,
    .refOps = )",
                ref ? "ast_detail::kRefOps" : "nullptr", R"(,
    .acceptBits = )",
                ref ? "ast_detail::kAccepts" : "nullptr", R"(,
    .propertyRows = ast_detail::kPropertyRows,
    .referenceTags = )",
                ref ? "ast_detail::kReferenceTags" : "nullptr", R"(,
    .memberVisitors = ast_detail::kMemberVisitors,
    .newNodes = ast_detail::kNewNodes,
    .unknownMembers = &ast_detail::UnknownMembers::visitUnknown,
    .referenceFields = )",
                ref ? "{ast_detail::kReferenceFields, "
                      "ast_detail::kReferenceFieldCount}"
                    : "{}",
                R"(,
    .referenceRows = )",
                ref ? "ast_detail::kReferenceRows" : "nullptr", R"(,
    .fieldCount = )",
                registry.fields.size(), R"(u,
    .referenceBound = )",
                bound, R"(u,
    .anyMultiReference = ast_detail::kAnyMultiReference,
};

)");
}

}  // namespace cursiv
