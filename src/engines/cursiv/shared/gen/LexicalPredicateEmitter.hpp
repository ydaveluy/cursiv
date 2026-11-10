// Lexical recognition compiled over bytes, with no skipper or AST.
#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/CLiteral.hpp"
#include "cursiv/shared/gen/Concat.hpp"
#include "cursiv/shared/gen/LexRecursion.hpp"
#include "cursiv/shared/gen/LexRelayGen.hpp"
#include "cursiv/shared/gen/lex_intern_set.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/text/TextBuilder.hpp"

namespace cursiv::engine {

template <class Cfg>
class LexicalPredicateEmitter {
 public:
  template <class Relays>
  LexicalPredicateEmitter(const Cfg& g, const Relays& relays,
                          std::string_view boundary)
      : _g(g),
        _boundary(boundary),
        _used(g.nlex),
        _width(g.nlex, kVariableWidth),
        _first(g.nlex),
        _recursive(lex_call_ancestors(g)),
        _childReading(g.nlex, ChildReading::spelled),
        _same(g.nlex) {
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
    std::map<std::vector<std::uint32_t>, std::uint16_t> shapes;
    for (const auto id : _order) {
      _childReading[id] = child_reading(id);
      std::vector<std::uint32_t> key;
      _same[id] = shape(id, key) ? shapes.try_emplace(key, id).first->second
                                 : id;
    }
    for (const auto root : _roots) use(same(root), fixed(root));
    for (const auto root : _recognizers) use(same(root), false);
  }

  bool any() const { return !_roots.empty() || !_recognizers.empty(); }
  bool searches() const {
    return std::ranges::any_of(_order, [&](auto id) {
      return _g.lex[id].kind == LexKind::through_same;
    });
  }

  std::string declarations() const {
    TextBuilder out;
    for (const auto root : _roots)
      out.line("bool lexical_predicate_", root,
               "(const char*, ::cursiv::TextOffset,\n"
               "    ::cursiv::TextOffset, ::cursiv::TextOffset&);");
    for (const auto root : _recognizers)
      out.line("::cursiv::engine::PredicateState lexical_recognizer_", root,
               "(const char*, "
               "::cursiv::TextOffset,\n"
               "    ::cursiv::TextOffset, ::cursiv::TextOffset&);");
    return out.take();
  }

  std::string source() const {
    TextBuilder out;
    for (const auto id : _order) {
      if (recursive(id)) {
        if (const auto kind = _g.lex[id].kind;
            kind == LexKind::through_lit || kind == LexKind::through_same)
          out.write(node(id, true));
        continue;
      }
      // A node of a shape met before, or none uses, has no code.
      if ((_used[id] & (kDirect | kNode)) == 0) continue;
      if (dispatched(id)) out.write(first_table(id));
      if (worded(id)) out.write(word_table(id));
      if ((_used[id] & kDirect) != 0) out.write(direct(id));
      if ((_used[id] & kNode) != 0) out.write(node(id));
    }
    if (std::ranges::any_of(_order, [&](auto id) { return recursive(id); }))
      out.write(recursive_source());
    const auto started = [&](std::uint16_t root, std::string_view most) {
      return recursive(root) ? concat("lexical_recursive(", root,
                                      "u, src, len, "
                                      "::cursiv::engine::LexicalState{"
                                      "::cursiv::engine::LexicalCursor{at}}"
                                      ", seen, captures)")
                             : call(root,
                                    "::cursiv::engine::LexicalState{"
                                    "::cursiv::engine::LexicalCursor{at}}",
                                    most);
    };
    for (const auto root : _roots) {
      out.line("bool lexical_predicate_", root,
               "(const char* src, ::cursiv::TextOffset "
               "len,\n"
               "    ::cursiv::TextOffset at, ::cursiv::TextOffset& seen) {");
      if (fixed(root))
        out.line("  return lexical_fixed_", same(root),
                 "(src, len, at, seen);");
      else {
        out.line("  ::cursiv::engine::LexicalCaptures captures;");
        out.line("  return !", started(root, "true"), ".empty();");
      }
      out.line("}");
    }
    for (const auto root : _recognizers) {
      out.line("::cursiv::engine::PredicateState lexical_recognizer_", root,
               "(const char* src, "
               "::cursiv::TextOffset len,\n"
               "    ::cursiv::TextOffset at, ::cursiv::TextOffset& seen) {");
      out.line("  ::cursiv::engine::LexicalCaptures captures;");
      out.line("  return ::cursiv::engine::lexical_endpoints(",
               started(root, "false"), ");");
      out.line("}");
    }
    return out.take();
  }

