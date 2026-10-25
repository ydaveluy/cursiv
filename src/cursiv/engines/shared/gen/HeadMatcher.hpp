// A set of words read at the head of a token, compiled to one matcher: a
// trie over the bytes from the token's first, which stops at the first byte
// no word takes.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/LexNfaPool.hpp"
#include "cursiv/engines/shared/gen/lex_intern_set.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

struct HeadMatcher {
  // A byte the word reads, or, `refused`, one it refuses where it stands
  // without reading it; `lead` the bytes at which a scalar decides that.
  struct Step {
    ByteSet bytes{};
    ByteSet lead{};
    bool refused = false;
    // Where `scalarRanges` lists the scalars `lead` decodes at, as
    // `lex_set_scalars` reads it; zero for the generated grammar's word class.
    std::uint32_t scalars = 0;
  };
  // The expressions naming the ranges of scalars: the word class's, and the
  // pool the sets of a section list theirs in.
  struct Ranges {
    std::string_view word, pool;
  };
  std::vector<std::vector<Step>> words;
  // The steps every word opens on that the token reads before them: `refuses`
  // starts past them, `reach` reads them as the token does, its walk dying
  // where one is left.
  std::uint32_t offset = 0;
  // What a matcher counts as read past `p`: nothing; or past a word spelled
  // whole the byte that would rescue it, and each byte a word's prefix read
  // up to the first that leaves it, the end of the text included -- `walked`,
  // that one left where a walk dies, at the end of what it read.
  enum class Reach : std::uint8_t { none, prefixes, walked };

  // `bool name(const char* p, TextOffset n, TextOffset rest)`: whether a word
  // starts at `p`, reading at most `n` bytes; a refusal looks as far as
  // `rest`, and `ranges` names the scalars of its class. Past `Reach::none`, a
  // last `TextOffset& seen` rises past what `reach` counts as read.
  std::string source(std::string_view linkage, std::string_view name,
                     const Ranges& ranges, Reach reach = Reach::none) const {
    std::string out = std::format(
        "{} bool {}(const char* p, ::cursiv::TextOffset n,\n"
        "    ::cursiv::TextOffset rest{}) {{\n"
        "  (void)n;\n  (void)rest;\n",
        linkage, name,
        reach != Reach::none ? ", ::cursiv::TextOffset& seen" : "");
    std::vector<ByteSet> sets;
    for (const auto& word : words)
      for (const Step& s : word)
        for (const ByteSet& b : {s.bytes, s.lead})
          if (s.refused && b.any() &&
              std::find(sets.begin(), sets.end(), b) == sets.end()) {
            out += std::format(
                "  static constexpr ::cursiv::ByteSet r{}{{{{{}ull, {}ull, "
                "{}ull, {}ull}}}};\n",
                sets.size(), b.w[0], b.w[1], b.w[2], b.w[3]);
            sets.push_back(b);
          }
    const std::uint32_t from = reach == Reach::none ? offset : 0;
    Items all;
    for (std::uint32_t w = 0; w < words.size(); ++w) all.push_back({w, from});
    node(out, from, std::move(all), 1, ranges, sets, reach);
    out += "}\n";
    return out;
  }

 private:
  using Items = std::vector<std::pair<std::uint32_t, std::uint32_t>>;

