// Lexical recognition compiled over bytes, with no skipper or AST.
#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/CLiteral.hpp"
#include "cursiv/engines/shared/gen/LexRecursion.hpp"
#include "cursiv/engines/shared/gen/LexRelayGen.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

template <class Cfg>
class LexicalPredicateEmitter {
 public:
  template <class Relays>
  LexicalPredicateEmitter(const Cfg& g, const Relays& relays,
                          std::string_view boundary, bool header = false)
      : _g(g),
        _boundary(boundary),
        _header(header),
        _used(g.nlex),
        _width(g.nlex, kVariableWidth),
        _first(g.nlex),
        _recursive(lex_call_ancestors(g)),
        _dispatched(g.nlex) {
    const auto add = [&](std::vector<std::uint16_t>& list, std::uint16_t node) {
      if (node == kNoLexNode || std::ranges::find(list, node) != list.end())
        return;
      list.push_back(node);
      visit(node);
    };
    for (const auto& r : relays) {
      add(_roots, r.predicate);
      add(_recognizers, r.recognition);
    }
    for (const auto id : _order) _dispatched[id] = dispatchable(id);
    for (const auto root : _roots) use(root, fixed(root));
    for (const auto root : _recognizers) use(root, false);
  }

  bool any() const { return !_roots.empty() || !_recognizers.empty(); }
  bool searches() const {
    return std::ranges::any_of(_order, [&](auto id) {
      return _g.lex[id].kind == LexKind::through_same && _g.lex[id].min == 2;
    });
  }

  std::string declarations() const {
    std::string out;
    for (const auto root : _roots)
      out += std::format(
          "{}bool lexical_predicate_{}(const char*, ::cursiv::TextOffset,\n"
          "    ::cursiv::TextOffset, ::cursiv::TextOffset&);\n",
          _header ? "inline " : "", root);
    for (const auto root : _recognizers)
      out += std::format(
          "{}::cursiv::PredicateState lexical_recognizer_{}(const char*, "
          "::cursiv::TextOffset,\n"
          "    ::cursiv::TextOffset, ::cursiv::TextOffset&);\n",
          _header ? "inline " : "", root);
    return out;
  }

  std::string source() const {
    std::string out;
    for (const auto id : _order) {
      if (recursive(id)) {
        if (const auto kind = _g.lex[id].kind;
            kind == LexKind::through_lit || kind == LexKind::through_same)
          out += node(id, true);
        continue;
      }
      if (dispatched(id)) out += first_table(id);
      if ((_used[id] & kDirect) != 0) out += direct(id);
      if ((_used[id] & kNode) != 0) out += node(id);
    }
    if (std::ranges::any_of(_order, [&](auto id) { return recursive(id); }))
      out += recursive_source();
    const auto started = [&](std::uint16_t root, std::string_view most) {
      return recursive(root)
                 ? std::format(
                       "lexical_recursive({}u, src, len, "
                       "::cursiv::LexicalState{{::cursiv::LexicalCursor{{at}}}}"
                       ", seen, captures)",
                       root)
                 : call(root,
                        "::cursiv::LexicalState{::cursiv::LexicalCursor{at}}",
                        most);
    };
    for (const auto root : _roots) {
      out += std::format(
          "{}bool lexical_predicate_{}(const char* src, ::cursiv::TextOffset "
          "len,\n"
          "    ::cursiv::TextOffset at, ::cursiv::TextOffset& seen) {{\n",
          _header ? "inline " : "", root);
      if (fixed(root))
        out += std::format("  return lexical_fixed_{}(src, len, at, seen);\n",
                           root);
      else {
        out += "  ::cursiv::LexicalCaptures captures;\n";
        out += "  return !" + started(root, "true") + ".empty();\n";
      }
      out += "}\n";
    }
    for (const auto root : _recognizers) {
      out += std::format(
          "{}::cursiv::PredicateState lexical_recognizer_{}(const char* src, "
          "::cursiv::TextOffset len,\n"
          "    ::cursiv::TextOffset at, ::cursiv::TextOffset& seen) {{\n",
          _header ? "inline " : "", root);
      out += "  ::cursiv::LexicalCaptures captures;\n";
      out += "  return ::cursiv::lexical_endpoints(" + started(root, "false") +
             ");\n";
      out += "}\n";
    }
    return out;
  }