 private:
  const Cfg& _g;
  std::string _boundary;
  static constexpr std::uint8_t kVisited = 1, kDirect = 2, kNode = 4,
                                kFinal = 8;
  std::vector<std::uint8_t> _used;
  static constexpr std::uint32_t kVariableWidth = 0xffffffffu;
  std::vector<std::uint32_t> _width;
  mutable std::vector<std::optional<std::pair<ByteSet, bool>>> _first;
  std::vector<std::uint8_t> _recursive;
  // How a node reads its children: each in turn, a choice dispatched on the
  // byte it opens on, or many words read as data (`LexicalWords`).
  enum class ChildReading : std::uint8_t { spelled, dispatched, words };
  std::vector<ChildReading> _childReading;
  // Each node as the first node of its shape: a grammar that repeats an
  // expression has its code once.
  std::vector<std::uint16_t> _same;
  std::vector<std::uint16_t> _roots, _recognizers, _order;

  bool recursive(std::uint16_t id) const {
    return !_recursive.empty() && _recursive[id];
  }

  std::uint16_t same(std::uint16_t id) const { return _same[id]; }
  // What a node's code reads, where that is its kind, its bounds, its classes
  // and its children' shapes alone; the others keep a code of their own.
  bool shape(std::uint16_t id, std::vector<std::uint32_t>& key) const {
    const LexNode& e = _g.lex[id];
    if (recursive(id)) return false;
    key = {static_cast<std::uint32_t>(e.kind), e.min, e.max};
    switch (e.kind) {
      case LexKind::chars:
        key.push_back(e.a);
        return true;
      case LexKind::nguard:
        key.push_back(e.a);
        for (unsigned i = 0; i < e.min; ++i)
          key.push_back(_g.lexChild[e.b + i]);
        return true;
      case LexKind::seq:
      case LexKind::alt:
        for (unsigned i = 0; i < e.b; ++i)
          key.push_back(same(_g.lexChild[e.a + i]));
        return true;
      case LexKind::rep:
        key.push_back(e.max != 0 ? same(e.a) : kNoLexNode);
        return e.b == kNoLexNode;
      case LexKind::positive:
      case LexKind::negative:
        key.push_back(same(e.a));
        return true;
      default:
        return false;
    }
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
    lex_children(_g, id, [&](auto child) { visit(child); });
    _width[id] = recursive(id) ? kVariableWidth : width(e);
    if (!recursive(id) && !fixed(id) &&
        (e.kind == LexKind::alt ||
         (e.kind == LexKind::rep && e.max != 0 &&
          _g.lex[e.a].kind == LexKind::chars) ||
         (e.kind == LexKind::seq && e.b != 0 &&
          (_used[_g.lexChild[e.a + e.b - 1]] & kFinal) != 0)))
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
    if ((e.kind == LexKind::rep && _g.lex[e.a].kind == LexKind::chars) ||
        worded(id))
      return;
    const bool searched =
        e.kind == LexKind::through_nested || e.kind == LexKind::through_same;
    lex_children(_g, id, [&](auto child) {
      use(same(child), !searched && (direct || dispatched(id)));
    });
  }

  std::string call(std::uint16_t id, std::string_view input,
                   std::string_view most = "false") const {
    return concat("lexical_node_", same(id),
                  (_used[id] & kFinal) != 0 ? concat("<", most, ">") : "",
                  "(src, len, ", input, ", seen, captures)");
  }

  std::string test(std::uint16_t set, std::string_view at) const {
    return concat(_boundary, ".at(", set, "u, src, ", at, ", len)");
  }

  bool fixed(std::uint16_t id) const { return _width[id] != kVariableWidth; }

  // A choice of several fixed readings, each opening on a byte: one table
  // says which of them the byte at hand can open, so the others are not asked.
  static constexpr unsigned kDispatchMin = 4, kDispatchMax = 32;
  bool dispatched(std::uint16_t id) const {
    return _childReading[id] == ChildReading::dispatched;
  }
  bool worded(std::uint16_t id) const {
    return _childReading[id] == ChildReading::words;
  }
  ChildReading child_reading(std::uint16_t id) const {
    const LexNode& e = _g.lex[id];
    if ((e.kind != LexKind::alt && e.kind != LexKind::seq) || recursive(id) ||
        e.b < kDispatchMin)
      return ChildReading::spelled;
    // Past what a mask holds, a choice of words or a run of refusals of words
    // is read as data: a class of characters over several bytes lowers to
    // hundreds of them.
    if (e.b > kDispatchMax) {
      for (unsigned i = 0; i < e.b; ++i)
        if (!(e.kind == LexKind::alt ? word(_g.lexChild[e.a + i])
                                     : refusal(_g.lexChild[e.a + i])))
          return ChildReading::spelled;
      return ChildReading::words;
    }
    if (e.kind != LexKind::alt) return ChildReading::spelled;
    for (unsigned i = 0; i < e.b; ++i) {
      const auto child = _g.lexChild[e.a + i];
      if (recursive(child) || !fixed(child) || _width[child] == 0 ||
          first_bytes(child).second)
        return ChildReading::spelled;
    }
    return ChildReading::dispatched;
  }

