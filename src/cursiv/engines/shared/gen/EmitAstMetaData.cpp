#include "cursiv/engines/shared/gen/EmitAstMetaData.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/EmitAstMembers.hpp"
#include "cursiv/engines/shared/gen/EmitOutput.hpp"
#include "cursiv/engines/shared/gen/TypeSpelling.hpp"

namespace cursiv {

std::string emit_ast_metadata(const AstRegistry& registry,
                              const std::vector<std::string>& tags) {
  std::vector<std::string> types, narrows;
  for (const auto& type : registry.types) types.push_back(type.source);
  for (const auto& field : registry.fields)
    narrows.push_back(narrow_list(field.narrows));
  const std::span<const AstRegistry::Field> fields = registry.fields;
  const std::span<const std::uint64_t> derives = registry.derives;
  const std::span<const std::uint64_t> accepts = registry.accepts;
  const std::size_t words = registry.typeWords;
  const auto order = member_order(fields, derives, words);
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
      if (u != t && ((derives[t * words + u / 64] >> (u % 64)) & 1u)) {
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
  out += emit_table("constexpr ::std::uint64_t kDerives[]", derives, word);

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
    out += emit_table("constexpr ::std::uint64_t kAccepts[]", accepts, word);
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
  return out + emit_ast_members(types, tags, fields, order, derives, words);
}

std::string emit_ast_metadata_record(const AstRegistry& registry) {
  const std::size_t bound = registry.reference_bound();
  const bool ref = bound != 0;
  return std::format(R"(constexpr ::cursiv::AstMetaData kAstMetaData{{
    .types = ast_detail::kTypes,
    .subtypes = ast_detail::kSubtypes,
    .derivesBits = ast_detail::kDerives,
    .typeCount = {0}u,
    .typeWords = {1}u,
    .fields = {2},
    .refOps = {3},
    .acceptBits = {4},
    .propertyRows = ast_detail::kPropertyRows,
    .referenceTags = {5},
    .complete = ast_detail::kComplete,
    .memberVisitors = ast_detail::kMemberVisitors,
    .unknownMembers = &ast_detail::UnknownMembers::visitUnknown,
    .referenceFields = {6},
    .referenceRows = {7},
    .fieldCount = {8}u,
    .referenceBound = {9}u,
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
                     ref ? "ast_detail::kReferenceRows" : "nullptr",
                     registry.fields.size(), bound);
}

}  // namespace cursiv