  void node(std::string& out, std::uint32_t d, Items items, unsigned depth,
            const Ranges& ranges, const std::vector<ByteSet>& sets,
            Reach reach) const {
    const std::string pad(depth * 2u, ' ');
    const bool prefix = reach != Reach::none && d != 0;
    for (const auto& [w, i] : items)
      if (i == words[w].size()) {
        if (reach != Reach::none && (i == 0 || !words[w][i - 1].refused))
          out += pad + std::format(
                           "if (rest > {0}u && seen < {1}u) seen = {1}u;\n", d,
                           d + 1u);
        out += pad + "return true;\n";
        return;
      }
    const auto set_of = [&](const ByteSet& b) {
      return std::format(
          "r{}", std::find(sets.begin(), sets.end(), b) - sets.begin());
    };
    for (const auto& [w, i] : items) {
      const Step& r = words[w][i];
      if (!r.refused) continue;
      Items kept, passed;
      for (const auto& [v, j] : items) {
        const Step& s = words[v][j];
        const bool same = s.refused && s.bytes == r.bytes && s.lead == r.lead &&
                          s.scalars == r.scalars;
        if (!same) kept.push_back({v, j});
        passed.push_back({v, same ? j + 1u : j});
      }
      const std::string c =
          std::format("static_cast<unsigned char>(p[{}u])", d);
      std::string test =
          r.bytes.any() ? std::format("{}.has({})", set_of(r.bytes), c)
                        : std::string("false");
      if (r.lead.any())
        test += std::format(
            " || ({}.has({}) && ::cursiv::lex_scalar_in({}, p + {}u, rest - "
            "{}u))",
            set_of(r.lead), c,
            r.scalars != 0 ? std::format("{} + {}u, {}u", ranges.pool,
                                         r.scalars >> 16, r.scalars & 0xFFFFu)
                           : std::string(ranges.word),
            d, d);
      if (reach != Reach::none)
        out += pad + std::format(
                         "if (rest > {0}u) {{\n{1}  const ::cursiv::TextOffset w "
                         "= {2};\n{1}  if (seen < {0}u + w) seen = {0}u + w;\n{1}}}\n",
                         d, pad,
                         r.lead.any()
                             ? std::format(
                                   "{}.has({}) ? ::std::min<::cursiv::TextOffset>("
                                   "::cursiv::utf8_width(p[{}u]), rest - {}u) : 1u",
                                   set_of(r.lead), c, d, d)
                             : std::string("1u"));
      out += pad + std::format("if (rest > {}u && ({})) {{\n", d, test);
      node(out, d, std::move(kept), depth + 1u, ranges, sets, reach);
      out += pad + "}\n";
      node(out, d, std::move(passed), depth, ranges, sets, reach);
      return;
    }
    if (items.empty()) {
      out += pad + "return false;\n";
      return;
    }
    std::array<Items, 256> next;
    for (unsigned b = 0; b < 256; ++b)
      for (const auto& [w, i] : items)
        if (words[w][i].bytes.has(static_cast<unsigned char>(b)))
          next[b].push_back({w, i + 1u});
    if (prefix)
      out += pad + std::format(
                       "if (n <= {0}u) {{\n{1}  if (seen < {0}u) seen = {0}u;\n"
                       "{1}  return false;\n{1}}}\n",
                       d, pad);
    else
      out += pad + std::format("if (n <= {}u) return false;\n", d);
    out += pad + std::format(
                     "switch (static_cast<unsigned char>(p[{}u])) {{\n", d);
    std::array<bool, 256> done{};
    for (unsigned b = 0; b < 256; ++b) {
      if (done[b] || next[b].empty()) continue;
      for (unsigned c = b; c < 256; ++c)
        if (!done[c] && next[c] == next[b]) {
          out += pad + std::format("  case {}u:\n", c);
          done[c] = true;
        }
      out += pad + "  {\n";
      node(out, d + 1u, next[b], depth + 2u, ranges, sets, reach);
      out += pad + "  }\n";
    }
    out += pad + "  default:\n";
    if (prefix)
      out += pad + std::format("    if (seen < {0}u) seen = {0}u;\n",
                               reach == Reach::walked || d < offset ? d
                                                                    : d + 1u);
    out += pad + "    return false;\n" + pad + "}\n";
  }
};

// The function `sig` answering for terminal `t` through its matcher `fn_<t>`.
inline std::string lex_head_switch(const std::vector<HeadMatcher>& heads,
                                   std::string_view sig, std::string_view fn) {
  std::string out = std::format("{} {{\n  switch (t) {{\n", sig);
  for (std::size_t t = 0; t < heads.size(); ++t)
    if (!heads[t].words.empty())
      out += std::format("    case {}u:\n      return {}_{}(p, rest, rest);\n",
                         t, fn, t);
  return out + "    default:\n      return false;\n  }\n}\n";
}