  // A word is a class, or a sequence of classes; a refusal of one, an `nguard`
  // of a byte at least.
  bool word(std::uint16_t id) const {
    const LexNode& e = _g.lex[id];
    if (e.kind == LexKind::chars) return true;
    if (e.kind != LexKind::seq || e.b == 0) return false;
    for (unsigned i = 0; i < e.b; ++i)
      if (_g.lex[_g.lexChild[e.a + i]].kind != LexKind::chars) return false;
    return true;
  }
  bool refusal(std::uint16_t id) const {
    const LexNode& e = _g.lex[id];
    return e.kind == LexKind::nguard && e.min != 0;
  }

  // The words of a node read as data, in `LexicalWords`'s layout, and the
  // tables that name them `lexical_words_<id>`.
  std::string word_table(std::uint16_t id) const {
    const LexNode& e = _g.lex[id];
    std::vector<std::uint16_t> words;
    std::vector<std::vector<std::uint32_t>> opens(256);
    for (unsigned i = 0; i < e.b; ++i) {
      const auto child = _g.lexChild[e.a + i];
      const LexNode& k = _g.lex[child];
      const auto at = static_cast<std::uint32_t>(words.size());
      if (k.kind == LexKind::nguard) {
        words.push_back(static_cast<std::uint16_t>(k.min));
        words.push_back(k.a);
        for (unsigned j = 0; j < k.min; ++j)
          words.push_back(_g.lexChild[k.b + j]);
      } else if (k.kind == LexKind::chars) {
        words.insert(words.end(), {1, kNoSet, k.a});
      } else {
        words.push_back(static_cast<std::uint16_t>(k.b));
        words.push_back(kNoSet);
        for (unsigned j = 0; j < k.b; ++j)
          words.push_back(_g.lex[_g.lexChild[k.a + j]].a);
      }
      const auto first = words[at + 2];
      const ByteSet opening = _g.sets[first] | lex_decode_bytes(_g, first);
      for (unsigned b = 0; b < 256; ++b)
        if (opening.has(static_cast<unsigned char>(b))) opens[b].push_back(at);
    }
    TextBuilder out;
    out.write("inline constexpr ::std::uint16_t lexical_list_", id, "[] = {");
    for (std::size_t i = 0; i < words.size(); ++i)
      out.write(i % 16 == 0 ? "\n" : "", words[i], "u,");
    out.write("\n};\ninline constexpr ::std::uint32_t lexical_opens_", id,
              "[] = {");
    std::uint32_t n = 0;
    for (unsigned b = 0; b <= 256; ++b) {
      out.write(b % 16 == 0 ? "\n" : "", n, "u,");
      if (b < 256) n += static_cast<std::uint32_t>(opens[b].size());
    }
    out.write("\n};\ninline constexpr ::std::uint32_t lexical_order_", id,
              "[] = {");
    n = 0;
    for (const auto& list : opens)
      for (const auto at : list) out.write(n++ % 16 == 0 ? "\n" : "", at, "u,");
    if (n == 0) out.write("0u");
    return out
        .line(
            "\n};\ninline constexpr ::cursiv::engine::LexicalWords "
            "lexical_words_",
            id, "{lexical_list_", id, ", ", words.size(),
            "u, "
            "lexical_opens_",
            id, ", lexical_order_", id, "};")
        .take();
  }

  // The one reading that opens on an ASCII byte, where only one does.
  int ascii_reading(std::uint16_t id) const {
    const LexNode& e = _g.lex[id];
    std::uint32_t seen = 0;
    ByteSet ascii;
    ascii.add(0, 0x7f);
    for (unsigned i = 0; i < e.b; ++i)
      if (first_bytes(_g.lexChild[e.a + i]).first.intersects(ascii))
        seen |= std::uint32_t{1} << i;
    return std::popcount(seen) == 1 ? std::countr_zero(seen) : -1;
  }

  // The readings each byte can open. Where one reading opens on ASCII, the
  // dispatch returns on every ASCII byte first: the table holds the high half
  // alone, indexed with the high bit stripped.
  std::string first_table(std::uint16_t id) const {
    const LexNode& e = _g.lex[id];
    const unsigned from = ascii_reading(id) >= 0 ? 0x80 : 0;
    TextBuilder out;
    out.write("inline constexpr ::std::uint32_t lexical_first_", id, "[",
              256 - from, "] = {");
    for (unsigned b = from; b < 256; ++b) {
      std::uint32_t mask = 0;
      for (unsigned i = 0; i < e.b; ++i)
        if (first_bytes(_g.lexChild[e.a + i])
                .first.has(static_cast<unsigned char>(b)))
          mask |= std::uint32_t{1} << i;
      out.write(b % 16 == 0 ? "\n" : "", mask, "u,");
    }
    return out.line("\n};").take();
  }

