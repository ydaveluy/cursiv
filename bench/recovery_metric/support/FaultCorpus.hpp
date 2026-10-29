// Faulted copies of a valid document, one class at a time, drawn from a seed
// so a run replays byte for byte. Sites are read on the twin's tokens: a
// class the document offers no site for yields nothing, never a stand-in.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/fuzz/SeededSource.hpp"
#include "recovery_metric/support/FaultClass.hpp"
#include "recovery_metric/support/RepairCase.hpp"
#include "recovery_metric/support/RepairFault.hpp"
#include "recovery_metric/support/RepairTwin.hpp"

namespace cursiv::recovery_metric {

struct FaultCorpus {
  // What a one-byte fault writes: blanks, brackets, operators, quotes,
  // digits and letters.
  static constexpr std::string_view kBytes =
      " \t\n{}[]()<>,;:=+-*/\"'@0123abcXY";

  [[nodiscard]] static std::vector<RepairCase> draw(const RepairTwin& twin,
                                                    FaultClass k,
                                                    unsigned count,
                                                    std::uint64_t seed) {
    FaultCorpus g(twin, seed ^ (0x9e3779b97f4a7c15ull *
                                (static_cast<std::uint64_t>(k) + 1)));
    std::vector<RepairCase> out;
    // A draw may land on a site that cannot be faulted as asked; bounded
    // retries keep a poor document from spinning.
    for (unsigned tries = 0; out.size() < count && tries < count * 8; ++tries) {
      std::vector<RepairFault> faults = g.faults(k);
      if (faults.empty()) {
        if (!g.offered) break;
        continue;
      }
      RepairCase c;
      if (RepairCase::make(twin.text, std::move(faults), c) &&
          c.text != twin.text)
        out.push_back(std::move(c));
    }
    return out;
  }

  // The twin with one bracket pair nested `levels` times deeper, `(x)` read
  // as `((x))`: the text a deep truncation needs and few documents have.
  // Null when no pair of the twin nests in its own language.
  [[nodiscard]] static std::unique_ptr<RepairTwin> deepen(
      const Parser& parser, const RepairTwin& twin, unsigned levels,
      std::uint64_t seed) {
    const Cst& cst = twin.result.cst();
    const std::vector<std::uint32_t> at = token_rank(twin);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
    for (NodeId id = 0; id < cst.size(); ++id) {
      if (cst[id].subtreeSize == 1) continue;
      const auto [a, b] = span_tokens(at, cst, id);
      if (a >= b || b - a < 2) continue;
      const std::string_view o = twin.spelling(a), e = twin.spelling(b - 1);
      if (twin.literal[a] && twin.literal[b - 1] && closer_of(o) == e)
        pairs.emplace_back(a, b - 1);
    }
    fuzzer::SeededSource rng(seed | 1u);
    for (std::size_t tries = 0; tries < 12 && !pairs.empty(); ++tries) {
      const auto [a, b] = pairs[rng.next() % pairs.size()];
      const TextOffset ob = twin.tokens[a].begin, ce = twin.tokens[b].end;
      std::string text = twin.text.substr(0, ob);
      for (unsigned i = 0; i < levels; ++i) text += twin.spelling(a);
      text += twin.text.substr(ob, ce - ob);
      for (unsigned i = 0; i < levels; ++i) text += twin.spelling(b);
      text += twin.text.substr(ce);
      if (auto deep = RepairTwin::read(parser, std::move(text))) return deep;
    }
    return nullptr;
  }

  // Per token, how many closers a cut right after it leaves owed: the
  // spelled tokens that end a record some spelled token opened before the
  // cut -- the `}` of a block, the `end` of a state, the `;` of a `return`.
  [[nodiscard]] static std::vector<std::uint32_t> owed(const RepairTwin& twin) {
    const Cst& cst = twin.result.cst();
    const std::size_t n = twin.tokens.size();
    const std::vector<std::uint32_t> at = token_rank(twin);
    const auto rank = [&](NodeId id) -> std::uint32_t {
      return at[id] < n && twin.tokens[at[id]].id == id ? at[id] : kNoRank;
    };
    std::vector<long long> delta(n + 1);
    for (NodeId id = 0; id < cst.size(); ++id) {
      if (cst[id].subtreeSize == 1) continue;
      std::uint32_t open = kNoRank, close = kNoRank;
      const NodeId first = cst.subtree_begin(id);
      for (NodeId c = id; c > first;) {
        const NodeId child = c - 1;
        c = cst.subtree_begin(child);
        const CstNode& k = cst[child];
        if (k.hidden || (k.subtreeSize == 1 && k.begin == k.end)) continue;
        const std::uint32_t r = k.subtreeSize == 1 ? rank(child) : kNoRank;
        if (close == kNoRank) close = r == kNoRank ? kNoTail : r;
        open = r;
      }
      if (open >= n || close >= n || open >= close || !twin.literal[open] ||
          !twin.literal[close])
        continue;
      ++delta[open];
      --delta[close];
    }
    std::vector<std::uint32_t> out(n);
    long long run = 0;
    for (std::size_t i = 0; i < n; ++i) {
      run += delta[i];
      out[i] = static_cast<std::uint32_t>(run);
    }
    return out;
  }