// Each terminal's matcher; `refuses`, the function answering for terminal `t`
// at `p` with `rest` bytes left whether its token there spells a word; and
// `reach`, the one answering for mode `m` how far its terminals' words at `p`
// are read, as `modeHeads` names them per mode and `read` counts.
inline std::string lex_head_source(const std::vector<HeadMatcher>& heads,
                                   const std::vector<DynSet>& modeHeads,
                                   std::string_view refuses,
                                   std::string_view reach,
                                   const HeadMatcher::Ranges& ranges,
                                   HeadMatcher::Reach read) {
  std::string out;
  for (std::size_t t = 0; t < heads.size(); ++t)
    if (!heads[t].words.empty())
      out += heads[t].source("CURSIV_NOINLINE inline",
                             std::format("refuses_{}", t), ranges) +
             heads[t].source("inline", std::format("reaches_{}", t), ranges,
                             read);
  out += lex_head_switch(heads, refuses, "refuses");
  out += std::format(
      "{} {{\n  ::cursiv::TextOffset seen = 0;\n  switch (m) {{\n", reach);
  std::vector<bool> done(modeHeads.size(), false);
  for (std::size_t m = 0; m < modeHeads.size(); ++m) {
    if (done[m] || modeHeads[m].none()) continue;
    for (std::size_t k = m; k < modeHeads.size(); ++k)
      if (!done[k] && modeHeads[k] == modeHeads[m]) {
        out += std::format("    case {}u:\n", k);
        done[k] = true;
      }
    for (std::size_t t = 0; t < heads.size(); ++t)
      if (modeHeads[m].test(static_cast<unsigned>(t)))
        out += std::format("      reaches_{}(p, rest, rest, seen);\n", t);
    out += "      break;\n";
  }
  out += "    default:\n      break;\n  }\n  return seen;\n}\n";
  return out;
}

// The source of `tail`, answering for terminal `t` whether one of the words
// it refuses where its token ends is spelled at `p`, `rest` bytes left.
inline std::string lex_tail_source(const std::vector<HeadMatcher>& tails,
                                   std::string_view tail,
                                   const HeadMatcher::Ranges& ranges) {
  std::string out;
  for (std::size_t t = 0; t < tails.size(); ++t)
    if (!tails[t].words.empty())
      out += tails[t].source("CURSIV_NOINLINE inline",
                             std::format("trails_{}", t), ranges);
  return out + lex_head_switch(tails, tail, "trails");
}

// The refusals a tree of splits opens on when every leaf is one, false
// otherwise.
inline bool lex_split_refusals(const LexNfaPool& n, int x,
                               std::vector<std::size_t>& leaves,
                               unsigned depth = 0) {
  if (x < 0 || depth > 32) return false;
  for (std::size_t k = 0; k < n.guards.size(); ++k)
    if (n.guards[k].hole == x) {
      leaves.push_back(k);
      return true;
    }
  const LexNfaState& st = n.s[static_cast<std::size_t>(x)];
  return st.set < 0 && st.accept < 0 && st.relay < 0 && st.t1 >= 0 &&
         st.t2 >= 0 && lex_split_refusals(n, st.t1, leaves, depth + 1) &&
         lex_split_refusals(n, st.t2, leaves, depth + 1);
}

// The spine of a terminal from `start`: the classes of one byte every match
// reads alike and the refusals in a row among them. `at(k, head)` meets guard
// `k` behind the classes `head`, `end(head)` the match where they all end. A
// split whose every branch opens on the refusal of one word ends the spine,
// that refusal standing in each.
template <class Cfg, class At, class End>
void lex_spine(const LexNfaPool& n, const Cfg& g, int start, At&& at,
               End&& end) {
  std::vector<HeadMatcher::Step> head;
  for (int x = start; x >= 0;) {
    std::size_t k = 0;
    while (k < n.guards.size() && n.guards[k].hole != x) ++k;
    if (k < n.guards.size()) {
      at(k, head);
      x = n.guards[k].out;
      continue;
    }
    const LexNfaState& st = n.s[static_cast<std::size_t>(x)];
    if (st.accept >= 0 && st.t1 < 0 && st.t2 < 0) end(head);
    if (st.set < 0 && st.t2 >= 0) {
      std::vector<std::size_t> leaves;
      if (lex_split_refusals(n, x, leaves) &&
          std::ranges::all_of(leaves, [&](std::size_t j) {
            return n.guards[j].node == n.guards[leaves[0]].node;
          }))
        for (const std::size_t j : leaves) at(j, head);
      return;
    }
    if (st.set < 0 || st.minus >= 0 || st.t2 >= 0) return;
    head.push_back({g.sets[static_cast<std::size_t>(st.set)], {}, false});
    x = st.t1;
  }
}