 private:
  const Cfg& _g;
  std::string _boundary;
  bool _header;
  static constexpr std::uint8_t kVisited = 1, kDirect = 2, kNode = 4,
                                kFinal = 8;
  std::vector<std::uint8_t> _used;
  static constexpr std::uint32_t kVariableWidth = 0xffffffffu;
  std::vector<std::uint32_t> _width;
  mutable std::vector<std::optional<std::pair<ByteSet, bool>>> _first;
  std::vector<std::uint8_t> _recursive;
  std::vector<std::uint8_t> _dispatched;
  std::vector<std::uint16_t> _roots, _recognizers, _order;

  bool recursive(std::uint16_t id) const {
    return !_recursive.empty() && _recursive[id];
  }

  const std::pair<ByteSet, bool>& first_bytes(std::uint16_t node) const {
    auto& slot = _first[node];
    if (!slot) slot = lex_first_bytes(_g, node);
    return *slot;
  }

  void visit(std::uint16_t id) {
    if (_used[id]) return;
    _used[id] = kVisited;
    const LexNode& e = _g.lex[id];
    switch (e.kind) {
      case LexKind::chars:
      case LexKind::nguard:
      case LexKind::same:
      case LexKind::seq:
      case LexKind::alt:
      case LexKind::rep:
      case LexKind::positive:
      case LexKind::negative:
      case LexKind::mark:
      case LexKind::call:
      case LexKind::through_lit:
      case LexKind::through_same:
      case LexKind::through_nested:
        break;
      default:
        throw std::invalid_argument(
            "cursiv: this lexical search cannot be used by a byte recognizer");
    }
    detail::lex_children(_g, id, [&](auto kid) { visit(kid); });
    _width[id] = recursive(id) ? kVariableWidth : width(e);
    if (!recursive(id) && !fixed(id) &&
        (e.kind == LexKind::alt ||
         (e.kind == LexKind::rep && e.max != 0 &&
          _g.lex[e.a].kind == LexKind::chars) ||
         (e.kind == LexKind::seq && e.b != 0 &&
          (_used[_g.lexKid[e.a + e.b - 1]] & kFinal) != 0)))
      _used[id] |= kFinal;
    _order.push_back(id);
  }

  void use(std::uint16_t id, bool direct) {
    const auto flag = direct ? kDirect : kNode;
    if ((_used[id] & flag) != 0) return;
    _used[id] |= flag;
    const LexNode& e = _g.lex[id];
    if (!direct && fixed(id)) {
      if (e.kind != LexKind::rep || e.max != 0) use(id, true);
      return;
    }
    if (e.kind == LexKind::rep && _g.lex[e.a].kind == LexKind::chars) return;
    const bool searched =
        e.kind == LexKind::through_nested || e.kind == LexKind::through_same;
    detail::lex_children(_g, id, [&](auto kid) {
      use(kid, !searched && (direct || dispatched(id)));
    });
  }

  std::string call(std::uint16_t id, std::string_view input,
                   std::string_view most = "false") const {
    return std::format(
        "lexical_node_{}{}(src, len, {}, seen, captures)", id,
        (_used[id] & kFinal) != 0 ? std::format("<{}>", most) : "", input);
  }

  std::string test(std::uint16_t set, std::string_view at) const {
    return std::format("{}.at({}u, src, {}, len)", _boundary, set, at);
  }

  bool fixed(std::uint16_t id) const { return _width[id] != kVariableWidth; }

  // A choice of several fixed readings, each opening on a byte: one table
  // says which of them the byte at hand can open, so the others are not asked.
  static constexpr unsigned kDispatchMin = 4, kDispatchMax = 32;
  bool dispatched(std::uint16_t id) const { return _dispatched[id] != 0; }
  bool dispatchable(std::uint16_t id) const {
    const LexNode& e = _g.lex[id];
    if (e.kind != LexKind::alt || recursive(id) || e.b < kDispatchMin ||
        e.b > kDispatchMax)
      return false;
    for (unsigned i = 0; i < e.b; ++i) {
      const auto kid = _g.lexKid[e.a + i];
      if (recursive(kid) || !fixed(kid) || _width[kid] == 0 ||
          first_bytes(kid).second)
        return false;
    }
    return true;
  }