  // The readings of a dispatched choice the byte at `at` can open, in order;
  // `take` is the code for one that reads.
  std::string dispatch(std::uint16_t id, std::string_view indent,
                       std::string_view take, std::string_view miss) const {
    const LexNode& e = _g.lex[id];
    TextBuilder out;
    out.line(indent, "const auto b = static_cast<unsigned char>(src[c.at]);");
    const int ascii = ascii_reading(id);
    if (ascii >= 0)
      out.line(indent, "if (b < 0x80) {\n", indent, "  auto p = c;\n", indent,
               "  if (lexical_fixed_",
               same(_g.lexChild[e.a + static_cast<unsigned>(ascii)]),
               "(src, len, p.at, seen)) {\n", take, indent, "  }\n", indent,
               "  return ", miss, ";\n", indent, "}");
    out.line(indent, "for (auto m = lexical_first_", id, "[b",
             ascii >= 0 ? " & 0x7fu" : "", "]; m != 0; m &= m - 1) {\n", indent,
             "  auto p = c;\n", indent, "  bool read = false;\n", indent,
             "  switch (::std::countr_zero(m)) {");
    for (unsigned i = 0; i < e.b; ++i)
      out.line(indent, "    case ", i, "u: read = lexical_fixed_",
               same(_g.lexChild[e.a + i]),
               "(src, len, p.at, seen); "
               "break;");
    out.line(indent, "    default: break;\n", indent, "  }");
    out.line(indent, "  if (read) {\n", take, indent, "  }\n", indent, "}");
    return out.take();
  }

  std::string longest_step(std::string_view name, std::uint16_t child,
                           std::string_view level) const {
    return concat("        auto ", name, " = ",
                  call(child, "::cursiv::engine::LexicalState{p}"),
                  ";\n"
                  "        if (!",
                  name,
                  ".empty()) {\n"
                  "          auto q = ",
                  name,
                  "[0];\n"
                  "          for (::std::size_t x = 1; x < ",
                  name,
                  ".size(); ++x)\n"
                  "            if (",
                  name, "[x].at > q.at) q = ", name,
                  "[x];\n"
                  "          if (q.at > p.at) {\n"
                  "            p = q;\n            ",
                  level,
                  "depth;\n            continue;\n"
                  "          }\n        }\n");
  }

  static std::string echoed_capture(std::string_view indent,
                                    std::string_view cursor,
                                    std::string_view step, std::uint32_t slot,
                                    std::string_view hit) {
    return concat(
        indent, "const auto* capture = captures.find(", cursor, ".captures, ",
        slot, "u);\n", indent, "if (capture == nullptr) continue;\n", indent,
        "const auto width = capture->end - capture->begin;\n", indent,
        "::cursiv::TextOffset ", step, " = 0;\n", indent, "while (", step,
        " < width) {\n", indent, "  seen = ::std::max(seen, ", cursor, ".at + ",
        step, " < len ? ", cursor, ".at + ", step,
        " + 1 : "
        "len);\n",
        indent, "  if (", cursor, ".at + ", step, " >= len || src[", cursor,
        ".at + ", step,
        "] != "
        "src[capture->begin + ",
        step, "]) break;\n", indent, "  ++", step, ";\n", indent, "}\n", indent,
        "if (", step, " == width) { ", cursor, ".at += width; ", hit, " }\n");
  }

