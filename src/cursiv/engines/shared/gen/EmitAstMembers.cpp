#include "cursiv/engines/shared/gen/EmitAstMembers.hpp"

#include <algorithm>
#include <format>
#include <numeric>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>

#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/engines/shared/gen/EmitOutput.hpp"

namespace cursiv {

std::vector<std::size_t> member_order(
    std::span<const AstRegistry::Field> fields,
    std::span<const std::uint64_t> derives, std::size_t words) {
  std::vector<std::size_t> depth(derives.size() / words, 0);
  for (std::size_t base = 0; base < depth.size(); ++base)
    for (std::size_t type = 0; type < depth.size(); ++type)
      if (type != base &&
          ((derives[base * words + type / 64] >> (type % 64)) & 1u) != 0)
        ++depth[type];
  std::vector<std::size_t> order(fields.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::ranges::stable_sort(order, {}, [&](std::size_t f) {
    return std::tuple(depth[fields[f].owner], fields[f].owner,
                      fields[f].offset);
  });
  return order;
}

std::string emit_ast_members(const std::vector<std::string>& types,
                             const std::vector<std::string>& tags,
                             std::span<const AstRegistry::Field> fields,
                             std::span<const std::size_t> order,
                             std::span<const std::uint64_t> derives,
                             std::size_t words) {
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
      if (((derives[std::size_t{fields[f].owner} * words + t / 64] >>
            (t % 64)) &
           1u) == 0)
        continue;
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

}  // namespace cursiv