  // The one reading that opens on an ASCII byte, where only one does.
  int ascii_reading(std::uint16_t id) const {
    const LexNode& e = _g.lex[id];
    std::uint32_t seen = 0;
    ByteSet ascii;
    ascii.add(0, 0x7f);
    for (unsigned i = 0; i < e.b; ++i)
      if (first_bytes(_g.lexKid[e.a + i]).first.intersects(ascii))
        seen |= std::uint32_t{1} << i;
    return std::popcount(seen) == 1 ? std::countr_zero(seen) : -1;
  }

  // The readings each byte can open. Where one reading opens on ASCII, the
  // dispatch returns on every ASCII byte first: the table holds the high half
  // alone, indexed with the high bit stripped.
  std::string first_table(std::uint16_t id) const {
    const LexNode& e = _g.lex[id];
    const unsigned from = ascii_reading(id) >= 0 ? 0x80 : 0;
    std::string out = std::format(
        "inline constexpr ::std::uint32_t lexical_first_{}[{}] = {{", id,
        256 - from);
    for (unsigned b = from; b < 256; ++b) {
      std::uint32_t mask = 0;
      for (unsigned i = 0; i < e.b; ++i)
        if (first_bytes(_g.lexKid[e.a + i])
                .first.has(static_cast<unsigned char>(b)))
          mask |= std::uint32_t{1} << i;
      out += std::format("{}{}u,", b % 16 == 0 ? "\n" : "", mask);
    }
    return out + "\n};\n";
  }

  // The readings of a dispatched choice the byte at `at` can open, in order;
  // `take` lays what to do with one that reads.
  std::string dispatch(std::uint16_t id, std::string_view indent,
                       std::string_view take, std::string_view miss) const {
    const LexNode& e = _g.lex[id];
    std::string out = std::format(
        "{}const auto b = static_cast<unsigned char>(src[c.at]);\n", indent);
    const int ascii = ascii_reading(id);
    if (ascii >= 0)
      out += std::format(
          "{0}if (b < 0x80) {{\n"
          "{0}  auto p = c;\n"
          "{0}  if (lexical_fixed_{1}(src, len, p.at, seen)) {{\n{2}{0}  }}\n"
          "{0}  return {3};\n"
          "{0}}}\n",
          indent, _g.lexKid[e.a + static_cast<unsigned>(ascii)], take, miss);
    out += std::format(
        "{0}for (auto m = lexical_first_{1}[b{2}]; m != 0; m &= m - 1) {{\n"
        "{0}  auto p = c;\n"
        "{0}  bool read = false;\n"
        "{0}  switch (::std::countr_zero(m)) {{\n",
        indent, id, ascii >= 0 ? " & 0x7fu" : "");
    for (unsigned i = 0; i < e.b; ++i)
      out += std::format(
          "{}    case {}u: read = lexical_fixed_{}(src, len, p.at, seen); "
          "break;\n",
          indent, i, _g.lexKid[e.a + i]);
    out += std::format("{0}    default: break;\n{0}  }}\n", indent);
    out += std::format("{}  if (read) {{\n{}{}  }}\n{}}}\n", indent, take,
                       indent, indent);
    return out;
  }

  std::string longest_step(std::string_view name, std::uint16_t kid,
                           std::string_view level) const {
    return std::format(
        "        auto {0} = {1};\n"
        "        if (!{0}.empty()) {{\n"
        "          auto q = {0}[0];\n"
        "          for (::std::size_t x = 1; x < {0}.size(); ++x)\n"
        "            if ({0}[x].at > q.at) q = {0}[x];\n"
        "          if (q.at > p.at) {{\n"
        "            p = q;\n            {2}depth;\n            continue;\n"
        "          }}\n        }}\n",
        name, call(kid, "::cursiv::LexicalState{p}"), level);
  }

  static std::string echoed_capture(std::string_view indent,
                                    std::string_view cursor,
                                    std::string_view step, std::uint32_t slot,
                                    std::string_view hit) {
    return std::format(
        "{0}const auto* capture = captures.find({1}.captures, {3}u);\n"
        "{0}if (capture == nullptr) continue;\n"
        "{0}const auto width = capture->end - capture->begin;\n"
        "{0}::cursiv::TextOffset {2} = 0;\n"
        "{0}while ({2} < width) {{\n"
        "{0}  seen = ::std::max(seen, {1}.at + {2} < len ? {1}.at + {2} + 1 : "
        "len);\n"
        "{0}  if ({1}.at + {2} >= len || src[{1}.at + {2}] != "
        "src[capture->begin + {2}]) break;\n"
        "{0}  ++{2};\n"
        "{0}}}\n"
        "{0}if ({2} == width) {{ {1}.at += width; {4} }}\n",
        indent, cursor, step, slot, hit);
  }

