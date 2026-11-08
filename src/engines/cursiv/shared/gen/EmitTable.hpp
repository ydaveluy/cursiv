// The spelling of a table every emitter shares.
#pragma once

#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/shared/gen/Concat.hpp"
#include "cursiv/text/TextBuilder.hpp"

namespace cursiv::engine {

// A row as a designated initializer that spells only the fields off their
// default: the member initializers write the rest.
struct EmitFields {
  std::string text;

  template <class V>
  void operator()(std::string_view name, V v, V def, std::string_view pre = "",
                  std::string_view post = "") {
    if (v != def) text += concat(",.", name, "=", pre, v, post);
  }
  // The fields, without the comma before the first.
  std::string_view list() const {
    return std::string_view(text).substr(text.empty() ? 0 : 1);
  }
};

// `decl = {`, a line per cell of `items`, `};`; `empty` is the one cell of a
// table with none.
template <class Items, class Cell = std::identity>
std::string emit_table(std::string_view decl, const Items& items,
                       Cell cell = {}, std::string_view empty = {}) {
  TextBuilder out;
  out.open(decl, " = {");
  for (const auto& item : items) out.line(cell(item), ",");
  if (std::ranges::empty(items) && !empty.empty()) out.line(empty, ",");
  out.close("};");
  return out.take();
}

}  // namespace cursiv::engine
