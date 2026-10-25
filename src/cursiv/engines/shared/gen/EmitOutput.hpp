// The text an emitter writes, with the spelling primitives every emitter shares.
#pragma once

#include <format>
#include <functional>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace cursiv {

struct EmitOutput {
  std::string out;

  void raw(std::string_view s) { out += s; }
  void line(std::string_view s) {
    out += s;
    out += '\n';
  }
  void num(unsigned long long v) { out += std::to_string(v); }
  template <class... A>
  void fmt(std::format_string<A...> f, A&&... args) {
    std::format_to(std::back_inserter(out), f, std::forward<A>(args)...);
  }
};

// The cells of `items`, `sep` between two.
template <class Items, class Cell = std::identity>
std::string join(const Items& items, std::string_view sep, Cell cell = {}) {
  std::string out;
  bool first = true;
  for (const auto& item : items) {
    if (!std::exchange(first, false)) out += sep;
    out += cell(item);
  }
  return out;
}

// `decl = {`, a line per cell of `items`, `};`; `empty` is the one cell of a
// table with none.
template <class Items, class Cell = std::identity>
std::string emit_table(std::string_view decl, const Items& items,
                       Cell cell = {}, std::string_view empty = {}) {
  std::string out = std::format("{} = {{\n", decl);
  for (const auto& item : items) out += std::format("    {},\n", cell(item));
  if (std::ranges::empty(items) && !empty.empty())
    out += std::format("    {},\n", empty);
  return out + "};\n";
}

}  // namespace cursiv