 private:
  static constexpr std::uint32_t kNoRank = ~std::uint32_t{0};
  // A record whose last child is no token owes no closer of its own.
  static constexpr std::uint32_t kNoTail = kNoRank - 1;

  const RepairTwin& twin;
  fuzzer::SeededSource rng;
  // The class has sites in this twin at all.
  bool offered = true;

  FaultCorpus(const RepairTwin& t, std::uint64_t seed) : twin(t), rng(seed) {}

  [[nodiscard]] std::size_t pick(std::size_t n) { return rng.next() % n; }

  // Rank of each record in the token list: the tokens of record `id` are
  // `[at[subtree_begin(id)], at[id + 1])`.
  [[nodiscard]] static std::vector<std::uint32_t> token_rank(
      const RepairTwin& twin) {
    const Cst& cst = twin.result.cst();
    std::vector<std::uint32_t> at(cst.size() + 1);
    std::size_t k = 0;
    for (NodeId id = 0; id < cst.size(); ++id) {
      at[id] = static_cast<std::uint32_t>(k);
      if (k < twin.tokens.size() && twin.tokens[k].id == id) ++k;
    }
    at[cst.size()] = static_cast<std::uint32_t>(k);
    return at;
  }

  [[nodiscard]] static std::pair<std::uint32_t, std::uint32_t> span_tokens(
      const std::vector<std::uint32_t>& at, const Cst& cst, NodeId id) {
    return {at[cst.subtree_begin(id)], at[id + 1]};
  }

  [[nodiscard]] static std::string_view closer_of(std::string_view open) {
    if (open == "(") return ")";
    if (open == "[") return "]";
    if (open == "{") return "}";
    return {};
  }

  [[nodiscard]] bool solid(std::size_t i) const {
    return i < twin.text.size() &&
           static_cast<unsigned char>(twin.text[i]) > ' ';
  }

  // Token `i` gone; a blank stands in when both neighbours touch it, or the
  // lexer would read them as one.
  [[nodiscard]] RepairFault drop(std::size_t i) const {
    const CstLeaf& t = twin.tokens[i];
    const bool glue = t.begin > 0 && solid(t.begin - 1) && solid(t.end);
    return RepairFault{t.begin, t.end - t.begin, glue ? " " : ""};
  }

