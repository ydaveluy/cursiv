// A set of words read at the head of a token, compiled to one matcher: a
// trie over the bytes from the token's first, which stops at the first byte
// no word takes.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/Concat.hpp"
#include "cursiv/shared/gen/LexNfaPool.hpp"
#include "cursiv/shared/gen/lex_intern_set.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/DynSet.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/text/TextBuilder.hpp"

namespace cursiv::engine {

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
    TextBuilder out;
    out.line(linkage, " bool ", name,
             "(const char* p, ::cursiv::TextOffset n,\n"
             "    ::cursiv::TextOffset rest",
             reach ? ", ::cursiv::TextOffset& seen" : "",
             ") {\n"
             "  (void)p;\n  (void)n;\n  (void)rest;");
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
    std::unordered_map<SetKey, std::uint32_t, SetKeyHash> setIndex;
    const auto set_of = [&](const ByteSet& bytes) {
      const auto [at, fresh] = setIndex.try_emplace(
          SetKey{bytes}, static_cast<std::uint32_t>(sets.size()));
      if (fresh) sets.push_back(bytes);
      return at->second;
    };
    std::vector<Row> rows;
    std::vector<Edge> edges;
    std::vector<Work> work;
    const auto add = [&](std::uint32_t at, Items items) {
      const auto id = static_cast<std::uint32_t>(work.size());
      work.emplace_back(at, std::move(items));
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
    out.line(
        "  [[maybe_unused]] static constexpr ::cursiv::engine::ByteSet sets[] "
        "= {");
    for (const ByteSet& s : sets)
      out.line("    {{", s.w[0], "ull, ", s.w[1], "ull, ", s.w[2], "ull, ",
               s.w[3], "ull}},");
    if (sets.empty()) out.line("    {},");
    out.line("  };");
    // Small tries stay direct, with flat labels instead of nesting one
    // switch per byte. The bound also bounds the emitted function's graph.
    if (rows.size() <= 16) {
      for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        out.write(i == 0 ? "  {\n" : concat("  s", i, ": {\n"));
        if (r.kind == accept) {
          if (reach && r.yes)
            out.line("    if (seen <= ", r.at,
                     "u)\n"
                     "      seen = rest > ",
                     r.at, "u ? ", r.at, "u + 1u : rest;");
          out.line("    return true;");
        } else if (r.kind == miss) {
          out.line("    return false;");
        } else if (r.kind == refuse) {
          const std::string c =
              concat("static_cast<unsigned char>(p[", r.at, "u])");
          const auto bytes = concat("sets[", r.first, "u].has(", c, ")");
          const auto lead = concat("sets[", r.count, "u].has(", c, ")");
          out.line("    if (rest > ", r.at, "u) {");
          if (reach) {
            out.write("      const ::cursiv::TextOffset width = ");
            out.write(sets[r.count].any()
                          ? concat(lead,
                                   " ? "
                                   "::std::min<::cursiv::TextOffset>("
                                   "::cursiv::utf8_width(p[",
                                   r.at, "u]), rest - ", r.at, "u) : 1u;\n")
                          : "1u;\n");
            out.line("      if (seen < ", r.at, "u + width) seen = ", r.at,
                     "u + width;");
          }
          std::string test = bytes;
          if (sets[r.count].any())
            test += concat(
                " || (", lead, " && ::cursiv::engine::lex_scalar_in(",
                r.scalars == 0 ? std::string(ranges.word)
                               : concat(ranges.pool, " + ", r.scalars >> 16,
                                        "u, ", r.scalars & 0xFFFFu, "u"),
                ", p + ", r.at, "u, rest - ", r.at, "u))");
          out.write("      if (", test, ") goto s", r.yes, ";\n    }");
          if (reach)
            out.write(" else if (seen < rest) {\n      seen = rest;\n    }");
          out.line("\n    goto s", r.no, ";");
        } else {
          out.line("    if (n <= ", r.at, "u) {");
          if (reach && r.at != 0)
            out.line("      if (seen < ", r.at, "u) seen = ", r.at, "u;");
          out.line("      return false;\n    }");
          out.line("    switch (static_cast<unsigned char>(p[", r.at, "u])) {");
          for (auto e = r.first; e < r.first + r.count; ++e) {
            for (unsigned b = 0; b < 256; ++b)
              if (sets[edges[e].set].has(static_cast<unsigned char>(b)))
                out.line("      case ", b, "u:");
            out.line("        goto s", edges[e].next, ";");
          }
          out.line("      default: break;\n    }");
          if (reach && r.at != 0)
            out.line("    if (seen < ", r.at, "u) seen = ", r.at, "u;");
          out.line("    return false;");
        }
        out.line("  }");
      }
      return out.line("}").take();
    }
    // A table keeps long spellings out of the C++ and Wasm control-flow
    // graphs. Both generation and matching use constant call-stack space.
    out.line(
        "  struct Row { ::std::uint32_t at, first, count, yes, no, scalars, "
        "kind; };\n"
        "  struct Edge { ::std::uint32_t set, next; };\n"
        "  static constexpr Edge edges[] = {");
    for (const Edge& e : edges) out.line("    {", e.set, "u, ", e.next, "u},");
    if (edges.empty()) out.line("    {},");
    out.line("  };\n  static constexpr Row rows[] = {");
    for (const Row& r : rows)
      out.line("    {", r.at, "u, ", r.first, "u, ", r.count, "u, ", r.yes,
               "u, ", r.no, "u, ", r.scalars, "u, ",
               static_cast<unsigned>(r.kind), "u},");
    out.line(
        "  };\n  ::std::uint32_t state = 0;\n  for (;;) {\n"
        "    const auto& r = rows[state];\n"
        "    if (r.kind == 0u) return false;\n"
        "    if (r.kind == 1u) {");
    if (reach)
      out.line(
          "      if (r.yes && seen <= r.at)\n"
          "        seen = rest > r.at ? r.at + 1u : rest;");
    out.line("      return true;\n    }");
    if (refused) {
      out.line(
          "    if (r.kind == 3u) {\n"
          "      bool hit = false;\n      if (rest > r.at) {\n"
          "        const auto c = static_cast<unsigned char>(p[r.at]);");
      if (reach) {
        out.write("        const ::cursiv::TextOffset width = ");
        out.write(decoded ? "sets[r.count].has(c) ? "
                            "::std::min<::cursiv::TextOffset>("
                            "::cursiv::utf8_width(p[r.at]), rest - r.at) : "
                            "1u;\n"
                          : "1u;\n");
        out.line("        if (seen < r.at + width) seen = r.at + width;");
      }
      out.write("        hit = sets[r.first].has(c)");
      if (decoded) {
        out.write(" || (sets[r.count].has(c) &&\n            ");
        if (ownScalars)
          out.write(
              "(r.scalars != 0u\n"
              "             ? ::cursiv::engine::lex_scalar_in(",
              ranges.pool,
              " + (r.scalars >> 16), "
              "r.scalars & 0xFFFFu, p + r.at, rest - r.at)\n"
              "             : ::cursiv::engine::lex_scalar_in(",
              ranges.word,
              ", p + r.at, rest - "
              "r.at))");
        else
          out.write("::cursiv::engine::lex_scalar_in(", ranges.word,
                    ", p + r.at, rest - r.at)");
        out.write(")");
      }
      out.write(";\n      }");
      if (reach)
        out.write(" else if (seen < rest) {\n        seen = rest;\n      }");
      out.line("\n      state = hit ? r.yes : r.no;\n      continue;\n    }");
    }
    out.line("    if (n <= r.at) {");
    if (reach) out.line("      if (r.at != 0u && seen < r.at) seen = r.at;");
    out.line(
        "      return false;\n    }\n"
        "    const auto c = static_cast<unsigned char>(p[r.at]);\n"
        "    bool found = false;\n"
        "    for (auto e = r.first; e < r.first + r.count; ++e) {\n"
        "      if (!sets[edges[e].set].has(c)) continue;\n"
        "      state = edges[e].next;\n      found = true;\n      break;\n    "
        "}\n"
        "    if (found) continue;");
    if (reach) out.line("    if (r.at != 0u && seen < r.at) seen = r.at;");
    return out.line("    return false;\n  }\n}").take();
  }
};