  bool padded_final(const LexNode& e) const {
    const auto& opener = _g.lex[e.a];
    if (opener.kind != LexKind::seq || opener.b < 2) return false;
    const auto& mark = _g.lex[_g.lexChild[opener.a + opener.b - 2]];
    if (mark.kind != LexKind::mark || mark.min != e.max) return false;
    std::string after;
    if (!LexRelayGen::literal_size(_g, _g.lexChild[opener.a + opener.b - 1]) ||
        (_g.lexChild[e.b + 1] != kNoLexNode &&
         !LexRelayGen::literal(_g, _g.lexChild[e.b + 1], after)))
      return false;
    const auto& [bytes, empty] = first_bytes(mark.a);
    const auto& pad = _g.lex[_g.lexChild[e.b + 2]];
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
        const auto part = _width[_g.lexChild[e.a + i]];
        if (part >= kVariableWidth - n) return kVariableWidth;
        n += part;
      }
      return n;
    }
    if (e.kind == LexKind::alt) {
      const auto n = e.b == 0 ? 0 : _width[_g.lexChild[e.a]];
      for (unsigned i = 1; i < e.b; ++i)
        if (_width[_g.lexChild[e.a + i]] != n) return kVariableWidth;
      return n;
    }
    return kVariableWidth;
  }

  std::string direct(std::uint16_t id) const {
    const auto& e = _g.lex[id];
    TextBuilder out;
    out.line("inline bool lexical_fixed_", id,
             "(const char* src, ::cursiv::TextOffset "
             "len,\n"
             "    ::cursiv::TextOffset& at, ::cursiv::TextOffset& seen) {");
    if (worded(id))
      out.line("  return ::cursiv::engine::lexical_",
               e.kind == LexKind::alt ? "first_word" : "refused", "(",
               _boundary, ", lexical_words_", id,
               ", src, len, at, "
               "seen);");
    else if (e.kind == LexKind::chars)
      out.line(
          "  seen = ::std::max(seen, at < len ? at + 1 : len);\n"
          "  if (at >= len || !",
          test(e.a, "at"), ") return false;\n  ++at;\n  return true;");
    else if (e.kind == LexKind::seq) {
      for (unsigned i = 0; i < e.b; ++i)
        out.line("  if (!lexical_fixed_", same(_g.lexChild[e.a + i]),
                 "(src, len, at, seen)) return false;");
      out.line("  return true;");
    } else if (e.kind == LexKind::rep) {
      if (e.max != 0 && _g.lex[e.a].kind == LexKind::chars)
        out.line(
            "  const auto begin = at;\n"
            "  while (at < len && at - begin < ",
            e.min,
            "u) {\n"
            "    seen = ::std::max(seen, at + 1);\n"
            "    if (!",
            test(_g.lex[e.a].a, "at"),
            ") return false;\n    ++at;\n  }\n"
            "  return at - begin == ",
            e.min, "u;");
      else if (e.max != 0)
        out.line("  for (::std::uint32_t n = 0; n < ", e.min,
                 "u; ++n)\n"
                 "    if (!lexical_fixed_",
                 same(e.a),
                 "(src, len, at, seen)) return false;\n"
                 "  return true;");
      else
        out.line("  return true;");
    } else if (e.kind == LexKind::alt && dispatched(id)) {
      out.line(
          "  seen = ::std::max(seen, at < len ? at + 1 : len);\n"
          "  if (at >= len) return false;\n"
          "  ::cursiv::engine::LexicalCursor c{at};");
      out.write(dispatch(id, "  ", "      at = p.at;\n      return true;\n",
                         "false"));
      out.line("  return false;");
    } else if (e.kind == LexKind::alt) {
      for (unsigned i = 0; i < e.b; ++i)
        out.line(
            "  { auto probe = at;\n"
            "    if (lexical_fixed_",
            same(_g.lexChild[e.a + i]),
            "(src, len, probe, seen)) {\n"
            "      at = probe;\n      return true;\n    }\n  }");
      out.line("  return false;");
    } else if (e.kind == LexKind::positive || e.kind == LexKind::negative)
      out.line("  auto probe = at;\n  return ",
               e.kind == LexKind::negative ? "!" : "", "lexical_fixed_",
               same(e.a),
               "(src, len, probe, "
               "seen);");
    else {
      out.line("  auto probe = at;");
      for (unsigned i = 0; i < e.min; ++i)
        out.line("  seen = ::std::max(seen, probe < len ? ", _boundary,
                 ".reach(", _g.lexChild[e.b + i],
                 "u, src, probe, "
                 "len) : len);\n"
                 "  if (probe >= len || !",
                 test(_g.lexChild[e.b + i], "probe"),
                 ") return true;\n  ++probe;");
      if (e.a == kNoSet)
        out.line("  return false;");
      else
        out.line(
            "  if (probe >= len) return false;\n"
            "  seen = ::std::max(seen, ",
            _boundary, ".reach(", e.a,
            "u, src, probe, len));\n"
            "  return ",
            _boundary, ".at(", e.a, "u, src, probe, len);");
    }
    return out.line("}").take();
  }

  std::string request(std::uint16_t id, std::string_view in) const {
    if (recursive(id)) return concat("m.enter(", id, "u, ", in, "); return;\n");
    return "m.returned() = " + call(id, in) + "; return;\n";
  }

  std::string recursive_source() const {
    TextBuilder out;
    out.line(
        "inline ::cursiv::engine::LexicalState lexical_recursive(\n"
        "    ::std::uint16_t root, const char* src, ::cursiv::TextOffset len,\n"
        "    ::cursiv::engine::LexicalState in, ::cursiv::TextOffset& seen,\n"
        "    ::cursiv::engine::LexicalCaptures& captures) {\n"
        "  ::cursiv::engine::LexicalMachine run;\n"
        "  return run.run(root, ::std::move(in), "
        "[&](::cursiv::engine::LexicalMachine& m) {\n"
        "    auto& f = m.frame();\n    switch (f.node) {");
    for (const auto id : _order) {
      if (!recursive(id) || (_used[id] & kNode) == 0) continue;
      out.line("    case ", id, "u: {");
      switch (const LexNode& e = _g.lex[id]; e.kind) {
        case LexKind::seq:
          out.line(
              "      if (f.stage) {\n"
              "        f.cur = ::std::move(m.returned());\n"
              "        if (f.cur.empty()) { m.finish({}); return; }\n"
              "      } else { f.cur = ::std::move(f.in); f.stage = 1; }\n"
              "      switch (f.at++) {");
          for (unsigned i = 0; i < e.b; ++i)
            out.write("      case ", i, "u: ",
                      request(_g.lexChild[e.a + i], "::std::move(f.cur)"));
          out.line(
              "      default: m.finish(::std::move(f.cur)); return;\n"
              "      }");
          break;
        case LexKind::alt:
          out.line(
              "      if (f.stage) f.out.unite(m.returned());\n"
              "      f.stage = 1;\n      switch (f.at++) {");
          for (unsigned i = 0; i < e.b; ++i)
            out.write("      case ", i,
                      "u: ", request(_g.lexChild[e.a + i], "f.in"));
          out.line(
              "      default: m.finish(::std::move(f.out)); return;\n"
              "      }");
          break;
        case LexKind::rep:
          out.line(
              "      if (!f.stage) {\n"
              "        f.cur = ::std::move(f.in);\n"
              "        f.stage = ",
              e.min == 0 ? 2 : 1, "u;");
          if (e.min == 0) out.line("        f.out = f.cur;");
          if (e.min != 0) {
            out.line(
                "      } else if (f.stage == 1) {\n"
                "        auto next = ::std::move(m.returned());\n"
                "        if (next == f.cur) { m.finish(::std::move(next)); "
                "return; }\n"
                "        f.cur = ::std::move(next);");
            out.line("        if (++f.round >= ", e.min,
                     "u) { f.out = f.cur; f.stage = 2; "
                     "}");
          }
          out.line(
              "      } else {\n        ++f.round;\n"
              "        f.cur = m.returned().without(f.out);\n"
              "        f.out.unite(f.cur);\n      }");
          out.write("      if (f.stage == 1 || (!f.cur.empty() && f.round < ",
                    e.max,
                    "u)) "
                    "{\n        ");
          out.line(request(e.a, "f.cur"),
                   "      }\n      m.finish(::std::move(f.out)); return;");
          break;
        case LexKind::call: {
          const std::uint16_t callee = lex_callee(_g, e);
          out.write(
              "      if (f.stage) {\n"
              "        m.remember(",
              callee,
              "u, f.in[f.at].at, m.returned());\n"
              "        f.out.unite(m.resume(f.in[f.at], m.returned()));\n"
              "        ++f.at; f.stage = 0;\n      }\n"
              "      while (f.at < f.in.size()) {\n"
              "        const auto c = f.in[f.at];\n"
              "        const auto local = "
              "::cursiv::engine::LexicalCursor{c.at};\n"
              "        if (const auto* cached = m.cached(",
              callee,
              "u, c.at)) {\n"
              "          f.out.unite(m.resume(c, *cached)); ++f.at; continue;\n"
              "        }\n        f.stage = 1;\n        ");
          out.line(request(callee, "::cursiv::engine::LexicalState{local}"),
                   "      }\n      m.finish(::std::move(f.out)); return;");
          break;
        }
        case LexKind::positive:
        case LexKind::negative:
          out.line("      if (f.stage) {");
          out.line("        if (m.returned().empty() == ",
                   e.kind == LexKind::negative ? "true" : "false",
                   ") "
                   "f.out.add(f.in[f.at]);");
          out.line(
              "        ++f.at;\n      }\n"
              "      if (f.at == f.in.size()) {\n"
              "        m.finish(::std::move(f.out)); return;\n      }\n"
              "      f.stage = 1;");
          out.write("      ",
                    request(e.a, "::cursiv::engine::LexicalState{f.in[f.at]}"));
          break;
        case LexKind::through_lit:
        case LexKind::through_same:
          out.line("      if (!f.stage) {\n        f.stage = 1;\n        ",
                   request(e.a, "::std::move(f.in)"), "      }");
          out.line("      m.finish(lexical_after_", id,
                   "(src, len, "
                   "::std::move(m.returned()), seen, captures)); "
                   "return;");
          break;
        default:
          throw std::logic_error("cursiv: unsupported recursive lexical node");
      }
      out.line("    }");
    }
    return out.line("    default: ::std::terminate();\n    }\n  });\n}").take();
  }

  std::string node(std::uint16_t id, bool opened = false) const {
    const LexNode& e = _g.lex[id];
    TextBuilder out;
    out.line(
        (_used[id] & kFinal) != 0 ? "template <bool Final>\n" : "",
        "inline ::cursiv::engine::LexicalState lexical_",
        opened ? "after" : "node", "_", id,
        "(\n"
        "    const char* src, ::cursiv::TextOffset len,\n"
        "    ::cursiv::engine::LexicalState in, ::cursiv::TextOffset& seen,\n"
        "    [[maybe_unused]] ::cursiv::engine::LexicalCaptures& captures) {");
    if (e.kind == LexKind::rep && e.max == 0)
      return out.line("  return in;\n}").take();
    if (e.kind == LexKind::seq && !fixed(id)) {
      for (unsigned i = 0; i < e.b; ++i)
        out.line("  in = ",
                 call(_g.lexChild[e.a + i], "::std::move(in)",
                      i + 1 == e.b ? "Final" : "false"),
                 ";\n  if (in.empty()) return in;");
      return out.line("  return in;\n}").take();
    }
    if (e.kind == LexKind::rep && !fixed(id) &&
        _g.lex[e.a].kind != LexKind::chars)
      return out
          .line("  return ::cursiv::engine::gap_repeat<", e.min, "u, ", e.max,
                "u>(in,\n"
                "      [&](const ::cursiv::engine::LexicalState& cur) { "
                "return ",
                call(e.a, "cur"), "; });\n}")
          .take();
    if (worded(id) && !fixed(id))
      return out
          .line("  return ::cursiv::engine::lexical_words<",
                (_used[id] & kFinal) != 0 ? "Final" : "false", ">(", _boundary,
                ", "
                "lexical_words_",
                id, ", src, len, in, seen);\n}")
          .take();
    out.line("  ::cursiv::engine::LexicalState out;");
    if (fixed(id))
      return out
          .line(
              "  for (::std::size_t k = 0; k < in.size(); ++k) {\n"
              "    auto c = in[k];\n"
              "    if (lexical_fixed_",
              id,
              "(src, len, c.at, seen)) out.add(c);\n"
              "  }\n  return out;\n}")
          .take();
    if (e.kind == LexKind::alt) {
      if (dispatched(id)) {
        out.line(
            "  if (in.size() == 1) {\n"
            "    const auto c = in[0];\n"
            "    seen = ::std::max(seen, c.at < len ? c.at + 1 : len);\n"
            "    if (c.at >= len) return out;");
        out.write(dispatch(id, "    ",
                           "      out.add(p);\n"
                           "      if constexpr (Final) return out;\n",
                           "out"));
        out.line("    return out;\n  }");
        for (unsigned i = 0; i < e.b; ++i)
          out.line(
              "  for (::std::size_t k = 0; k < in.size(); ++k) {\n"
              "    auto c = in[k];\n"
              "    if (lexical_fixed_",
              same(_g.lexChild[e.a + i]),
              "(src, len, c.at, seen)) out.add(c);\n"
              "  }\n"
              "  if constexpr (Final) if (!out.empty()) return out;");
        return out.line("  return out;\n}").take();
      }
      for (unsigned i = 0; i < e.b; ++i) {
        out.line("  out.unite(", call(_g.lexChild[e.a + i], "in", "Final"),
                 ");");
        out.line("  if constexpr (Final) if (!out.empty()) return out;");
      }
      return out.line("  return out;\n}").take();
    }
    if (opened)
      out.line("  auto starts = ::std::move(in);");
    else
      out.line(
          "  for (::std::size_t k = 0; k < in.size(); ++k) {\n    auto c = "
          "in[k];");
    if (!opened && (e.kind == LexKind::through_nested ||
                    e.kind == LexKind::through_same ||
                    e.kind == LexKind::through_lit))
      out.line("    auto starts = ",
               call(e.a, "::cursiv::engine::LexicalState{c}"), ";");
    switch (e.kind) {
      case LexKind::positive:
      case LexKind::negative:
        out.line("    if (",
                 call(e.a, "::cursiv::engine::LexicalState{c}", "true"),
                 ".empty() == ", e.kind == LexKind::negative ? "true" : "false",
                 ") out.add(c);");
        break;
      case LexKind::rep: {
        out.line("    const auto begin = c.at;");
        if (e.min == 0) out.line("    if constexpr (!Final) out.add(c);");
        out.line("    while (c.at < len && c.at - begin < ", e.max,
                 "u) {\n"
                 "      seen = ::std::max(seen, c.at + 1);\n"
                 "      if (!",
                 test(_g.lex[e.a].a, "c.at"), ") break;\n      ++c.at;");
        const auto enough = e.min == 0
                                ? std::string{}
                                : concat("if (c.at - begin >= ", e.min, "u) ");
        out.line("      if constexpr (!Final) ", enough,
                 "out.add(c);\n    }\n"
                 "    if constexpr (Final) ",
                 enough, "out.add(c);");
        break;
      }
      case LexKind::mark:
        out.line("    auto ends = ",
                 call(e.a, "::cursiv::engine::LexicalState{c}"), ";");
        out.line(
            "    for (::std::size_t j = 0; j < ends.size(); ++j) {\n"
            "      auto p = ends[j];\n"
            "      p.captures = captures.bind(p.captures, ",
            e.min,
            "u, c.at, p.at);\n"
            "      out.add(p);\n    }");
        break;
      case LexKind::same:
        out.write(echoed_capture("    ", "c", "j", e.min, "out.add(c);"));
        break;
      case LexKind::through_nested:
        out.line(
            "    for (::std::size_t j = 0; j < starts.size(); ++j) {\n"
            "      auto p = starts[j];\n      unsigned depth = 1;\n"
            "      while (p.at < len && depth != 0) {");
        out.write(longest_step("close", _g.lexChild[e.b + 1], "--"));
        out.write(longest_step("open", _g.lexChild[e.b], "++"));
        out.line(
            "        seen = ::std::max(seen, p.at + 1);\n        ++p.at;\n"
            "      }");
        out.line("      if (depth == 0", e.max ? " || p.at == len" : "",
                 ") out.add(p);\n    }");
        break;
      case LexKind::through_same:
        if (e.min == 2) {
          std::string before, after;
          if ((_g.lexChild[e.b] != kNoLexNode &&
               !LexRelayGen::literal(_g, _g.lexChild[e.b], before)) ||
              (_g.lexChild[e.b + 1] != kNoLexNode &&
               !LexRelayGen::literal(_g, _g.lexChild[e.b + 1], after)))
            throw std::logic_error("cursiv: an echo closer is not a literal");
          out.line(
              "    for (::std::size_t j = 0; j < starts.size(); ++j) {\n"
              "      auto p = starts[j];\n"
              "      if (p.at == len) continue;");
          out.line(
              "      const auto* capture = captures.find(p.captures, ", e.max,
              "u);\n"
              "      if (capture == nullptr) { seen = ::std::max(seen, len); "
              "continue; }\n"
              "      const char* hit = ::cursiv::engine::lex_find_closer(\n"
              "          {src + p.at, len - p.at},\n"
              "          {",
              c_literal(before), ", ", before.size(),
              "u}, {src + capture->begin, capture->end - "
              "capture->begin},\n"
              "          {",
              c_literal(after), ", ", after.size(),
              "u}, [&](const char* q) {\n"
              "            return ::cursiv::engine::lex_character_boundary(\n"
              "                {src + p.at, len - p.at}, q);\n"
              "          });\n"
              "      if (hit == nullptr) { seen = ::std::max(seen, len); "
              "continue; }\n"
              "      p.at = static_cast<::cursiv::TextOffset>(hit - src) + ",
              before.size() + after.size(),
              "u + capture->end - capture->begin;\n"
              "      seen = ::std::max(seen, p.at);\n"
              "      out.add(p);\n    }");
          break;
        }
        out.line(
            "    for (::std::size_t j = 0; j < starts.size(); ++j) {\n"
            "      auto p = starts[j];\n      while (p.at < len) {\n"
            "        auto ends = ::cursiv::engine::LexicalState{p};");
        if (_g.lexChild[e.b] != kNoLexNode)
          out.line("        ends = ",
                   call(_g.lexChild[e.b], "::std::move(ends)"), ";");
        if (e.min > 2)
          out.line("        ends = ",
                   call(_g.lexChild[e.b + 2], "::std::move(ends)",
                        padded_final(e) ? "true" : "false"),
                   ";");
        out.line(
            "        ::cursiv::engine::LexicalState echoed;\n"
            "        for (::std::size_t x = 0; x < ends.size(); ++x) {\n"
            "          auto q = ends[x];");
        out.write(
            echoed_capture("          ", "q", "t", e.max, "echoed.add(q);"));
        out.line("        }");
        if (_g.lexChild[e.b + 1] != kNoLexNode)
          out.line("        echoed = ",
                   call(_g.lexChild[e.b + 1], "::std::move(echoed)"), ";");
        out.line(
            "        if (!echoed.empty()) { out.unite(echoed); break; }\n"
            "        ::std::size_t width = 0;\n"
            "        (void)::cursiv::scalar_at(src + p.at, len - p.at, "
            "width);\n"
            "        p.at += static_cast<::cursiv::TextOffset>(\n"
            "            width != 0 ? width : 1);\n"
            "        seen = ::std::max(seen, p.at);\n"
            "      }\n    }");
        break;
      case LexKind::through_lit:
        out.line(
            "    for (::std::size_t j = 0; j < starts.size(); ++j) {\n"
            "      auto p = starts[j];\n      unsigned state = 0;\n"
            "      while (p.at < len) {\n"
            "        seen = ::std::max(seen, p.at + 1);\n"
            "        switch (state) {");
        for (unsigned i = 0; i < e.min; ++i) {
          out.line("          case ", i, "u:");
          bool first = true;
          for (unsigned d = 0; d <= e.min; ++d) {
            const auto set = _g.lexChild[e.b + i * (e.min + 1) + d];
            if (set == kNoSet) continue;
            out.line("            ", first ? "" : "else ", "if (",
                     test(set, "p.at"), ") state = ", d, "u;");
            first = false;
          }
          out.line("            break;");
        }
        out.line("        }\n        ++p.at;\n        if (state == ", e.min,
                 "u) break;\n   "
                 "   }");
        out.line("      if (state == ", e.min, "u",
                 e.max != 0 ? " || p.at == len" : "", ") out.add(p);\n    }");
        break;
      default:
        break;
    }
    return out.line((opened ? "" : "  }\n"), "  return out;\n}").take();
  }
};

}  // namespace cursiv::engine
