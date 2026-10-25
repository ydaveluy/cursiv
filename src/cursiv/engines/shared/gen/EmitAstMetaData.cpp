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

std::string emit_ast_metadata(const AstRegistry& registry) {
  std::vector<std::string> types, tags, narrows;
  std::vector<std::ptrdiff_t> offsets;
  for (const auto& type : registry.types)
    types.push_back(gen_qualified(type.source.empty() ? type.name : type.source));
  for (std::size_t f = 0; f < registry.fields.size(); ++f) {
    const auto& field = registry.fields[f];
    const auto& owner = registry.types[registry.descs[f].owner];
    tags.push_back(owner.source.empty() ? tag_spelling(field.ptr)
        : "::cursiv::detail::field_tag<&" + owner.source + "::" +
          std::string(registry.descs[f].name) + ">");
    narrows.push_back(narrow_list(field.narrows));
    offsets.push_back(field.offset);
  }
  const std::span<const detail::FieldDesc> fields = registry.descs;
  const std::span<const std::uint64_t> derives = registry.derives;
  const std::span<const std::uint64_t> accepts = registry.accepts;
  const std::size_t words = registry.typeWords;
  const auto order = member_order(fields, offsets, derives, words);
  const auto word = [](std::uint64_t w) { return std::format("{}ull", w); };
  const auto fieldIds = std::views::iota(std::size_t{0}, fields.size());
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
      for (unsigned k = 0; k < field.target_count; ++k)
        out += std::format("{}u, ", field.targets[k]);
      if (field.target_count != 0) out += '\n';
    }
    if (registry.targets.empty()) out += "    0u,\n";
    out += "};\n";
    constexpr const char* natures[] = {"value", "node", "reference",
                                       "multi_reference", "variant"};
    constexpr const char* storages[] = {"direct", "optional", "vector"};
    at = 0;
    out += emit_table(
        "constexpr ::cursiv::detail::FieldDesc kFields[]", fields,
        [&](const detail::FieldDesc& field) {
          return std::format(
              "{{\"{}\", ::cursiv::detail::FieldNature::{}, "
              "::cursiv::detail::FieldStorage::{}, {}u, {}u, "
              "kTargets + {}u, {}u}}",
              field.name, natures[static_cast<unsigned>(field.nature)],
              storages[static_cast<unsigned>(field.storage)], field.target,
              field.owner, std::exchange(at, at + field.target_count),
              field.target_count);
        });
    out += emit_table("constexpr ::cursiv::RefOps kRefOps[]", fieldIds,
                      [&](std::size_t f) {
                        return detail::is_ref_nature(fields[f].nature)
                                   ? std::format(
                                         "::cursiv::detail::ref_ops_of<{}, "
                                         "{}>()",
                                         tags[f], narrows[f])
                                   : std::string("{}");
                      });
  }
  out += emit_table("constexpr ::std::uint64_t kAccepts[]", accepts, word);
  std::vector<std::size_t> properties;
  counts.assign(types.size(), 0);
  for (const std::size_t f : order)
    if (fields[f].owner < types.size()) ++counts[fields[f].owner];
  std::vector<std::size_t> next(types.size());
  std::size_t total = 0;
  for (std::size_t t = 0; t < types.size(); ++t)
    next[t] = std::exchange(total, total + counts[t]);
  properties.resize(total);
  for (const std::size_t f : order)
    if (fields[f].owner < types.size())
      properties[next[fields[f].owner]++] = f;
  out += emit_table(
      "constexpr ::cursiv::PropertyMetaData kProperties[]", properties,
      [&](std::size_t f) {
        const auto& field = fields[f];
        const bool ref = detail::is_ref_nature(field.nature) &&
                         field.target != detail::kNoIndex;
        return std::format(
            "{{\"{}\", {}}}", field.name,
            ref ? std::format("kTypes[{}u]", field.target) : "nullptr");
      },
      "{}");
  at = 0;
  out += emit_table(
      "constexpr ::cursiv::detail::PropertyRow kPropertyRows[]", counts,
      [&at](std::size_t count) {
        return count == 0 ? std::string("{nullptr, 0}")
                          : std::format("{{kProperties + {}u, {}u}}",
                                        std::exchange(at, at + count), count);
      });
  out += emit_table(
      "constexpr const ::cursiv::AstTypeTag* kReferenceTags[]", fieldIds,
      [&](std::size_t f) {
        return fields[f].nature == detail::FieldNature::reference
                   ? std::format("::cursiv::detail::reference_tag_of<{}>()",
                                 tags[f])
                   : std::string("nullptr");
      },
      "nullptr");
  return out + emit_ast_members(types, tags, fields, order, derives, words);
}

std::string emit_ast_metadata_record(const AstRegistry& registry) {
  return std::format(R"(constexpr ::cursiv::AstMetaData kAstMetaData{{
    .types = ::kTypes,
    .subtypes = ::kSubtypes,
    .derivesBits = ::kDerives,
    .typeCount = {0}u,
    .typeWords = {1}u,
    .fields = {2},
    .refOps = {3},
    .acceptBits = ::kAccepts,
    .properties = ::kProperties,
    .propertyRows = ::kPropertyRows,
    .referenceTags = ::kReferenceTags,
    .complete = ::kComplete,
    .memberVisitors = ::kMemberVisitors,
    .unknownMembers = &::UnknownMembers::visitUnknown,
    .referenceFields = {{::kReferenceFields, ::kReferenceFieldCount}},
    .referenceRows = ::kReferenceRows,
    .fieldCount = {4}u,
    .anyVariantField = {5},
    .anyMultiReference = ::kAnyMultiReference,
}};

)",
                     registry.types.size(), registry.typeWords,
                     registry.fields.empty() ? "nullptr" : "::kFields",
                     registry.fields.empty() ? "nullptr" : "::kRefOps",
                     registry.fields.size(), registry.anyVariantField);
}

}  // namespace cursiv