// The function `sig` answering for terminal `t` through its matcher `fn_<t>`.
inline std::string lex_head_switch(const std::vector<HeadMatcher>& heads,
                                   std::string_view sig, std::string_view fn) {
  TextBuilder out;
  out.line(sig, " {\n  switch (t) {");
  for (std::size_t t = 0; t < heads.size(); ++t)
    if (!heads[t].words.empty())
      out.line("    case ", t, "u:\n      return ", fn, "_", t,
               "(p, rest, rest);");
  return out.line("    default:\n      return false;\n  }\n}").take();
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
  TextBuilder out;
  for (std::size_t t = 0; t < heads.size(); ++t)
    if (!heads[t].words.empty())
      out.write(heads[t].source("CURSIV_NOINLINE inline", concat("refuses_", t),
                                ranges) +
                heads[t].source("inline", concat("reaches_", t), ranges, true));
  out.write(lex_head_switch(heads, refuses, "refuses"));
  out.line(reach, " {\n  ::cursiv::TextOffset seen = 0;\n  switch (m) {");
  std::vector<bool> done(modeHeads.size(), false);
  for (std::size_t m = 0; m < modeHeads.size(); ++m) {
    if (done[m] || modeHeads[m].none()) continue;
    for (std::size_t k = m; k < modeHeads.size(); ++k)
      if (!done[k] && modeHeads[k] == modeHeads[m]) {
        out.line("    case ", k, "u:");
        done[k] = true;
      }
    for (std::size_t t = 0; t < heads.size(); ++t)
      if (modeHeads[m].test(static_cast<unsigned>(t)))
        out.line("      reaches_", t, "(p, rest, rest, seen);");
    out.line("      break;");
  }
  out.line("    default:\n      break;\n  }\n  return seen;\n}");
  return out.take();
}

// The source of `tail`, answering for terminal `t` whether one of the words
// it refuses where its token ends is spelled at `p`, `rest` bytes left.
inline std::string lex_tail_source(const std::vector<HeadMatcher>& tails,
                                   std::string_view tail,
                                   const HeadMatcher::Ranges& ranges) {
  TextBuilder out;
  for (std::size_t t = 0; t < tails.size(); ++t)
    if (!tails[t].words.empty())
      out.write(tails[t].source("CURSIV_NOINLINE inline", concat("trails_", t),
                                ranges));
  return out.write(lex_head_switch(tails, tail, "trails")).take();
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
      if (std::vector<std::size_t> leaves;
          lex_split_refusals(n, x, leaves) &&
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
  std::vector out(n.guards.size(), -1);
  for (const int start : startOf)
    lex_spine(
        n, g, start,
        [&](std::size_t k, const std::vector<HeadMatcher::Step>& head) {
          const LexNode e = g.lex[n.guards[k].node];
          if (e.min > 1 || e.a != kNoSet)
            out[k] = static_cast<int>(head.size());
        },
        [](const std::vector<HeadMatcher::Step>&) {
          /* the spine's end adds no offset */
        });
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

}  // namespace cursiv::engine
