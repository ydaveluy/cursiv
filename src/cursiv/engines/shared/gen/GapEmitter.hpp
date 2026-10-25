// Trivia queries compile to fixed-pattern checks or native position transforms.
// Every gap a condition asks, a phrase's own included, is laid once at
// construction.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/EmitOutput.hpp"
#include "cursiv/engines/shared/gen/GapProgramEmitter.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"

namespace cursiv {

template <class Cfg>
class GapEmitter : EmitOutput {
 public:
  GapEmitter(const Cfg& cfg, std::string sets, std::string boundary,
             std::string_view linkage = "static")
      : _cfg(cfg),
        _sets(std::move(sets)),
        _linkage(linkage),
        _program(cfg, std::move(boundary), linkage),
        _walked(cfg.nlex) {
    for (const GapLeaf& leaf : cfg.gapLeaves.v)
      if (leaf.kind == GapLeafKind::has || leaf.kind == GapLeafKind::is)
        ask(leaf.node, leaf.kind == GapLeafKind::is);
      else if (leaf.reads_tokens())
        phrase(leaf.node);
  }

  std::string source() const { return out + _program.source(); }

  // Whether the `size` bytes at `text` hold gap `node`, or are it, `whole`.
  std::string expression(std::uint16_t node, bool whole,
                         std::string_view text = "g",
                         std::string_view size = "n") const {
    const auto at = std::ranges::find_if(_asked, [&](const Asked& a) {
      return a.node == node && a.whole == whole;
    });
    switch (at->how) {
      case How::never:
        return "false";
      case How::always:
        return "true";
      case How::empty:
        return std::format("({} == 0)", size);
      case How::patterns:
        return std::format("{}({}, {})", name(*at), text, size);
      case How::program:
        break;
    }
    return _program.expression(node, whole, text, size);
  }
  std::string expression(unsigned leaf) const {
    return expression(_cfg.gapLeaves[leaf].node,
                      _cfg.gapLeaves[leaf].kind == GapLeafKind::is);
  }

 private:
  enum class How : std::uint8_t { never, always, empty, patterns, program };
  struct Asked {
    std::uint16_t node;
    bool whole;
    How how;
  };

  static std::string name(const Asked& a) {
    return std::format("gap_{}_{}", a.whole ? "is" : "has", a.node);
  }

  void ask(std::uint16_t node, bool whole) {
    if (std::ranges::any_of(_asked, [&](const Asked& a) {
          return a.node == node && a.whole == whole;
        }))
      return;
    Asked a{node, whole, simple_gap(node, whole)};
    if (a.how == How::program) _program.add(node);
    _asked.push_back(a);
  }
  // The gaps a phrase asks where it stands.
  void phrase(std::uint16_t at) {
    if (_walked[at]) return;
    _walked[at] = true;
    const LexNode& e = _cfg.lex[at];
    switch (e.kind) {
      case LexKind::gap_has:
      case LexKind::gap_is:
        return ask(e.a, e.kind == LexKind::gap_is);
      case LexKind::seq:
      case LexKind::alt:
        for (unsigned i = 0; i < e.b; ++i) phrase(_cfg.lexKid[e.a + i]);
        return;
      case LexKind::rep:
        if (e.max != 0) phrase(e.a);
        return;
      case LexKind::positive:
      case LexKind::negative:
        return phrase(e.a);
      default:
        return;
    }
  }

  using GapWord = std::vector<std::uint16_t>;
  bool gap_patterns(std::uint16_t node, std::vector<GapWord>& patterns) const {
    const auto& e = _cfg.lex[node];
    if (e.kind == LexKind::alt) {
      for (unsigned i = 0; i < e.b; ++i)
        if (!gap_patterns(_cfg.lexKid[e.a + i], patterns)) return false;
      return patterns.size() <= 16;
    }
    GapWord bytes;
    const auto sequence = [&](auto&& self, std::uint16_t n) -> bool {
      const auto& part = _cfg.lex[n];
      if (part.kind == LexKind::chars) {
        bytes.push_back(part.a);
        return bytes.size() <= 16;
      }
      if (part.kind != LexKind::seq) return false;
      for (unsigned i = 0; i < part.b; ++i)
        if (!self(self, _cfg.lexKid[part.a + i])) return false;
      return true;
    };
    if (!sequence(sequence, node)) return false;
    patterns.push_back(std::move(bytes));
    return patterns.size() <= 16;
  }

  How simple_gap(std::uint16_t node, bool whole) {
    std::vector<GapWord> patterns;
    if (!gap_patterns(node, patterns)) return How::program;
    if (patterns.empty()) return How::never;
    if (std::ranges::any_of(patterns,
                            [](const auto& p) { return p.empty(); })) {
      if (!whole) return How::always;
      if (patterns.size() == 1) return How::empty;
    }
    if (!whole) {
      // A search for CR already includes CRLF, wherever it occurs in the gap.
      const auto covers = [&](std::uint16_t pe, std::uint16_t qe) {
        return _cfg.sets[qe].covers(_cfg.sets[pe]);
      };
      for (std::size_t i = patterns.size(); i-- > 0;) {
        bool covered = false;
        const auto& p = patterns[i];
        for (std::size_t j = 0; j < patterns.size() && !covered; ++j) {
          const auto& q = patterns[j];
          covered = (q.size() < p.size() || (q.size() == p.size() && j < i)) &&
                    !std::ranges::search(p, q, covers).empty();
        }
        if (covered) patterns.erase(patterns.begin() + i);
      }
    }
    fmt("{} bool {}(const char* g, ::cursiv::TextOffset n) {{\n", _linkage,
        name({node, whole, How::patterns}));
    if (!whole)
      raw("  for (::cursiv::TextOffset p = 0; p < n;\n"
          "       p += ::cursiv::gap_width(g + p, n - p)) {\n");
    else
      raw("  constexpr ::cursiv::TextOffset p = 0;\n");
    for (const auto& bytes : patterns) {
      fmt("    if ({} {} {}u", whole ? "n" : "n - p",
          whole ? "==" : ">=", bytes.size());
      for (std::size_t b = 0; b < bytes.size(); ++b)
        fmt(" && {}[{}].has("
            "static_cast<unsigned char>(g[p + {}u]))",
            _sets, bytes[b], b);
      raw(") return true;\n");
    }
    if (!whole) raw("  }\n");
    raw("  return false;\n}\n");
    return How::patterns;
  }

  const Cfg& _cfg;
  std::string _sets;
  std::string_view _linkage;
  GapProgramEmitter<Cfg> _program;
  std::vector<bool> _walked;
  std::vector<Asked> _asked;
};

}  // namespace cursiv
