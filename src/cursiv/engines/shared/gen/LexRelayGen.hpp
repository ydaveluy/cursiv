// A relay as a generator builds it, the lexical predicate it asserts by node,
// and the pool both engines intern relay chains into.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/bit_words.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"

namespace cursiv {

struct LexRelayGen : LexRelay {
  std::uint16_t predicate = kNoLexNode;
  std::uint16_t recognition = kNoLexNode;
  // Launches a trivia a keyword's spelling stands beside: its terminal is read
  // behind the relay, never at the state that launches it.
  bool gate = false;

  constexpr bool operator==(const LexRelayGen&) const = default;

  static bool pad_final(const ByteSet& pad, const ByteSet& mark, bool markEmpty,
                        std::string_view after) {
    return !pad.intersects(mark) &&
           (!markEmpty ||
            (!after.empty() &&
             !pad.has(static_cast<unsigned char>(after.front()))));
  }

  template <class Cfg>
  static bool literal(const Cfg& g, std::uint16_t node, std::string& out) {
    const auto one = [&](std::uint16_t kid) {
      const auto& c = g.lex[kid];
      if (c.kind != LexKind::chars || g.sets[c.a].count() != 1) return false;
      each_bit(g.sets[c.a].w, 4,
               [&](std::size_t b) { out += static_cast<char>(b); });
      return true;
    };
    const LexNode& e = g.lex[node];
    if (e.kind == LexKind::chars) return one(node);
    if (e.kind != LexKind::seq) return false;
    for (unsigned i = 0; i < e.b; ++i)
      if (!one(g.lexKid[e.a + i])) return false;
    return !out.empty();
  }
};

// A row appended by `push`, which answers its index: an index is sixteen
// bits, `kNoRelay` aside.
template <class Push>
std::uint16_t lex_relay_push(Push&& push, const LexRelayGen& row) {
  const std::size_t at = push(row);
  if (at >= kNoRelay) throw LowerRefused(LowerFail::tokenGraphTooLarge);
  return static_cast<std::uint16_t>(at);
}

// The pool's first rows, one per relay of the token graph, each resuming in
// the DFA state its resume subset interns as.
template <class Dfa, class Relays, class Push>
void lex_relay_seed(Dfa& dfa, const Relays& relays, Push&& push) {
  for (const auto& relay : relays) {
    LexRelayGen row = relay.row;
    dfa.close(relay.resumeOut);
    row.resume = dfa.intern();
    lex_relay_push(push, row);
  }
}

struct LexRelayChains {
  std::unordered_map<std::uint32_t, std::uint16_t> links;

  template <class Rows, class Push>
  std::uint16_t intern(std::vector<std::uint16_t>& group, const Rows& rows,
                       Push&& push) {
    std::sort(group.begin(), group.end());
    group.erase(std::unique(group.begin(), group.end()), group.end());
    std::uint16_t head = kNoRelay;
    for (auto it = group.rbegin(); it != group.rend(); ++it) {
      if (head == kNoRelay) {
        head = *it;
        continue;
      }
      const auto key = (std::uint32_t{*it} << 16) | head;
      if (const auto found = links.find(key); found != links.end()) {
        head = found->second;
        continue;
      }
      LexRelayGen row = rows[*it];
      row.next = head;
      head = lex_relay_push(push, row);
      links.emplace(key, head);
    }
    return head;
  }
};

// The row as a designated initializer, defaults left out; `name` maps an
// offset into the generator's name pool to the table's.
template <class Name>
std::string lex_relay_source(const LexRelayGen& r, Name&& name) {
  constexpr LexRelay d{};
  std::string out;
  const auto field = [&]<class V>(std::string_view key, V v, V def,
                                  std::string_view pre = "") {
    if (v != def)
      std::format_to(std::back_inserter(out), ",.{}={}{}", key, pre, v);
  };
  field("term", r.term, d.term);
  field("markSet", r.markSet, d.markSet);
  field("fence", r.fence, d.fence);
  field("closes", r.closes, d.closes);
  field("beforeOff", name(r.beforeOff), d.beforeOff);
  field("beforeLen", r.beforeLen, d.beforeLen);
  field("afterOff", name(r.afterOff), d.afterOff);
  field("afterLen", r.afterLen, d.afterLen);
  field("resume", r.resume, d.resume);
  field("nested", r.nested, d.nested);
  field("back", r.back, d.back);
  field("next", r.next, d.next);
  field("padSet", r.padSet, d.padSet);
  field("several", r.several, d.several);
  field("eof", r.eof, d.eof);
  field("tier", r.tier, d.tier);
  field("asserts", r.predicate, kNoLexNode, "&lexical_predicate_");
  field("recognizes", r.recognition, kNoLexNode, "&lexical_recognizer_");
  return "{" + out.substr(out.empty() ? 0 : 1) + "}";
}

}  // namespace cursiv
