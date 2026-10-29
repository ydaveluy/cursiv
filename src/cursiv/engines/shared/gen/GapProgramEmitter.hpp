#pragma once

#include <algorithm>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

template <class Cfg>
class GapProgramEmitter {
 public:
  GapProgramEmitter(const Cfg& cfg, std::string boundary,
                    std::string_view linkage)
      : _cfg(cfg),
        _boundary(std::move(boundary)),
        _linkage(linkage),
        _used(cfg.nlex) {}

  void add(std::uint16_t id) {
    if (_used[id]) return;
    _used[id] = true;
    const auto& e = _cfg.lex[id];
    if (e.kind == LexKind::seq || e.kind == LexKind::alt)
      for (unsigned i = 0; i < e.b; ++i) add(_cfg.lexKid[e.a + i]);
    else if (e.kind == LexKind::rep && e.max != 0)
      add(e.a);
    _order.push_back(id);
  }

  // Whether the source reads the boundary: a class test and a guard do, no
  // other node of a program does.
  bool reads_boundary() const {
    return std::ranges::any_of(_order, [&](std::uint16_t id) {
      const LexKind kind = _cfg.lex[id].kind;
      return kind == LexKind::chars || kind == LexKind::nguard;
    });
  }

  std::string expression(std::uint16_t id, bool whole, std::string_view text,
                         std::string_view size) const {
    return std::format(
        "::cursiv::gap_matches<{}>({}, {}, [&](const auto& in) {{ return "
        "gap_pattern_{}({}, {}, in); }})",
        whole, text, size, id, text, size);
  }

  std::string source() const {
    std::string out;
    for (const auto id : _order) {
      const auto& e = _cfg.lex[id];
      out += std::format(
          "template <class Set>\n{} Set gap_pattern_{}(const char* g, "
          "::cursiv::TextOffset n, const Set& in) {{\n"
          "  (void)g; (void)n; (void)in;\n",
          _linkage, id);
      switch (e.kind) {
        case LexKind::chars:
          out += std::format(
              "  Set out;\n  in.each([&](::cursiv::TextOffset p) {{\n"
              "    if (p < n && {}.at({}u, g, p, n)) out.set(p + 1);\n"
              "  }});\n  return out;\n",
              _boundary, e.a);
          break;
        case LexKind::nguard:
          out += "  Set out;\n  in.each([&](::cursiv::TextOffset p) {\n";
          if (e.min != 0)
            out += std::format(
                "    if ({}u > n - p) {{ out.set(p); return; }}\n", e.min);
          for (unsigned k = 0; k < e.min; ++k)
            out += std::format(
                "    if (!{}.at({}u, g, p + {}u, n)) {{ out.set(p); return; "
                "}}\n",
                _boundary, _cfg.lexKid[e.b + k], k);
          if (e.a != kNoSet)
            out += std::format(
                "    if (p + {0}u < n && {1}.at({2}u, g, p + {0}u, n)) "
                "out.set(p);\n",
                e.min, _boundary, e.a);
          out += "  });\n  return out;\n";
          break;
        case LexKind::seq:
          out += "  auto cur = in;\n";
          for (unsigned k = 0; k < e.b; ++k)
            out += std::format(
                "  if (cur.empty()) return cur;\n"
                "  cur = gap_pattern_{}(g, n, cur);\n",
                _cfg.lexKid[e.a + k]);
          out += "  return cur;\n";
          break;
        case LexKind::alt:
          out += "  Set out;\n";
          for (unsigned k = 0; k < e.b; ++k)
            out += std::format("  out.unite(gap_pattern_{}(g, n, in));\n",
                               _cfg.lexKid[e.a + k]);
          out += "  return out;\n";
          break;
        case LexKind::rep:
          if (e.max == 0)
            out += "  return in;\n";
          else
            out += std::format(
                "  return ::cursiv::gap_repeat<{}u, {}u>(in, [&](const Set& "
                "cur) {{\n"
                "    return gap_pattern_{}(g, n, cur);\n  }});\n",
                e.min, e.max, e.a);
          break;
        default:
          throw std::logic_error("cursiv: nonregular node in a gap program");
      }
      out += "}\n";
    }
    return out;
  }

 private:
  const Cfg& _cfg;
  std::string _boundary;
  std::string_view _linkage;
  std::vector<bool> _used;
  std::vector<std::uint16_t> _order;
};

}  // namespace cursiv
