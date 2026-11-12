// How every emitter writes a table and the labels of a `switch` arm.
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

// `case` labels several a line, then the arm they read where one is given:
// the compiler folds a run of labels as a range.
template <class Each>
void case_rows(TextBuilder& out, Each&& each, std::string_view arm = {}) {
  std::string row = "   ";
  const auto put = [&](std::string_view part) {
    if (row.size() + 1 + part.size() > 80) {
      out.line(row);
      row = "   ";
    }
    row += ' ';
    row += part;
  };
  each([&](unsigned v) { put(concat("case ", v, ":")); });
  if (!arm.empty()) put(arm);
  out.line(row);
}

}  // namespace cursiv::engine
