// Indentation levels and token synthesis shared by both parser engines.
// Levels compare exact whitespace prefixes, so no tab width is assumed.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engines/shared/table/TermInfo.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

inline constexpr std::uint32_t kNoIndent = 0xFFFFFFFFu;

struct IndentLevel {
  TextOffset begin = 0;
  std::uint32_t prev = kNoIndent;
  TextOffset len = 0;
  // Invented by a repair: a line at the enclosing level closes it too.
  bool virt = false;
  // Its indent dropped by a repair: the dedent that closes it is the blank
  // it stood for.
  bool mute = false;
};

// Where a reading stands: its top level, and the brackets open around it.
struct IndentAt {
  std::uint32_t head = kNoIndent;
  std::uint32_t depth = 0;

  constexpr bool operator==(const IndentAt&) const = default;
};

// The terminal a table lays as `Kind`, or zero where the grammar has none.
template <const auto& C, std::uint8_t Kind>
inline constexpr SymbolId kLaid = [] {
  for (unsigned t = 1; t < C.nterms; ++t)
    if (C.terms[t].laid == Kind) return static_cast<SymbolId>(t);
  return SymbolId{0};
}();

// The grammar brackets a line: inside brackets a newline is blank.
template <const auto& C>
inline constexpr bool kBracketed = [] {
  for (unsigned t = 1; t < C.nterms; ++t)
    if (C.terms[t].delim != 0) return true;
  return false;
}();

// A reading owns its levels and its bracket depth where the grammar lays an
// indentation token or brackets a line.
template <const auto& C>
inline constexpr bool kIndented =
    kLaid<C, kLaidIndent> != 0 || kLaid<C, kLaidDedent> != 0 || kBracketed<C>;

// Shifting it moves what the indentation reads: a level or the depth.
template <const auto& C>
inline bool indent_moves(SymbolId t) {
  return (kLaid<C, kLaidIndent> != 0 && t == kLaid<C, kLaidIndent>) ||
         (kLaid<C, kLaidDedent> != 0 && t == kLaid<C, kLaidDedent>) ||
         (t < C.nterms && C.terms[t].delim != 0);
}

inline bool indent_byte(unsigned char b) { return b == ' ' || b == '\t'; }

// A line continued with a backslash starts no logical line. The backslash
// must stand in plain trivia, past `held()` -- the end of what tokens hold --
// since one a comment or a token ends on is that token's own byte.
template <class Held>
inline bool indent_line_start(const char* src, TextOffset p, Held&& held) {
  if (p == 0) return true;
  if (src[p - 1] != '\n' && src[p - 1] != '\r') return false;
  TextOffset q = p - 1;
  if (src[q] == '\n' && q != 0 && src[q - 1] == '\r') --q;
  return q == 0 || src[q - 1] != '\\' || q - 1 < held();
}

// The level's run against the line's: 0 equal, 1 the line is deeper, -1
// shallower, 2 neither one in front of the other.
inline int indent_cmp(const char* src, const IndentLevel* top, TextOffset b,
                      TextOffset n) {
  const TextOffset tn = top != nullptr ? top->len : 0;
  const auto m = n < tn ? n : tn;
  for (TextOffset i = 0; i < m; ++i)
    if (src[top->begin + i] != src[b + i]) return 2;
  if (n == tn) return top != nullptr && top->virt ? -1 : 0;
  return n > tn ? 1 : -1;
}

// A level is written once for every reading that opens it at one position:
// two arms of a cell push the same line, and the merge compares heads. The
// readings that open this line push last, so the search stops at a level
// opened on an earlier one.
inline std::uint32_t indent_push(std::vector<IndentLevel>& arena,
                                 std::uint32_t head, TextOffset begin,
                                 TextOffset len, bool virt, bool mute = false) {
  const auto n = static_cast<std::uint32_t>(arena.size());
  for (std::uint32_t i = n; i-- > 0 && arena[i].begin >= begin;) {
    const auto& l = arena[i];
    if (l.prev == head && l.begin == begin && l.len == len && l.virt == virt &&
        l.mute == mute)
      return i;
  }
  arena.push_back(IndentLevel{begin, head, len, virt, mute});
  return n;
}

// Whether two positions stand in the same levels at the same depth. A search
// can write one level twice, since `indent_push` finds a level again only
// among the latest pushes: the levels compare by content, down to one both
// share.
inline bool indent_same(const std::vector<IndentLevel>& arena, IndentAt a,
                        IndentAt b) {
  if (a.depth != b.depth) return false;
  for (; a.head != b.head && a.head != kNoIndent && b.head != kNoIndent;
       a.head = arena[a.head].prev, b.head = arena[b.head].prev) {
    const IndentLevel& u = arena[a.head];
    const IndentLevel& v = arena[b.head];
    if (u.begin != v.begin || u.len != v.len || u.virt != v.virt ||
        u.mute != v.mute)
      return false;
  }
  return a.head == b.head;
}

// The levels under those whose indent a repair dropped: what a reading's
// dedent closes, since a dropped indent opened no block.
inline std::uint32_t indent_unmute(const std::vector<IndentLevel>& arena,
                                   std::uint32_t head) {
  while (head != kNoIndent && arena[head].mute) head = arena[head].prev;
  return head;
}

// The end of input closes the levels no repair dropped.
inline bool indent_owes_dedent(const std::vector<IndentLevel>& arena,
                               std::uint32_t head) {
  return indent_unmute(arena, head) != kNoIndent;
}

inline std::uint32_t indent_pop(const std::vector<IndentLevel>& arena,
                                std::uint32_t head) {
  return head == kNoIndent ? kNoIndent : arena[head].prev;
}