  [[nodiscard]] std::vector<RepairFault> faults(FaultClass k) {
    const std::size_t n = twin.tokens.size();
    switch (k) {
      case FaultClass::byte: {
        const std::size_t size = twin.text.size();
        const std::size_t pos = pick(size + 1);
        const unsigned op = pos == size ? 0 : static_cast<unsigned>(pick(3));
        const char b = kBytes[pick(kBytes.size())];
        if (op == 2 && twin.text[pos] == b) return {};
        return {RepairFault{static_cast<TextOffset>(pos), op == 0 ? 0u : 1u,
                            op == 1 ? std::string() : std::string(1, b)}};
      }
      case FaultClass::spread1:
      case FaultClass::spread3:
      case FaultClass::spread10: {
        const std::size_t d = k == FaultClass::spread1   ? 1
                              : k == FaultClass::spread3 ? 3
                                                         : 10;
        const std::size_t span = 2 * (d + 1) + 1;
        if (n < span + 1) {
          offered = false;
          return {};
        }
        const std::size_t s = pick(n - span + 1);
        std::vector<RepairFault> out;
        for (std::size_t site = s; site < s + span; site += d + 1) {
          const CstLeaf& t = twin.tokens[site];
          const std::string_view other = twin.spelling(pick(n));
          const unsigned kind = static_cast<unsigned>(pick(3));
          if (kind == 0)
            out.push_back(drop(site));
          else if (kind == 1)
            out.push_back(RepairFault{t.begin, 0,
                                      " " + std::string(other) + " "});
          else if (other == twin.spelling(site))
            return {};
          else
            out.push_back(RepairFault{t.begin, t.end - t.begin,
                                      " " + std::string(other) + " "});
        }
        return out;
      }
      case FaultClass::separators: {
        // Separators of one list: same spelling, same bracket depth, and no
        // bracket closing below that depth between two of them.
        std::vector<int> depth(n + 1);
        for (std::size_t i = 0; i < n; ++i) {
          const std::string_view s = twin.spelling(i);
          const bool lit = twin.literal[i];
          depth[i + 1] = depth[i] + (lit && !closer_of(s).empty()   ? 1
                                     : lit && (s == ")" || s == "]" || s == "}")
                                         ? -1
                                         : 0);
        }
        std::vector<std::vector<std::size_t>> runs;
        std::vector<std::size_t> open;
        for (std::size_t i = 0; i < n; ++i) {
          const std::string_view s = twin.spelling(i);
          if (!twin.literal[i] || (s != "," && s != ";")) continue;
          bool joined = false;
          for (std::size_t r = open.size(); r-- > 0;) {
            std::vector<std::size_t>& run = runs[open[r]];
            const std::size_t last = run.back();
            if (twin.spelling(last) != s || depth[last + 1] != depth[i + 1])
              continue;
            bool closes = false;
            for (std::size_t j = last + 1; j < i && !closes; ++j)
              closes = depth[j + 1] < depth[i + 1];
            if (!closes) {
              run.push_back(i);
              joined = true;
            }
            break;
          }
          if (!joined) {
            open.push_back(runs.size());
            runs.push_back({i});
          }
        }
        std::erase_if(runs, [](const auto& r) { return r.size() < 2; });
        if (runs.empty()) {
          offered = false;
          return {};
        }
        const std::vector<std::size_t>& run = runs[pick(runs.size())];
        const std::size_t len =
            2 + pick(std::min<std::size_t>(run.size(), 8) - 1);
        const std::size_t from = pick(run.size() - len + 1);
        std::vector<RepairFault> out;
        for (std::size_t i = from; i < from + len; ++i)
          out.push_back(drop(run[i]));
        return out;
      }
      case FaultClass::eof1:
      case FaultClass::eof3:
      case FaultClass::eof7:
      case FaultClass::eof20: {
        const unsigned lo = k == FaultClass::eof1   ? 1
                            : k == FaultClass::eof3 ? 2
                            : k == FaultClass::eof7 ? 4
                                                    : 8;
        const unsigned hi = k == FaultClass::eof1   ? 1
                            : k == FaultClass::eof3 ? 3
                            : k == FaultClass::eof7 ? 7
                                                    : 20;
        const std::vector<std::uint32_t> depth = owed(twin);
        std::vector<std::size_t> sites;
        for (std::size_t i = 0; i + 1 < n; ++i)
          if (depth[i] >= lo && depth[i] <= hi) sites.push_back(i);
        if (sites.empty()) {
          offered = false;
          return {};
        }
        const TextOffset cut = twin.tokens[sites[pick(sites.size())]].end;
        return {RepairFault{
            cut, static_cast<TextOffset>(twin.text.size()) - cut, ""}};
      }
      case FaultClass::quote:
      case FaultClass::escape: {
        std::vector<std::size_t> strings;
        // Escaping the closing quote is a fault only where a backslash
        // escapes it: some string of the twin says so.
        bool escapes = false;
        for (std::size_t i = 0; i < n; ++i) {
          const std::string_view s = twin.spelling(i);
          if (twin.literal[i] || s.size() < 2 ||
              (s.front() != '"' && s.front() != '\'') || s.back() != s.front())
            continue;
          strings.push_back(i);
          const char quote[] = {'\\', s.front(), '\0'};
          if (s.substr(1, s.size() - 2).find(quote) != std::string_view::npos)
            escapes = true;
        }
        if (strings.empty() || (k == FaultClass::escape && !escapes)) {
          offered = false;
          return {};
        }
        const CstLeaf& t = twin.tokens[strings[pick(strings.size())]];
        if (k == FaultClass::quote) return {RepairFault{t.end - 1, 1, ""}};
        return {RepairFault{t.end - 1, 0, "\\"}};
      }
      case FaultClass::comment: {
        std::vector<std::size_t> open;
        for (std::size_t i = 0; i < twin.hidden.size(); ++i) {
          const CstLeaf& h = twin.hidden[i];
          const std::string_view s =
              std::string_view(twin.text).substr(h.begin, h.end - h.begin);
          if (s.size() >= 4 && s.starts_with("/*") && s.ends_with("*/") &&
              twin.text.find("*/", h.end) == std::string::npos)
            open.push_back(i);
        }
        if (open.empty()) {
          offered = false;
          return {};
        }
        const CstLeaf& h = twin.hidden[open[pick(open.size())]];
        return {RepairFault{h.end - 2, 2, ""}};
      }
      case FaultClass::hole: {
        std::vector<std::size_t> punct, closers;
        for (std::size_t i = 0; i < n; ++i) {
          const std::string_view s = twin.spelling(i);
          if (!twin.literal[i]) continue;
          if (std::none_of(s.begin(), s.end(), [](char ch) {
                return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9') || ch == '_';
              }))
            punct.push_back(i);
          if (s == ")" || s == "]" || s == "}") closers.push_back(i);
        }
        if (n < 2 || punct.empty()) {
          offered = false;
          return {};
        }
        const std::size_t at = pick(n - 1);
        const std::size_t len = 2 + pick(5);
        std::string junk;
        for (std::size_t i = 0; i < len; ++i) {
          const std::size_t t = i == 0 && !closers.empty()
                                    ? closers[pick(closers.size())]
                                    : punct[pick(punct.size())];
          junk += ' ';
          junk += twin.spelling(t);
        }
        junk += ' ';
        return {RepairFault{twin.tokens[at].end, 0, junk}};
      }
    }
    return {};
  }
};

}  // namespace cursiv::recovery_metric