  bool padded_final(const LexNode& e) const {
    const auto& opener = _g.lex[e.a];
    if (opener.kind != LexKind::seq || opener.b < 2) return false;
    const auto& mark = _g.lex[_g.lexKid[opener.a + opener.b - 2]];
    if (mark.kind != LexKind::mark || mark.min != e.max) return false;
    std::string after;
    if (!LexRelayGen::literal_size(_g, _g.lexKid[opener.a + opener.b - 1]) ||
        (_g.lexKid[e.b + 1] != kNoLexNode &&
         !LexRelayGen::literal(_g, _g.lexKid[e.b + 1], after)))
      return false;
    const auto& [bytes, empty] = first_bytes(mark.a);
    const auto& pad = _g.lex[_g.lexKid[e.b + 2]];
    return pad.kind == LexKind::rep && _g.lex[pad.a].kind == LexKind::chars &&
           LexRelayGen::pad_final(_g.sets[_g.lex[pad.a].a], bytes, empty,
                                  after);
  }

  std::uint32_t width(const LexNode& e) const {
    if (e.kind == LexKind::chars) return 1;
    if (e.kind == LexKind::nguard) return 0;
    if (e.kind == LexKind::positive || e.kind == LexKind::negative)
      return fixed(e.a) ? 0 : kVariableWidth;
    if (e.kind == LexKind::rep) {
      if (e.max == 0) return 0;
      if (const auto part = _width[e.a];
          e.min == e.max && fixed(e.a) &&
          (part == 0 || e.min <= (kVariableWidth - 1) / part))
        return part * e.min;
      return kVariableWidth;
    }
    if (e.kind == LexKind::seq) {
      std::uint32_t n = 0;
      for (unsigned i = 0; i < e.b; ++i) {
        const auto part = _width[_g.lexKid[e.a + i]];
        if (part >= kVariableWidth - n) return kVariableWidth;
        n += part;
      }
      return n;
    }
    if (e.kind == LexKind::alt) {
      const auto n = e.b == 0 ? 0 : _width[_g.lexKid[e.a]];
      for (unsigned i = 1; i < e.b; ++i)
        if (_width[_g.lexKid[e.a + i]] != n) return kVariableWidth;
      return n;
    }
    return kVariableWidth;
  }

  std::string direct(std::uint16_t id) const {
    const auto& e = _g.lex[id];
    std::string out = std::format(
        "inline bool lexical_fixed_{}(const char* src, ::cursiv::TextOffset "
        "len,\n"
        "    ::cursiv::TextOffset& at, ::cursiv::TextOffset& seen) {{\n",
        id);
    if (e.kind == LexKind::chars)
      out += std::format(
          "  seen = ::std::max(seen, at < len ? at + 1 : len);\n"
          "  if (at >= len || !{}) return false;\n  ++at;\n  return true;\n",
          test(e.a, "at"));
    else if (e.kind == LexKind::seq) {
      for (unsigned i = 0; i < e.b; ++i)
        out += std::format(
            "  if (!lexical_fixed_{}(src, len, at, seen)) return false;\n",
            _g.lexKid[e.a + i]);
      out += "  return true;\n";
    } else if (e.kind == LexKind::rep) {
      if (e.max != 0 && _g.lex[e.a].kind == LexKind::chars)
        out += std::format(
            "  const auto begin = at;\n"
            "  while (at < len && at - begin < {0}u) {{\n"
            "    seen = ::std::max(seen, at + 1);\n"
            "    if (!{1}) return false;\n    ++at;\n  }}\n"
            "  return at - begin == {0}u;\n",
            e.min, test(_g.lex[e.a].a, "at"));
      else if (e.max != 0)
        out += std::format(
            "  for (::std::uint32_t n = 0; n < {}u; ++n)\n"
            "    if (!lexical_fixed_{}(src, len, at, seen)) return false;\n"
            "  return true;\n",
            e.min, e.a);
      else
        out += "  return true;\n";
    } else if (e.kind == LexKind::alt && dispatched(id)) {
      out +=
          "  seen = ::std::max(seen, at < len ? at + 1 : len);\n"
          "  if (at >= len) return false;\n"
          "  ::cursiv::LexicalCursor c{at};\n";
      out +=
          dispatch(id, "  ", "      at = p.at;\n      return true;\n", "false");
      out += "  return false;\n";
    } else if (e.kind == LexKind::alt) {
      for (unsigned i = 0; i < e.b; ++i)
        out += std::format(
            "  {{ auto probe = at;\n"
            "    if (lexical_fixed_{}(src, len, probe, seen)) {{\n"
            "      at = probe;\n      return true;\n    }}\n  }}\n",
            _g.lexKid[e.a + i]);
      out += "  return false;\n";
    } else if (e.kind == LexKind::positive || e.kind == LexKind::negative)
      out += std::format(
          "  auto probe = at;\n  return {}lexical_fixed_{}(src, len, probe, "
          "seen);\n",
          e.kind == LexKind::negative ? "!" : "", e.a);
    else {
      out += "  auto probe = at;\n";
      for (unsigned i = 0; i < e.min; ++i)
        out += std::format(
            "  seen = ::std::max(seen, probe < len ? {}.reach({}u, src, probe, "
            "len) : len);\n"
            "  if (probe >= len || !{}) return true;\n  ++probe;\n",
            _boundary, _g.lexKid[e.b + i], test(_g.lexKid[e.b + i], "probe"));
      if (e.a == kNoSet)
        out += "  return false;\n";
      else
        out += std::format(
            "  if (probe >= len) return false;\n"
            "  seen = ::std::max(seen, {}.reach({}u, src, probe, len));\n"
            "  return {}.at({}u, src, probe, len);\n",
            _boundary, e.a, _boundary, e.a);
    }
    return out + "}\n";
  }