// The token the leading run [p, q) says against the reading's top level. An
// indent covers its run; a dedent is zero-width, and the run stays for the
// next token to cross. A run that agrees with no open level is a token no mode
// reads: the parse says so instead of joining the line.
inline bool indent_token(const char* src, const std::vector<IndentLevel>& arena,
                         std::uint32_t head, SymbolId indentTerm,
                         SymbolId dedentTerm, TextOffset p, TextOffset q,
                         Token& out) {
  const IndentLevel* top = head == kNoIndent ? nullptr : &arena[head];
  const auto cmp = indent_cmp(src, top, p, q - p);
  if (cmp == 2)
    out = Token{p, q, kInvalidSymbol};
  else if (cmp == 1 && indentTerm != 0)
    out = Token{p, q, indentTerm};
  else if (cmp == -1 && dedentTerm != 0)
    out = Token{p, p, dedentTerm};
  else
    return false;
  return true;
}

// The token the indentation says at `p`, a line start. A blank line says
// nothing. A form feed in the run is a run no mode reads. A line back past a
// level whose indent a repair dropped closes it as the blank it stood for, and
// so does the end of the input. Past a last line with no newline the input's
// end is read first, and the drive hands the dedents over where that end is
// refused.
template <class Held>
inline bool indent_virtual_at(const char* src, TextOffset len,
                              const std::vector<IndentLevel>& arena,
                              std::uint32_t& head, SymbolId indentTerm,
                              SymbolId dedentTerm, TextOffset p, Token& out,
                              Held&& held, TextOffset* reached = nullptr) {
  if (!indent_line_start(src, p, held)) return false;
  TextOffset q = p;
  bool formFeed = false;
  for (; q < len; ++q) {
    if (src[q] == '\f')
      formFeed = true;
    else if (!indent_byte(static_cast<unsigned char>(src[q])))
      break;
  }
  if (reached != nullptr) *reached = q < len ? q + 1 : q;
  if (formFeed) {
    out = Token{p, q, kInvalidSymbol};
    return true;
  }
  if (q != len && (src[q] == '\n' || src[q] == '\r')) return false;
  while (head != kNoIndent && arena[head].mute &&
         (q == len || indent_cmp(src, &arena[head], p, q - p) == -1))
    head = arena[head].prev;
  if (q == len) return false;
  return indent_token(src, arena, head, indentTerm, dedentTerm, p, q, out);
}

// What shifting an indent does to a reading's levels: its run opens a level.
// A generated parser calls the helper of the kind it shifts, `indent_shift`
// the one of the token in hand.
inline void indent_shift_indent(std::vector<IndentLevel>& arena, IndentAt& at,
                                const Token& t) {
  at.head = indent_push(arena, at.head, t.begin, t.end - t.begin, false);
}

// ...a dedent: it closes the level under the muted ones.
inline void indent_shift_dedent(const std::vector<IndentLevel>& arena,
                                IndentAt& at) {
  at.head = indent_pop(arena, indent_unmute(arena, at.head));
}

// ...a terminal whose bracket kind is `delim`: +1 opens a bracket, -1 closes
// the innermost open one.
inline void indent_shift_delim(IndentAt& at, std::int8_t delim) {
  if (delim > 0)
    ++at.depth;
  else if (delim < 0 && at.depth != 0)
    --at.depth;
}

// What shifting `t` does to a reading's levels and bracket depth, `delim`
// being the terminal's own.
inline void indent_shift(std::vector<IndentLevel>& arena, IndentAt& at,
                         const Token& t, SymbolId indentTerm,
                         SymbolId dedentTerm, std::int8_t delim) {
  if (indentTerm != 0 && t.sym == indentTerm)
    indent_shift_indent(arena, at, t);
  else if (dedentTerm != 0 && t.sym == dedentTerm)
    indent_shift_dedent(arena, at);
  else
    indent_shift_delim(at, delim);
}

template <const auto& C>
inline void indent_shift(std::vector<IndentLevel>& arena, IndentAt& at,
                         const Token& t) {
  indent_shift(arena, at, t, kLaid<C, kLaidIndent>, kLaid<C, kLaidDedent>,
               t.sym < C.nterms ? C.terms[t.sym].delim : 0);
}

// What dropping `t` does to the levels from `head`: an indent still opens its
// level, muted, and a dedent closes the level under the muted ones. False
// where `t` moves no level.
inline bool indent_drop(std::vector<IndentLevel>& arena, std::uint32_t& head,
                        const Token& t, SymbolId indentTerm,
                        SymbolId dedentTerm) {
  if (indentTerm != 0 && t.sym == indentTerm)
    head = indent_push(arena, head, t.begin, t.end - t.begin, false, true);
  else if (dedentTerm != 0 && t.sym == dedentTerm)
    head = indent_pop(arena, indent_unmute(arena, head));
  else
    return false;
  return true;
}

// An indent a repair invents opens a level the next line at the enclosing
// one closes.
inline std::uint32_t indent_invent(std::vector<IndentLevel>& arena,
                                   std::uint32_t head) {
  const IndentLevel* top = head == kNoIndent ? nullptr : &arena[head];
  return indent_push(arena, head, top != nullptr ? top->begin : 0,
                     top != nullptr ? top->len : 0, true);
}

template <const auto& C>
inline void indent_shift_invented(std::vector<IndentLevel>& arena, IndentAt& at,
                                  const Token& t) {
  if (kLaid<C, kLaidIndent> != 0 && t.sym == kLaid<C, kLaidIndent>)
    at.head = indent_invent(arena, at.head);
  else
    indent_shift<C>(arena, at, t);
}

}  // namespace cursiv
