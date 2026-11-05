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

namespace cursiv {

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
        _kids(g.nlex, Kids::spelled),
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
      _kids[id] = kids(id);
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
    std::string out;
    for (const auto root : _roots)
      out += concat("bool lexical_predicate_", root,
                    "(const char*, ::cursiv::TextOffset,\n"
                    "    ::cursiv::TextOffset, ::cursiv::TextOffset&);\n");
    for (const auto root : _recognizers)
      out += concat("::cursiv::PredicateState lexical_recognizer_", root,
                    "(const char*, "
                    "::cursiv::TextOffset,\n"
                    "    ::cursiv::TextOffset, ::cursiv::TextOffset&);\n");
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
      // A node of a shape met before, or none uses, has no code.
      if ((_used[id] & (kDirect | kNode)) == 0) continue;
      if (dispatched(id)) out += first_table(id);
      if (worded(id)) out += word_table(id);
      if ((_used[id] & kDirect) != 0) out += direct(id);
      if ((_used[id] & kNode) != 0) out += node(id);
    }
    if (std::ranges::any_of(_order, [&](auto id) { return recursive(id); }))
      out += recursive_source();
    const auto started = [&](std::uint16_t root, std::string_view most) {
      return recursive(root)
                 ? concat("lexical_recursive(", root,
                          "u, src, len, "
                          "::cursiv::LexicalState{::cursiv::LexicalCursor{at}}"
                          ", seen, captures)")
                 : call(root,
                        "::cursiv::LexicalState{::cursiv::LexicalCursor{at}}",
                        most);
    };
    for (const auto root : _roots) {
      out += concat(
          "bool lexical_predicate_", root,
          "(const char* src, ::cursiv::TextOffset "
          "len,\n"
          "    ::cursiv::TextOffset at, ::cursiv::TextOffset& seen) {\n");
      if (fixed(root))
        out += concat("  return lexical_fixed_", same(root),
                      "(src, len, at, seen);\n");
      else {
        out += "  ::cursiv::LexicalCaptures captures;\n";
        out += "  return !" + started(root, "true") + ".empty();\n";
      }
      out += "}\n";
    }
    for (const auto root : _recognizers) {
      out += concat(
          "::cursiv::PredicateState lexical_recognizer_", root,
          "(const char* src, "
          "::cursiv::TextOffset len,\n"
          "    ::cursiv::TextOffset at, ::cursiv::TextOffset& seen) {\n");
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
  static constexpr std::uint8_t kVisited = 1, kDirect = 2, kNode = 4,
                                kFinal = 8;
  std::vector<std::uint8_t> _used;
  static constexpr std::uint32_t kVariableWidth = 0xffffffffu;
  std::vector<std::uint32_t> _width;
  mutable std::vector<std::optional<std::pair<ByteSet, bool>>> _first;
  std::vector<std::uint8_t> _recursive;
  // How a node reads its kids: each in turn, a choice dispatched on the byte
  // it opens on, or many words read as data (`LexicalWords`).
  enum class Kids : std::uint8_t { spelled, dispatched, words };
  std::vector<Kids> _kids;
  // Each node as the first node of its shape: a grammar that repeats an
  // expression has its code once.
  std::vector<std::uint16_t> _same;
  std::vector<std::uint16_t> _roots, _recognizers, _order;

  bool recursive(std::uint16_t id) const {
    return !_recursive.empty() && _recursive[id];
  }

  std::uint16_t same(std::uint16_t id) const { return _same[id]; }
  // What a node's code reads, where that is its kind, its bounds, its classes
  // and its kids' shapes alone; the others keep a code of their own.
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
        for (unsigned i = 0; i < e.min; ++i) key.push_back(_g.lexKid[e.b + i]);
        return true;
      case LexKind::seq:
      case LexKind::alt:
        for (unsigned i = 0; i < e.b; ++i)
          key.push_back(same(_g.lexKid[e.a + i]));
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
    if ((e.kind == LexKind::rep && _g.lex[e.a].kind == LexKind::chars) ||
        worded(id))
      return;
    const bool searched =
        e.kind == LexKind::through_nested || e.kind == LexKind::through_same;
    detail::lex_children(_g, id, [&](auto kid) {
      use(same(kid), !searched && (direct || dispatched(id)));
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
    return _kids[id] == Kids::dispatched;
  }
  bool worded(std::uint16_t id) const { return _kids[id] == Kids::words; }
  Kids kids(std::uint16_t id) const {
    const LexNode& e = _g.lex[id];
    if ((e.kind != LexKind::alt && e.kind != LexKind::seq) || recursive(id) ||
        e.b < kDispatchMin)
      return Kids::spelled;
    // Past what a mask holds, a choice of words or a run of refusals of words
    // is read as data: a class of characters over several bytes lowers to
    // hundreds of them.
    if (e.b > kDispatchMax) {
      for (unsigned i = 0; i < e.b; ++i)
        if (!(e.kind == LexKind::alt ? word(_g.lexKid[e.a + i])
                                     : refusal(_g.lexKid[e.a + i])))
          return Kids::spelled;
      return Kids::words;
    }
    if (e.kind != LexKind::alt) return Kids::spelled;
    for (unsigned i = 0; i < e.b; ++i) {
      const auto kid = _g.lexKid[e.a + i];
      if (recursive(kid) || !fixed(kid) || _width[kid] == 0 ||
          first_bytes(kid).second)
        return Kids::spelled;
    }
    return Kids::dispatched;
  }

  // A word is a class, or a sequence of classes; a refusal of one, an `nguard`
  // of a byte at least.
  bool word(std::uint16_t id) const {
    const LexNode& e = _g.lex[id];
    if (e.kind == LexKind::chars) return true;
    if (e.kind != LexKind::seq || e.b == 0) return false;
    for (unsigned i = 0; i < e.b; ++i)
      if (_g.lex[_g.lexKid[e.a + i]].kind != LexKind::chars) return false;
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
      const auto kid = _g.lexKid[e.a + i];
      const LexNode& k = _g.lex[kid];
      const auto at = static_cast<std::uint32_t>(words.size());
      if (k.kind == LexKind::nguard) {
        words.push_back(static_cast<std::uint16_t>(k.min));
        words.push_back(k.a);
        for (unsigned j = 0; j < k.min; ++j)
          words.push_back(_g.lexKid[k.b + j]);
      } else if (k.kind == LexKind::chars) {
        words.insert(words.end(), {1, kNoSet, k.a});
      } else {
        words.push_back(static_cast<std::uint16_t>(k.b));
        words.push_back(kNoSet);
        for (unsigned j = 0; j < k.b; ++j)
          words.push_back(_g.lex[_g.lexKid[k.a + j]].a);
      }
      const auto first = words[at + 2];
      const ByteSet opening = _g.sets[first] | lex_decode_bytes(_g, first);
      for (unsigned b = 0; b < 256; ++b)
        if (opening.has(static_cast<unsigned char>(b))) opens[b].push_back(at);
    }
    std::string out =
        concat("inline constexpr ::std::uint16_t lexical_list_", id, "[] = {");
    for (std::size_t i = 0; i < words.size(); ++i)
      out += concat(i % 16 == 0 ? "\n" : "", words[i], "u,");
    out += concat("\n};\ninline constexpr ::std::uint32_t lexical_opens_", id,
                  "[] = {");
    std::uint32_t n = 0;
    for (unsigned b = 0; b <= 256; ++b) {
      out += concat(b % 16 == 0 ? "\n" : "", n, "u,");
      if (b < 256) n += static_cast<std::uint32_t>(opens[b].size());
    }
    out += concat("\n};\ninline constexpr ::std::uint32_t lexical_order_", id,
                  "[] = {");
    n = 0;
    for (const auto& list : opens)
      for (const auto at : list)
        out += concat(n++ % 16 == 0 ? "\n" : "", at, "u,");
    if (n == 0) out += "0u";
    return out + concat(
                     "\n};\ninline constexpr ::cursiv::LexicalWords "
                     "lexical_words_",
                     id, "{lexical_list_", id, ", ", words.size(),
                     "u, "
                     "lexical_opens_",
                     id, ", lexical_order_", id, "};\n");
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
    std::string out = concat("inline constexpr ::std::uint32_t lexical_first_",
                             id, "[", 256 - from, "] = {");
    for (unsigned b = from; b < 256; ++b) {
      std::uint32_t mask = 0;
      for (unsigned i = 0; i < e.b; ++i)
        if (first_bytes(_g.lexKid[e.a + i])
                .first.has(static_cast<unsigned char>(b)))
          mask |= std::uint32_t{1} << i;
      out += concat(b % 16 == 0 ? "\n" : "", mask, "u,");
    }
    return out + "\n};\n";
  }

  // The readings of a dispatched choice the byte at `at` can open, in order;
  // `take` lays what to do with one that reads.
  std::string dispatch(std::uint16_t id, std::string_view indent,
                       std::string_view take, std::string_view miss) const {
    const LexNode& e = _g.lex[id];
    std::string out = concat(
        indent, "const auto b = static_cast<unsigned char>(src[c.at]);\n");
    const int ascii = ascii_reading(id);
    if (ascii >= 0)
      out += concat(indent, "if (b < 0x80) {\n", indent, "  auto p = c;\n",
                    indent, "  if (lexical_fixed_",
                    same(_g.lexKid[e.a + static_cast<unsigned>(ascii)]),
                    "(src, len, p.at, seen)) {\n", take, indent, "  }\n",
                    indent, "  return ", miss, ";\n", indent, "}\n");
    out += concat(indent, "for (auto m = lexical_first_", id, "[b",
                  ascii >= 0 ? " & 0x7fu" : "", "]; m != 0; m &= m - 1) {\n",
                  indent, "  auto p = c;\n", indent, "  bool read = false;\n",
                  indent, "  switch (::std::countr_zero(m)) {\n");
    for (unsigned i = 0; i < e.b; ++i)
      out += concat(indent, "    case ", i, "u: read = lexical_fixed_",
                    same(_g.lexKid[e.a + i]),
                    "(src, len, p.at, seen); "
                    "break;\n");
    out += concat(indent, "    default: break;\n", indent, "  }\n");
    out +=
        concat(indent, "  if (read) {\n", take, indent, "  }\n", indent, "}\n");
    return out;
  }

  std::string longest_step(std::string_view name, std::uint16_t kid,
                           std::string_view level) const {
    return concat("        auto ", name, " = ",
                  call(kid, "::cursiv::LexicalState{p}"),
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
    std::string out =
        concat("inline bool lexical_fixed_", id,
               "(const char* src, ::cursiv::TextOffset "
               "len,\n"
               "    ::cursiv::TextOffset& at, ::cursiv::TextOffset& seen) {\n");
    if (worded(id))
      out += concat("  return ::cursiv::lexical_",
                    e.kind == LexKind::alt ? "first_word" : "refused", "(",
                    _boundary, ", lexical_words_", id,
                    ", src, len, at, "
                    "seen);\n");
    else if (e.kind == LexKind::chars)
      out += concat(
          "  seen = ::std::max(seen, at < len ? at + 1 : len);\n"
          "  if (at >= len || !",
          test(e.a, "at"), ") return false;\n  ++at;\n  return true;\n");
    else if (e.kind == LexKind::seq) {
      for (unsigned i = 0; i < e.b; ++i)
        out += concat("  if (!lexical_fixed_", same(_g.lexKid[e.a + i]),
                      "(src, len, at, seen)) return false;\n");
      out += "  return true;\n";
    } else if (e.kind == LexKind::rep) {
      if (e.max != 0 && _g.lex[e.a].kind == LexKind::chars)
        out += concat(
            "  const auto begin = at;\n"
            "  while (at < len && at - begin < ",
            e.min,
            "u) {\n"
            "    seen = ::std::max(seen, at + 1);\n"
            "    if (!",
            test(_g.lex[e.a].a, "at"),
            ") return false;\n    ++at;\n  }\n"
            "  return at - begin == ",
            e.min, "u;\n");
      else if (e.max != 0)
        out += concat("  for (::std::uint32_t n = 0; n < ", e.min,
                      "u; ++n)\n"
                      "    if (!lexical_fixed_",
                      same(e.a),
                      "(src, len, at, seen)) return false;\n"
                      "  return true;\n");
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
        out += concat(
            "  { auto probe = at;\n"
            "    if (lexical_fixed_",
            same(_g.lexKid[e.a + i]),
            "(src, len, probe, seen)) {\n"
            "      at = probe;\n      return true;\n    }\n  }\n");
      out += "  return false;\n";
    } else if (e.kind == LexKind::positive || e.kind == LexKind::negative)
      out += concat("  auto probe = at;\n  return ",
                    e.kind == LexKind::negative ? "!" : "", "lexical_fixed_",
                    same(e.a),
                    "(src, len, probe, "
                    "seen);\n");
    else {
      out += "  auto probe = at;\n";
      for (unsigned i = 0; i < e.min; ++i)
        out += concat("  seen = ::std::max(seen, probe < len ? ", _boundary,
                      ".reach(", _g.lexKid[e.b + i],
                      "u, src, probe, "
                      "len) : len);\n"
                      "  if (probe >= len || !",
                      test(_g.lexKid[e.b + i], "probe"),
                      ") return true;\n  ++probe;\n");
      if (e.a == kNoSet)
        out += "  return false;\n";
      else
        out += concat(
            "  if (probe >= len) return false;\n"
            "  seen = ::std::max(seen, ",
            _boundary, ".reach(", e.a,
            "u, src, probe, len));\n"
            "  return ",
            _boundary, ".at(", e.a, "u, src, probe, len);\n");
    }
    return out + "}\n";
  }

  std::string request(std::uint16_t id, std::string_view in) const {
    if (recursive(id)) return concat("m.enter(", id, "u, ", in, "); return;\n");
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
      out += concat("    case ", id, "u: {\n");
      switch (const LexNode& e = _g.lex[id]; e.kind) {
        case LexKind::seq:
          out +=
              "      if (f.stage) {\n"
              "        f.cur = ::std::move(m.returned());\n"
              "        if (f.cur.empty()) { m.finish({}); return; }\n"
              "      } else { f.cur = ::std::move(f.in); f.stage = 1; }\n"
              "      switch (f.at++) {\n";
          for (unsigned i = 0; i < e.b; ++i)
            out += concat("      case ", i, "u: ") +
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
            out += concat("      case ", i, "u: ") +
                   request(_g.lexKid[e.a + i], "f.in");
          out +=
              "      default: m.finish(::std::move(f.out)); return;\n"
              "      }\n";
          break;
        case LexKind::rep:
          out += concat(
              "      if (!f.stage) {\n"
              "        f.cur = ::std::move(f.in);\n"
              "        f.stage = ",
              e.min == 0 ? 2 : 1, "u;\n");
          if (e.min == 0) out += "        f.out = f.cur;\n";
          if (e.min != 0) {
            out +=
                "      } else if (f.stage == 1) {\n"
                "        auto next = ::std::move(m.returned());\n"
                "        if (next == f.cur) { m.finish(::std::move(next)); "
                "return; }\n"
                "        f.cur = ::std::move(next);\n";
            out += concat("        if (++f.round >= ", e.min,
                          "u) { f.out = f.cur; f.stage = 2; "
                          "}\n");
          }
          out +=
              "      } else {\n        ++f.round;\n"
              "        f.cur = m.returned().without(f.out);\n"
              "        f.out.unite(f.cur);\n      }\n";
          out += concat(
              "      if (f.stage == 1 || (!f.cur.empty() && f.round < ", e.max,
              "u)) "
              "{\n        ");
          out += request(e.a, "f.cur") +
                 "      }\n      m.finish(::std::move(f.out)); return;\n";
          break;
        case LexKind::call: {
          const std::uint16_t callee = lex_callee(_g, e);
          out += concat(
              "      if (f.stage) {\n"
              "        m.remember(",
              callee,
              "u, f.in[f.at].at, m.returned());\n"
              "        f.out.unite(m.resume(f.in[f.at], m.returned()));\n"
              "        ++f.at; f.stage = 0;\n      }\n"
              "      while (f.at < f.in.size()) {\n"
              "        const auto c = f.in[f.at];\n"
              "        const auto local = ::cursiv::LexicalCursor{c.at};\n"
              "        if (const auto* cached = m.cached(",
              callee,
              "u, c.at)) {\n"
              "          f.out.unite(m.resume(c, *cached)); ++f.at; continue;\n"
              "        }\n        f.stage = 1;\n        ");
          out += request(callee, "::cursiv::LexicalState{local}") +
                 "      }\n      m.finish(::std::move(f.out)); return;\n";
          break;
        }
        case LexKind::positive:
        case LexKind::negative:
          out += "      if (f.stage) {\n";
          out += concat("        if (m.returned().empty() == ",
                        e.kind == LexKind::negative ? "true" : "false",
                        ") "
                        "f.out.add(f.in[f.at]);\n");
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
          out += concat("      m.finish(lexical_after_", id,
                        "(src, len, "
                        "::std::move(m.returned()), seen, captures)); "
                        "return;\n");
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
    std::string out =
        concat((_used[id] & kFinal) != 0 ? "template <bool Final>\n" : "",
               "inline ::cursiv::LexicalState lexical_",
               opened ? "after" : "node", "_", id,
               "(\n"
               "    const char* src, ::cursiv::TextOffset len,\n"
               "    ::cursiv::LexicalState in, ::cursiv::TextOffset& seen,\n"
               "    [[maybe_unused]] ::cursiv::LexicalCaptures& captures) {\n");
    if (e.kind == LexKind::rep && e.max == 0) return out + "  return in;\n}\n";
    if (e.kind == LexKind::seq && !fixed(id)) {
      for (unsigned i = 0; i < e.b; ++i)
        out += concat("  in = ",
                      call(_g.lexKid[e.a + i], "::std::move(in)",
                           i + 1 == e.b ? "Final" : "false"),
                      ";\n  if (in.empty()) return in;\n");
      return out + "  return in;\n}\n";
    }
    if (e.kind == LexKind::rep && !fixed(id) &&
        _g.lex[e.a].kind != LexKind::chars)
      return out + concat("  return ::cursiv::gap_repeat<", e.min, "u, ", e.max,
                          "u>(in,\n"
                          "      [&](const ::cursiv::LexicalState& cur) { "
                          "return ",
                          call(e.a, "cur"), "; });\n}\n");
    if (worded(id) && !fixed(id))
      return out + concat("  return ::cursiv::lexical_words<",
                          (_used[id] & kFinal) != 0 ? "Final" : "false", ">(",
                          _boundary,
                          ", "
                          "lexical_words_",
                          id, ", src, len, in, seen);\n}\n");
    out += "  ::cursiv::LexicalState out;\n";
    if (fixed(id))
      return concat(out,
                    "  for (::std::size_t k = 0; k < in.size(); ++k) {\n"
                    "    auto c = in[k];\n"
                    "    if (lexical_fixed_",
                    id,
                    "(src, len, c.at, seen)) out.add(c);\n"
                    "  }\n  return out;\n}\n");
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
          out += concat(
              "  for (::std::size_t k = 0; k < in.size(); ++k) {\n"
              "    auto c = in[k];\n"
              "    if (lexical_fixed_",
              same(_g.lexKid[e.a + i]),
              "(src, len, c.at, seen)) out.add(c);\n"
              "  }\n"
              "  if constexpr (Final) if (!out.empty()) return out;\n");
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
      out += concat(
          "    auto starts = ", call(e.a, "::cursiv::LexicalState{c}"), ";\n");
    switch (e.kind) {
      case LexKind::positive:
      case LexKind::negative:
        out += concat(
            "    if (", call(e.a, "::cursiv::LexicalState{c}", "true"),
            ".empty() == ", e.kind == LexKind::negative ? "true" : "false",
            ") out.add(c);\n");
        break;
      case LexKind::rep: {
        out += "    const auto begin = c.at;\n";
        if (e.min == 0) out += "    if constexpr (!Final) out.add(c);\n";
        out += concat("    while (c.at < len && c.at - begin < ", e.max,
                      "u) {\n"
                      "      seen = ::std::max(seen, c.at + 1);\n"
                      "      if (!",
                      test(_g.lex[e.a].a, "c.at"), ") break;\n      ++c.at;\n");
        const auto enough = e.min == 0
                                ? std::string{}
                                : concat("if (c.at - begin >= ", e.min, "u) ");
        out += concat("      if constexpr (!Final) ", enough,
                      "out.add(c);\n    }\n"
                      "    if constexpr (Final) ",
                      enough, "out.add(c);\n");
        break;
      }
      case LexKind::mark:
        out += concat(
            "    auto ends = ", call(e.a, "::cursiv::LexicalState{c}"), ";\n");
        out += concat(
            "    for (::std::size_t j = 0; j < ends.size(); ++j) {\n"
            "      auto p = ends[j];\n"
            "      p.captures = captures.bind(p.captures, ",
            e.min,
            "u, c.at, p.at);\n"
            "      out.add(p);\n    }\n");
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
        out += concat("      if (depth == 0", e.max ? " || p.at == len" : "",
                      ") out.add(p);\n    }\n");
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
          out += concat(
              "      const auto* capture = captures.find(p.captures, ", e.max,
              "u);\n"
              "      if (capture == nullptr) { seen = ::std::max(seen, len); "
              "continue; }\n"
              "      const char* hit = ::cursiv::lex_find_closer(\n"
              "          {src + p.at, len - p.at},\n"
              "          {",
              c_literal(before), ", ", before.size(),
              "u}, {src + capture->begin, capture->end - "
              "capture->begin},\n"
              "          {",
              c_literal(after), ", ", after.size(),
              "u}, [&](const char* q) {\n"
              "            return ::cursiv::lex_character_boundary(\n"
              "                {src + p.at, len - p.at}, q);\n"
              "          });\n"
              "      if (hit == nullptr) { seen = ::std::max(seen, len); "
              "continue; }\n"
              "      p.at = static_cast<::cursiv::TextOffset>(hit - src) + ",
              before.size() + after.size(),
              "u + capture->end - capture->begin;\n"
              "      seen = ::std::max(seen, p.at);\n"
              "      out.add(p);\n    }\n");
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
            "        ::std::size_t width = 0;\n"
            "        (void)::cursiv::scalar_at(src + p.at, len - p.at, width);\n"
            "        p.at += static_cast<::cursiv::TextOffset>(\n"
            "            width != 0 ? width : 1);\n"
            "        seen = ::std::max(seen, p.at);\n"
            "      }\n    }\n";
        break;
      case LexKind::through_lit:
        out +=
            "    for (::std::size_t j = 0; j < starts.size(); ++j) {\n"
            "      auto p = starts[j];\n      unsigned state = 0;\n"
            "      while (p.at < len) {\n"
            "        seen = ::std::max(seen, p.at + 1);\n"
            "        switch (state) {\n";
        for (unsigned i = 0; i < e.min; ++i) {
          out += concat("          case ", i, "u:\n");
          bool first = true;
          for (unsigned d = 0; d <= e.min; ++d) {
            const auto set = _g.lexKid[e.b + i * (e.min + 1) + d];
            if (set == kNoSet) continue;
            out += concat("            ", first ? "" : "else ", "if (",
                          test(set, "p.at"), ") state = ", d, "u;\n");
            first = false;
          }
          out += "            break;\n";
        }
        out +=
            concat("        }\n        ++p.at;\n        if (state == ", e.min,
                   "u) break;\n   "
                   "   }\n");
        out += concat("      if (state == ", e.min, "u",
                      e.max != 0 ? " || p.at == len" : "",
                      ") out.add(p);\n    }\n");
        break;
      default:
        break;
    }
    return out + (opened ? "" : "  }\n") + "  return out;\n}\n";
  }
};

}  // namespace cursiv
