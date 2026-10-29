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
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/LexNfaPool.hpp"
#include "cursiv/engines/shared/gen/SetIndex.hpp"
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

  // `bool name(const char* p, TextOffset n, TextOffset rest)`: whether a word
  // starts at `p`, reading at most `n` bytes; a refusal looks as far as
  // `rest`, and `ranges` names the scalars of its class. With `reach`, a last
  // `TextOffset& seen` rises as a walk's halt does: to the byte no word
  // continues with, or to the end of the text; past a word spelled whole, to
  // past the byte that would rescue it, or to the end of the text where none
  // follows.
  std::string source(std::string_view linkage, std::string_view name,
                     const Ranges& ranges, bool reach = false) const {
    std::string out = std::format(
        "{} bool {}(const char* p, ::cursiv::TextOffset n,\n"
        "    ::cursiv::TextOffset rest{}) {{\n"
        "  (void)p;\n  (void)n;\n  (void)rest;\n",
        linkage, name, reach ? ", ::cursiv::TextOffset& seen" : "");
    using Items = std::vector<std::pair<std::uint32_t, std::uint32_t>>;
    enum Kind { miss, accept, read, refuse };
    struct Row {
      std::uint32_t at = 0, first = 0, count = 0, yes = 0, no = 0, scalars = 0;
      Kind kind = miss;
    };
    struct Edge {
      std::uint32_t set, next;
    };
    struct Work {
      std::uint32_t at;
      Items items;
    };
    std::vector<ByteSet> sets;
    std::unordered_map<detail::SetKey, std::uint32_t, detail::SetKeyHash>
        setIndex;
    const auto set_of = [&](const ByteSet& bytes) {
      const auto [at, fresh] = setIndex.try_emplace(
          detail::SetKey{bytes}, static_cast<std::uint32_t>(sets.size()));
      if (fresh) sets.push_back(bytes);
      return at->second;
    };
    std::vector<Row> rows;
    std::vector<Edge> edges;
    std::vector<Work> work;
    const auto add = [&](std::uint32_t at, Items items) {
      const auto id = static_cast<std::uint32_t>(work.size());
      work.push_back({at, std::move(items)});
      return id;
    };
    const std::uint32_t from = reach ? 0 : offset;
    Items all;
    for (std::uint32_t w = 0; w < words.size(); ++w) all.push_back({w, from});
    add(from, std::move(all));
    bool refused = false, decoded = false, ownScalars = false;
    for (std::size_t at = 0; at < work.size(); ++at) {
      // Enqueuing a child may move the worklist. Keep this node by value and
      // release its items once it has been emitted, rather than retaining a
      // copy of every prefix in the trie.
      auto [d, items] = std::move(work[at]);
      Row row{.at = d};
      const auto ended = std::ranges::find_if(items, [&](const auto& item) {
        return item.second == words[item.first].size();
      });
      if (ended != items.end()) {
        row.kind = accept;
        row.yes = ended->second == 0 ||
                  !words[ended->first][ended->second - 1].refused;
      } else if (!items.empty()) {
        const auto guard = std::ranges::find_if(items, [&](const auto& item) {
          return words[item.first][item.second].refused;
        });
        if (guard != items.end()) {
          const Step r = words[guard->first][guard->second];
          Items kept, passed;
          for (const auto& [w, i] : items) {
            const Step& s = words[w][i];
            const bool same = s.refused && s.bytes == r.bytes &&
                              s.lead == r.lead && s.scalars == r.scalars;
            if (!same) kept.push_back({w, i});
            passed.push_back({w, same ? i + 1u : i});
          }
          row.kind = refuse;
          row.first = set_of(r.bytes);
          row.count = set_of(r.lead);
          row.scalars = r.scalars;
          row.yes = add(d, std::move(kept));
          row.no = add(d, std::move(passed));
          refused = true;
          decoded = decoded || r.lead.any();
          ownScalars = ownScalars || (r.lead.any() && r.scalars != 0);
        } else {
          std::array<Items, 256> next;
          for (unsigned b = 0; b < 256; ++b)
            for (const auto& [w, i] : items)
              if (words[w][i].bytes.has(static_cast<unsigned char>(b)))
                next[b].push_back({w, i + 1u});
          row.kind = read;
          row.first = static_cast<std::uint32_t>(edges.size());
          std::array<bool, 256> done{};
          for (unsigned b = 0; b < 256; ++b) {
            if (done[b] || next[b].empty()) continue;
            ByteSet bytes;
            for (unsigned c = b; c < 256; ++c)
              if (!done[c] && next[c] == next[b]) {
                bytes.add(static_cast<unsigned char>(c));
                done[c] = true;
              }
            edges.push_back({set_of(bytes), add(d + 1u, std::move(next[b]))});
          }
          row.count = static_cast<std::uint32_t>(edges.size()) - row.first;
        }
      }
      rows.push_back(row);
    }
    out += "  [[maybe_unused]] static constexpr ::cursiv::ByteSet sets[] = {\n";
    for (const ByteSet& s : sets)
      out += std::format("    {{{{{}ull, {}ull, {}ull, {}ull}}}},\n", s.w[0],
                         s.w[1], s.w[2], s.w[3]);
    if (sets.empty()) out += "    {},\n";
    out += "  };\n";
    // Small tries stay direct, with flat labels instead of nesting one
    // switch per byte. The bound also bounds the emitted function's graph.
    if (rows.size() <= 16) {
      for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        out += i == 0 ? "  {\n" : std::format("  s{}: {{\n", i);
        if (r.kind == accept) {
          if (reach && r.yes)
            out += std::format(
                "    if (seen <= {0}u)\n"
                "      seen = rest > {0}u ? {0}u + 1u : rest;\n",
                r.at);
          out += "    return true;\n";
        } else if (r.kind == miss) {
          out += "    return false;\n";
        } else if (r.kind == refuse) {
          const std::string c =
              std::format("static_cast<unsigned char>(p[{}u])", r.at);
          const auto bytes = std::format("sets[{}u].has({})", r.first, c);
          const auto lead = std::format("sets[{}u].has({})", r.count, c);
          out += std::format("    if (rest > {}u) {{\n", r.at);
          if (reach) {
            out += "      const ::cursiv::TextOffset width = ";
            out += sets[r.count].any()
                       ? std::format(
                             "{} ? "
                             "::std::min<::cursiv::TextOffset>(::cursiv::utf8_"
                             "width(p[{}u]), rest - {}u) : 1u;\n",
                             lead, r.at, r.at)
                       : "1u;\n";
            out += std::format(
                "      if (seen < {0}u + width) seen = {0}u + width;\n", r.at);
          }
          std::string test = bytes;
          if (sets[r.count].any())
            test += std::format(
                " || ({} && ::cursiv::lex_scalar_in({}, p + {}u, rest - {}u))",
                lead,
                r.scalars == 0
                    ? std::string(ranges.word)
                    : std::format("{} + {}u, {}u", ranges.pool, r.scalars >> 16,
                                  r.scalars & 0xFFFFu),
                r.at, r.at);
          out += std::format("      if ({}) goto s{};\n    }}", test, r.yes);
          if (reach)
            out += " else if (seen < rest) {\n      seen = rest;\n    }";
          out += std::format("\n    goto s{};\n", r.no);
        } else {
          out += std::format("    if (n <= {}u) {{\n", r.at);
          if (reach && r.at != 0)
            out += std::format("      if (seen < {0}u) seen = {0}u;\n", r.at);
          out += "      return false;\n    }\n";
          out += std::format(
              "    switch (static_cast<unsigned char>(p[{}u])) {{\n", r.at);
          for (auto e = r.first; e < r.first + r.count; ++e) {
            for (unsigned b = 0; b < 256; ++b)
              if (sets[edges[e].set].has(static_cast<unsigned char>(b)))
                out += std::format("      case {}u:\n", b);
            out += std::format("        goto s{};\n", edges[e].next);
          }
          out += "      default: break;\n    }\n";
          if (reach && r.at != 0)
            out += std::format("    if (seen < {0}u) seen = {0}u;\n", r.at);
          out += "    return false;\n";
        }
        out += "  }\n";
      }
      return out + "}\n";
    }
    // A table keeps long spellings out of the C++ and Wasm control-flow
    // graphs. Both generation and matching use constant call-stack space.
    out +=
        "  struct Row { ::std::uint32_t at, first, count, yes, no, scalars, "
        "kind; };\n"
        "  struct Edge { ::std::uint32_t set, next; };\n"
        "  static constexpr Edge edges[] = {\n";
    for (const Edge& e : edges)
      out += std::format("    {{{}u, {}u}},\n", e.set, e.next);
    if (edges.empty()) out += "    {},\n";
    out += "  };\n  static constexpr Row rows[] = {\n";
    for (const Row& r : rows)
      out += std::format("    {{{}u, {}u, {}u, {}u, {}u, {}u, {}u}},\n", r.at,
                         r.first, r.count, r.yes, r.no, r.scalars,
                         static_cast<unsigned>(r.kind));
    out +=
        "  };\n  ::std::uint32_t state = 0;\n  for (;;) {\n"
        "    const auto& r = rows[state];\n"
        "    if (r.kind == 0u) return false;\n"
        "    if (r.kind == 1u) {\n";
    if (reach)
      out +=
          "      if (r.yes && seen <= r.at)\n"
          "        seen = rest > r.at ? r.at + 1u : rest;\n";
    out += "      return true;\n    }\n";
    if (refused) {
      out +=
          "    if (r.kind == 3u) {\n"
          "      bool hit = false;\n      if (rest > r.at) {\n"
          "        const auto c = static_cast<unsigned char>(p[r.at]);\n";
      if (reach) {
        out += "        const ::cursiv::TextOffset width = ";
        out += decoded ? "sets[r.count].has(c) ? "
                         "::std::min<::cursiv::TextOffset>(::cursiv::utf8_"
                         "width(p[r.at]), rest - r.at) : 1u;\n"
                       : "1u;\n";
        out += "        if (seen < r.at + width) seen = r.at + width;\n";
      }
      out += "        hit = sets[r.first].has(c)";
      if (decoded) {
        out += " || (sets[r.count].has(c) &&\n            ";
        if (ownScalars)
          out += std::format(
              "(r.scalars != 0u\n"
              "             ? ::cursiv::lex_scalar_in({} + (r.scalars >> 16), "
              "r.scalars & 0xFFFFu, p + r.at, rest - r.at)\n"
              "             : ::cursiv::lex_scalar_in({}, p + r.at, rest - "
              "r.at))",
              ranges.pool, ranges.word);
        else
          out +=
              std::format("::cursiv::lex_scalar_in({}, p + r.at, rest - r.at)",
                          ranges.word);
        out += ")";
      }
      out += ";\n      }";
      if (reach)
        out += " else if (seen < rest) {\n        seen = rest;\n      }";
      out += "\n      state = hit ? r.yes : r.no;\n      continue;\n    }\n";
    }
    out += "    if (n <= r.at) {\n";
    if (reach) out += "      if (r.at != 0u && seen < r.at) seen = r.at;\n";
    out +=
        "      return false;\n    }\n"
        "    const auto c = static_cast<unsigned char>(p[r.at]);\n"
        "    bool found = false;\n"
        "    for (auto e = r.first; e < r.first + r.count; ++e) {\n"
        "      if (!sets[edges[e].set].has(c)) continue;\n"
        "      state = edges[e].next;\n      found = true;\n      break;\n    "
        "}\n"
        "    if (found) continue;\n";
    if (reach) out += "    if (r.at != 0u && seen < r.at) seen = r.at;\n";
    return out + "    return false;\n  }\n}\n";
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
// are read, as `modeHeads` names them per mode.
inline std::string lex_head_source(const std::vector<HeadMatcher>& heads,
                                   const std::vector<DynSet>& modeHeads,
                                   std::string_view refuses,
                                   std::string_view reach,
                                   const HeadMatcher::Ranges& ranges) {
  std::string out;
  for (std::size_t t = 0; t < heads.size(); ++t)
    if (!heads[t].words.empty())
      out +=
          heads[t].source("CURSIV_NOINLINE inline",
                          std::format("refuses_{}", t), ranges) +
          heads[t].source("inline", std::format("reaches_{}", t), ranges, true);
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
  const LexNfaState& st = n.s[static_cast<std::size_t>(x)];
  if (st.guard >= 0) {
    leaves.push_back(static_cast<std::size_t>(st.guard));
    return true;
  }
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
    const LexNfaState& st = n.s[static_cast<std::size_t>(x)];
    if (st.guard >= 0) {
      const auto k = static_cast<std::size_t>(st.guard);
      at(k, head);
      x = n.guards[k].out;
      continue;
    }
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
          if (e.min > 1 || e.a != kNoSet)
            out[k] = static_cast<int>(head.size());
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