  std::string request(std::uint16_t id, std::string_view in) const {
    if (recursive(id))
      return std::format("m.enter({}u, {}); return;\n", id, in);
    return "m.returned() = " + call(id, in) + "; return;\n";
  }

  std::string recursive_source() const {
    std::string out =
        "inline ::cursiv::LexicalState lexical_recursive(\n"
        "    ::std::uint16_t root, const char* src, ::cursiv::TextOffset len,\n"
        "    ::cursiv::LexicalState in, ::cursiv::TextOffset& seen,\n"
        "    ::cursiv::LexicalCaptures& captures) {\n"
        "  ::cursiv::LexicalMachine run;\n"
        "  return run.run(root, ::std::move(in), "
        "[&](::cursiv::LexicalMachine& m) {\n"
        "    auto& f = m.frame();\n    switch (f.node) {\n";
    for (const auto id : _order) {
      if (!recursive(id) || (_used[id] & kNode) == 0) continue;
      out += std::format("    case {}u: {{\n", id);
      switch (const LexNode& e = _g.lex[id]; e.kind) {
        case LexKind::seq:
          out +=
              "      if (f.stage) {\n"
              "        f.cur = ::std::move(m.returned());\n"
              "        if (f.cur.empty()) { m.finish({}); return; }\n"
              "      } else { f.cur = ::std::move(f.in); f.stage = 1; }\n"
              "      switch (f.at++) {\n";
          for (unsigned i = 0; i < e.b; ++i)
            out += std::format("      case {}u: ", i) +
                   request(_g.lexKid[e.a + i], "::std::move(f.cur)");
          out +=
              "      default: m.finish(::std::move(f.cur)); return;\n"
              "      }\n";
          break;
        case LexKind::alt:
          out +=
              "      if (f.stage) f.out.unite(m.returned());\n"
              "      f.stage = 1;\n      switch (f.at++) {\n";
          for (unsigned i = 0; i < e.b; ++i)
            out += std::format("      case {}u: ", i) +
                   request(_g.lexKid[e.a + i], "f.in");
          out +=
              "      default: m.finish(::std::move(f.out)); return;\n"
              "      }\n";
          break;
        case LexKind::rep:
          out += std::format(
              "      if (!f.stage) {{\n"
              "        f.cur = ::std::move(f.in);\n"
              "        f.stage = {}u;\n",
              e.min == 0 ? 2 : 1);
          if (e.min == 0) out += "        f.out = f.cur;\n";
          if (e.min != 0) {
            out +=
                "      } else if (f.stage == 1) {\n"
                "        auto next = ::std::move(m.returned());\n"
                "        if (next == f.cur) { m.finish(::std::move(next)); "
                "return; }\n"
                "        f.cur = ::std::move(next);\n";
            out += std::format(
                "        if (++f.round >= {}u) {{ f.out = f.cur; f.stage = 2; "
                "}}\n",
                e.min);
          }
          out +=
              "      } else {\n        ++f.round;\n"
              "        f.cur = m.returned().without(f.out);\n"
              "        f.out.unite(f.cur);\n      }\n";
          out += std::format(
              "      if (f.stage == 1 || (!f.cur.empty() && f.round < {}u)) "
              "{{\n        ",
              e.max);
          out += request(e.a, "f.cur") +
                 "      }\n      m.finish(::std::move(f.out)); return;\n";
          break;
        case LexKind::call: {
          const std::uint16_t callee = lex_callee(_g, e);
          out += std::format(
              "      if (f.stage) {{\n"
              "        m.remember({0}u, f.in[f.at].at, m.returned());\n"
              "        f.out.unite(m.resume(f.in[f.at], m.returned()));\n"
              "        ++f.at; f.stage = 0;\n      }}\n"
              "      while (f.at < f.in.size()) {{\n"
              "        const auto c = f.in[f.at];\n"
              "        const auto local = ::cursiv::LexicalCursor{{c.at}};\n"
              "        if (const auto* cached = m.cached({0}u, c.at)) {{\n"
              "          f.out.unite(m.resume(c, *cached)); ++f.at; continue;\n"
              "        }}\n        f.stage = 1;\n        ",
              callee);
          out += request(callee, "::cursiv::LexicalState{local}") +
                 "      }\n      m.finish(::std::move(f.out)); return;\n";
          break;
        }
        case LexKind::mark:
        case LexKind::positive:
        case LexKind::negative:
          out += "      if (f.stage) {\n";
          if (e.kind == LexKind::mark)
            out += std::format(
                "        const auto begin = f.in[f.at].at;\n"
                "        for (::std::size_t j = 0; j < m.returned().size(); "
                "++j) {{\n          auto c = m.returned()[j];\n"
                "          c.captures = captures.bind(c.captures, {}u, begin, "
                "c.at);\n"
                "          f.out.add(c);\n        }}\n",
                e.min);
          else
            out += std::format(
                "        if (m.returned().empty() == {}) "
                "f.out.add(f.in[f.at]);\n",
                e.kind == LexKind::negative ? "true" : "false");
          out +=
              "        ++f.at;\n      }\n"
              "      if (f.at == f.in.size()) {\n"
              "        m.finish(::std::move(f.out)); return;\n      }\n"
              "      f.stage = 1;\n";
          out += "      " + request(e.a, "::cursiv::LexicalState{f.in[f.at]}");
          break;
        case LexKind::through_lit:
        case LexKind::through_same:
          out += "      if (!f.stage) {\n        f.stage = 1;\n        " +
                 request(e.a, "::std::move(f.in)") + "      }\n";
          out += std::format(
              "      m.finish(lexical_after_{}(src, len, "
              "::std::move(m.returned()), seen, captures)); "
              "return;\n",
              id);
          break;
        default:
          throw std::logic_error("cursiv: unsupported recursive lexical node");
      }
      out += "    }\n";
    }
    return out + "    default: ::std::terminate();\n    }\n  });\n}\n";
  }

