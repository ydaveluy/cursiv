// A converter's whole variant replaces the one the member holds, and an
// alternative that cannot be assigned leaves the variant without an assignment.
// expect LR: this member's type cannot be assigned
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "refusals/head.hpp"

namespace refuse {

namespace frozen_pick {
struct ConstText {
  const std::string text;
  explicit ConstText(std::string value) : text(std::move(value)) {}
};
inline std::variant<ConstText, std::int64_t> pick(std::string_view text) {
  return ConstText(std::string(text));
}
struct Node : cursiv::Extends<cursiv::AstNode> {
  variant<ConstText, int64> value{int64{0}};
};
}  // namespace frozen_pick
struct G : Base {
  struct NAME : terminal<as<&frozen_pick::pick>(some(cr<"a-z">))> {};
  struct Root : rule<create<frozen_pick::Node>(
                    assign<&frozen_pick::Node::value>(ref<NAME>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace refuse
