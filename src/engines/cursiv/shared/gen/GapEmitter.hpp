// Trivia queries compile to fixed-pattern checks or native position transforms.
// Every gap a condition asks, a phrase's own included, is emitted once at
// construction.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/Concat.hpp"
#include "cursiv/shared/gen/EmitTable.hpp"
#include "cursiv/shared/table/GapLeaf.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/text/TextBuilder.hpp"

namespace cursiv::engine {

// A gap program as native position transforms, one function per node.
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
    if (const auto& e = _cfg.lex[id];
        e.kind == LexKind::seq || e.kind == LexKind::alt)
      for (unsigned i = 0; i < e.b; ++i) add(_cfg.lexChild[e.a + i]);
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
    return concat("::cursiv::engine::gap_matches<", whole, ">(", text, ", ",
                  size,
                  ", [&](const auto& in) { return "
                  "gap_pattern_",
                  id, "(", text, ", ", size, ", in); })");
  }

  std::string source() const {
    TextBuilder out;
    for (const auto id : _order) {
      const auto& e = _cfg.lex[id];
      out.line("template <class Set>\n", _linkage, " Set gap_pattern_", id,
               "(const char* g, "
               "::cursiv::TextOffset n, const Set& in) {\n"
               "  (void)g; (void)n; (void)in;");
      switch (e.kind) {
        case LexKind::chars:
          out.line(
              "  Set out;\n  in.each([&](::cursiv::TextOffset p) {\n"
              "    if (p < n && ",
              _boundary, ".at(", e.a,
              "u, g, p, n)) out.set(p + 1);\n"
              "  });\n  return out;");
          break;
        case LexKind::nguard:
          out.line("  Set out;\n  in.each([&](::cursiv::TextOffset p) {");
          if (e.min != 0)
            out.line("    if (", e.min, "u > n - p) { out.set(p); return; }");
          for (unsigned k = 0; k < e.min; ++k)
            out.line("    if (!", _boundary, ".at(", _cfg.lexChild[e.b + k],
                     "u, g, p + ", k,
                     "u, n)) { out.set(p); return; "
                     "}");
          if (e.a != kNoSet)
            out.line("    if (p + ", e.min, "u < n && ", _boundary, ".at(", e.a,
                     "u, g, p + ", e.min,
                     "u, n)) "
                     "out.set(p);");
          out.line("  });\n  return out;");
          break;
        case LexKind::seq:
          out.line("  auto cur = in;");
          for (unsigned k = 0; k < e.b; ++k)
            out.line(
                "  if (cur.empty()) return cur;\n"
                "  cur = gap_pattern_",
                _cfg.lexChild[e.a + k], "(g, n, cur);");
          out.line("  return cur;");
          break;
        case LexKind::alt:
          out.line("  Set out;");
          for (unsigned k = 0; k < e.b; ++k)
            out.line("  out.unite(gap_pattern_", _cfg.lexChild[e.a + k],
                     "(g, n, in));");
          out.line("  return out;");
          break;
        case LexKind::rep:
          if (e.max == 0)
            out.line("  return in;");
          else
            out.line("  return ::cursiv::engine::gap_repeat<", e.min, "u, ",
                     e.max,
                     "u>(in, [&](const Set& "
                     "cur) {\n"
                     "    return gap_pattern_",
                     e.a, "(g, n, cur);\n  });");
          break;
        default:
          throw std::logic_error("cursiv: nonregular node in a gap program");
      }
      out.line("}");
    }
    return out.take();
  }

 private:
  const Cfg& _cfg;
  std::string _boundary;
  std::string_view _linkage;
  std::vector<bool> _used;
  std::vector<std::uint16_t> _order;
};

template <class Cfg>
class GapEmitter : private TextBuilder {
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

  std::string source() const { return text() + _program.source(); }

  // Whether the source names the boundary the emitter was given: a gap that
  // reads only the sets, or has a constant answer, does not.
  bool reads_boundary() const { return _program.reads_boundary(); }

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
        return concat("(", size, " == 0)");
      case How::patterns:
        return concat(name(*at), "(", text, ", ", size, ")");
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
    return concat("gap_", a.whole ? "is" : "has", "_", a.node);
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
        for (unsigned i = 0; i < e.b; ++i) phrase(_cfg.lexChild[e.a + i]);
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
    if (const auto& e = _cfg.lex[node]; e.kind == LexKind::alt) {
      for (unsigned i = 0; i < e.b; ++i)
        if (!gap_patterns(_cfg.lexChild[e.a + i], patterns)) return false;
      return patterns.size() <= 16;
    }
    GapWord bytes;
    const auto sequence = [&](auto&& self, std::uint16_t n) {
      const auto& part = _cfg.lex[n];
      if (part.kind == LexKind::chars) {
        bytes.push_back(part.a);
        return bytes.size() <= 16;
      }
      if (part.kind != LexKind::seq) return false;
      for (unsigned i = 0; i < part.b; ++i)
        if (!self(self, _cfg.lexChild[part.a + i])) return false;
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
      // The search tries only character starts, so a pattern that may begin on
      // a continuation byte, 0x80 to 0xBF, covers nothing past a start.
      const auto covers = [&](std::uint16_t pe, std::uint16_t qe) {
        return _cfg.sets[qe].covers(_cfg.sets[pe]);
      };
      for (std::size_t i = patterns.size(); i-- > 0;) {
        bool covered = false;
        const auto& p = patterns[i];
        for (std::size_t j = 0; j < patterns.size() && !covered; ++j) {
          const auto& q = patterns[j];
          const auto at = std::ranges::search(p, q, covers);
          covered = (q.size() < p.size() || (q.size() == p.size() && j < i)) &&
                    !at.empty() &&
                    (at.begin() == p.begin() || _cfg.sets[q[0]].w[2] == 0);
        }
        if (covered) patterns.erase(patterns.begin() + i);
      }
    }
    line(_linkage, " bool ", name({node, whole, How::patterns}),
         "(const char* g, ::cursiv::TextOffset n) {");
    if (!whole)
      line(
          "  for (::cursiv::TextOffset p = 0; p < n;\n"
          "       p += ::cursiv::engine::gap_width(g + p, n - p)) {");
    else
      line("  constexpr ::cursiv::TextOffset p = 0;");
    for (const auto& bytes : patterns) {
      write("    if (", whole ? "n" : "n - p", " ", whole ? "==" : ">=", " ",
            bytes.size(), "u");
      for (std::size_t b = 0; b < bytes.size(); ++b)
        write(" && ", _sets, "[", bytes[b],
              "].has("
              "static_cast<unsigned char>(g[p + ",
              b, "u]))");
      line(") return true;");
    }
    if (!whole) line("  }");
    line("  return false;\n}");
    return How::patterns;
  }

  const Cfg& _cfg;
  std::string _sets;
  std::string_view _linkage;
  GapProgramEmitter<Cfg> _program;
  std::vector<bool> _walked;
  std::vector<Asked> _asked;
};

}  // namespace cursiv::engine