  std::string node(std::uint16_t id, bool opened = false) const {
    const LexNode& e = _g.lex[id];
    std::string out = std::format(
        "{}inline ::cursiv::LexicalState lexical_{}_{}(\n"
        "    const char* src, ::cursiv::TextOffset len,\n"
        "    ::cursiv::LexicalState in, ::cursiv::TextOffset& seen,\n"
        "    [[maybe_unused]] ::cursiv::LexicalCaptures& captures) {{\n",
        (_used[id] & kFinal) != 0 ? "template <bool Final>\n" : "",
        opened ? "after" : "node", id);
    if (e.kind == LexKind::rep && e.max == 0) return out + "  return in;\n}\n";
    if (e.kind == LexKind::seq && !fixed(id)) {
      for (unsigned i = 0; i < e.b; ++i)
        out += std::format("  in = {};\n  if (in.empty()) return in;\n",
                           call(_g.lexKid[e.a + i], "::std::move(in)",
                                i + 1 == e.b ? "Final" : "false"));
      return out + "  return in;\n}\n";
    }
    if (e.kind == LexKind::rep && !fixed(id) &&
        _g.lex[e.a].kind != LexKind::chars)
      return out + std::format(
                       "  return ::cursiv::gap_repeat<{}u, {}u>(in,\n"
                       "      [&](const ::cursiv::LexicalState& cur) {{ "
                       "return {}; }});\n}}\n",
                       e.min, e.max, call(e.a, "cur"));
    out += "  ::cursiv::LexicalState out;\n";
    if (fixed(id))
      return std::format(
          "{}  for (::std::size_t k = 0; k < in.size(); ++k) {{\n"
          "    auto c = in[k];\n"
          "    if (lexical_fixed_{}(src, len, c.at, seen)) out.add(c);\n"
          "  }}\n  return out;\n}}\n",
          out, id);
    if (e.kind == LexKind::alt) {
      if (dispatched(id)) {
        out +=
            "  if (in.size() == 1) {\n"
            "    const auto c = in[0];\n"
            "    seen = ::std::max(seen, c.at < len ? c.at + 1 : len);\n"
            "    if (c.at >= len) return out;\n";
        out += dispatch(id, "    ",
                        "      out.add(p);\n"
                        "      if constexpr (Final) return out;\n",
                        "out");
        out += "    return out;\n  }\n";
        for (unsigned i = 0; i < e.b; ++i)
          out += std::format(
              "  for (::std::size_t k = 0; k < in.size(); ++k) {{\n"
              "    auto c = in[k];\n"
              "    if (lexical_fixed_{}(src, len, c.at, seen)) out.add(c);\n"
              "  }}\n"
              "  if constexpr (Final) if (!out.empty()) return out;\n",
              _g.lexKid[e.a + i]);
        return out + "  return out;\n}\n";
      }
      for (unsigned i = 0; i < e.b; ++i) {
        out +=
            "  out.unite(" + call(_g.lexKid[e.a + i], "in", "Final") + ");\n";
        out += "  if constexpr (Final) if (!out.empty()) return out;\n";
      }
      return out + "  return out;\n}\n";
    }
    if (opened)
      out += "  auto starts = ::std::move(in);\n";
    else
      out +=
          "  for (::std::size_t k = 0; k < in.size(); ++k) {\n    auto c = "
          "in[k];\n";
    if (!opened && (e.kind == LexKind::through_nested ||
                    e.kind == LexKind::through_same ||
                    e.kind == LexKind::through_lit))
      out += std::format("    auto starts = {};\n",
                         call(e.a, "::cursiv::LexicalState{c}"));
    switch (e.kind) {
      case LexKind::positive:
      case LexKind::negative:
        out += std::format("    if ({}.empty() == {}) out.add(c);\n",
                           call(e.a, "::cursiv::LexicalState{c}", "true"),
                           e.kind == LexKind::negative ? "true" : "false");
        break;
      case LexKind::rep: {
        out += "    const auto begin = c.at;\n";
        if (e.min == 0) out += "    if constexpr (!Final) out.add(c);\n";
        out += std::format(
            "    while (c.at < len && c.at - begin < {}u) {{\n"
            "      seen = ::std::max(seen, c.at + 1);\n"
            "      if (!{}) break;\n      ++c.at;\n",
            e.max, test(_g.lex[e.a].a, "c.at"));
        const auto enough =
            e.min == 0 ? std::string{}
                       : std::format("if (c.at - begin >= {}u) ", e.min);
        out += std::format(
            "      if constexpr (!Final) {}out.add(c);\n    }}\n"
            "    if constexpr (Final) {}out.add(c);\n",
            enough, enough);
        break;
      }
      case LexKind::mark:
        out += std::format("    auto ends = {};\n",
                           call(e.a, "::cursiv::LexicalState{c}"));
        out += std::format(
            "    for (::std::size_t j = 0; j < ends.size(); ++j) {{\n"
            "      auto p = ends[j];\n"
            "      p.captures = captures.bind(p.captures, {}u, c.at, p.at);\n"
            "      out.add(p);\n    }}\n",
            e.min);
        break;
      case LexKind::same:
        out += echoed_capture("    ", "c", "j", e.min, "out.add(c);");
        break;
      case LexKind::through_nested:
        out +=
            "    for (::std::size_t j = 0; j < starts.size(); ++j) {\n"
            "      auto p = starts[j];\n      unsigned depth = 1;\n"
            "      while (p.at < len && depth != 0) {\n";
        out += longest_step("close", _g.lexKid[e.b + 1], "--");
        out += longest_step("open", _g.lexKid[e.b], "++");
        out +=
            "        seen = ::std::max(seen, p.at + 1);\n        ++p.at;\n"
            "      }\n";
        out += std::format("      if (depth == 0{}) out.add(p);\n    }}\n",
                           e.max ? " || p.at == len" : "");
        break;
      case LexKind::through_same:
        if (e.min == 2) {
          std::string before, after;
          if ((_g.lexKid[e.b] != kNoLexNode &&
               !LexRelayGen::literal(_g, _g.lexKid[e.b], before)) ||
              (_g.lexKid[e.b + 1] != kNoLexNode &&
               !LexRelayGen::literal(_g, _g.lexKid[e.b + 1], after)))
            throw std::logic_error("cursiv: an echo closer is not a literal");
          out +=
              "    for (::std::size_t j = 0; j < starts.size(); ++j) {\n"
              "      auto p = starts[j];\n"
              "      if (p.at == len) continue;\n";
          out += std::format(
              "      const auto* capture = captures.find(p.captures, {}u);\n"
              "      if (capture == nullptr) {{ seen = ::std::max(seen, len); "
              "continue; }}\n"
              "      const char* hit = ::cursiv::lex_find_long(\n"
              "          {{src + p.at, len - p.at}},\n"
              "          {{{}, {}u}}, {{src + capture->begin, capture->end - "
              "capture->begin}},\n"
              "          {{{}, {}u}});\n"
              "      if (hit == nullptr) {{ seen = ::std::max(seen, len); "
              "continue; }}\n"
              "      p.at = static_cast<::cursiv::TextOffset>(hit - src) + "
              "{}u + capture->end - capture->begin;\n"
              "      seen = ::std::max(seen, p.at);\n"
              "      out.add(p);\n    }}\n",
              e.max, c_literal(before), before.size(), c_literal(after),
              after.size(), before.size() + after.size());
          break;
        }
        out +=
            "    for (::std::size_t j = 0; j < starts.size(); ++j) {\n"
            "      auto p = starts[j];\n      while (p.at < len) {\n"
            "        auto ends = ::cursiv::LexicalState{p};\n";
        if (_g.lexKid[e.b] != kNoLexNode)
          out += "        ends = " + call(_g.lexKid[e.b], "::std::move(ends)") +
                 ";\n";
        if (e.min > 2)
          out += "        ends = " +
                 call(_g.lexKid[e.b + 2], "::std::move(ends)",
                      padded_final(e) ? "true" : "false") +
                 ";\n";
        out +=
            "        ::cursiv::LexicalState echoed;\n"
            "        for (::std::size_t x = 0; x < ends.size(); ++x) {\n"
            "          auto q = ends[x];\n";
        out += echoed_capture("          ", "q", "t", e.max, "echoed.add(q);");
        out += "        }\n";
        if (_g.lexKid[e.b + 1] != kNoLexNode)
          out += "        echoed = " +
                 call(_g.lexKid[e.b + 1], "::std::move(echoed)") + ";\n";
        out +=
            "        if (!echoed.empty()) { out.unite(echoed); break; }\n"
            "        seen = ::std::max(seen, p.at + 1);\n        ++p.at;\n     "
            " }\n    }\n";
        break;
      case LexKind::through_lit:
        out +=
            "    for (::std::size_t j = 0; j < starts.size(); ++j) {\n"
            "      auto p = starts[j];\n      unsigned state = 0;\n"
            "      while (p.at < len) {\n"
            "        seen = ::std::max(seen, p.at + 1);\n"
            "        switch (state) {\n";
        for (unsigned i = 0; i < e.min; ++i) {
          out += std::format("          case {}u:\n", i);
          bool first = true;
          for (unsigned d = 0; d <= e.min; ++d) {
            const auto set = _g.lexKid[e.b + i * (e.min + 1) + d];
            if (set == kNoSet) continue;
            out += std::format("            {}if ({}) state = {}u;\n",
                               first ? "" : "else ", test(set, "p.at"), d);
            first = false;
          }
          out += "            break;\n";
        }
        out += std::format(
            "        }}\n        ++p.at;\n        if (state == {}u) break;\n   "
            "   }}\n",
            e.min);
        out += std::format("      if (state == {}u{}) out.add(p);\n    }}\n",
                           e.min, e.max != 0 ? " || p.at == len" : "");
        break;
      default:
        break;
    }
    return out + (opened ? "" : "  }\n") + "  return out;\n}\n";
  }
};

}  // namespace cursiv