// How far behind its terminal's start every match reads a refusal of more
// than one class standing on the spine; -1 for any other refusal.
template <class Cfg>
std::vector<int> lex_spine_offsets(const LexNfaPool& n, const Cfg& g,
                                   const std::vector<int>& startOf) {
  std::vector<int> out(n.guards.size(), -1);
  for (const int start : startOf)
    lex_spine(
        n, g, start,
        [&](std::size_t k, const std::vector<HeadMatcher::Step>& head) {
          const LexNode e = g.lex[n.guards[k].node];
          if (e.min > 1 || e.a != kNoSet) out[k] = static_cast<int>(head.size());
        },
        [](const std::vector<HeadMatcher::Step>&) {});
  return out;
}

// The words of the spine refusals `taken` marks, taken off each terminal's
// graph: the refusal leaves its hole open onto the body, and a matcher asks
// it of the token instead, opening on the head's classes. `tails` holds,
// without the head, the words standing where every match ends, which a
// re-reading that misspells the head asks at its own end.
template <class Cfg>
std::vector<HeadMatcher> lex_head_words(LexNfaPool& n, const Cfg& g,
                                        const std::vector<int>& startOf,
                                        const std::vector<bool>& taken,
                                        std::vector<HeadMatcher>& tails) {
  std::vector<HeadMatcher> out(startOf.size());
  tails.assign(startOf.size(), HeadMatcher{});
  const bool scalars = g.wordRanges.size() != 0;
  for (std::size_t t = 0; t < startOf.size(); ++t) {
    std::vector<std::size_t> at;
    std::vector<std::pair<std::uint16_t, std::size_t>> placed;
    lex_spine(
        n, g, startOf[t],
        [&](std::size_t k, const std::vector<HeadMatcher::Step>& head) {
          if (!taken[k]) return;
          const LexNfaGuard gd = n.guards[k];
          n.s[static_cast<std::size_t>(gd.hole)].t1 = gd.out;
          const std::pair<std::uint16_t, std::size_t> here{gd.node,
                                                           head.size()};
          if (std::ranges::find(placed, here) != placed.end()) return;
          placed.push_back(here);
          const LexNode e = g.lex[gd.node];
          if (out[t].words.empty())
            out[t].offset = static_cast<std::uint32_t>(head.size());
          std::vector<HeadMatcher::Step> word = head;
          for (unsigned i = 0; i < e.min; ++i)
            word.push_back({g.sets[g.lexKid[e.b + i]], {}, false});
          if (e.a != kNoSet) {
            HeadMatcher::Step rescue{g.sets[e.a], {}, true};
            if (g.setDecode[e.a] != kNoSet &&
                (scalars || lex_set_scalars(g, e.a) != 0)) {
              rescue.lead = g.sets[g.setDecode[e.a]];
              rescue.scalars = lex_set_scalars(g, e.a);
            }
            word.push_back(rescue);
          }
          out[t].words.push_back(std::move(word));
          at.push_back(head.size());
        },
        [&](const std::vector<HeadMatcher::Step>& head) {
          for (std::size_t w = 0; w < at.size(); ++w)
            if (at[w] == head.size())
              tails[t].words.emplace_back(
                  out[t].words[w].begin() +
                      static_cast<std::ptrdiff_t>(head.size()),
                  out[t].words[w].end());
        });
  }
  return out;
}

}  // namespace cursiv
