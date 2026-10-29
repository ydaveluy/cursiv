// A relay as a generator builds it, the lexical predicate it asserts by node,
// the pool both engines intern relay chains into, and the name pool a table
// keeps of what terminals and relays name.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/engines/shared/gen/EmitOutput.hpp"
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

  // Walks the literal `node` spells, one byte per single-byte class, handing
  // each byte to `emit`. Answers its length, or nothing where `node` is no
  // literal.
  template <class Cfg, class Emit>
  static std::optional<std::size_t> walk_literal(const Cfg& g,
                                                 std::uint16_t node,
                                                 Emit&& emit) {
    const auto one = [&](std::uint16_t kid) {
      const auto& c = g.lex[kid];
      if (c.kind != LexKind::chars || g.sets[c.a].count() != 1) return false;
      each_bit(g.sets[c.a].w, 4,
               [&](std::size_t b) { emit(static_cast<char>(b)); });
      return true;
    };
    const LexNode& e = g.lex[node];
    if (e.kind == LexKind::chars)
      return one(node) ? std::optional<std::size_t>{1} : std::nullopt;
    if (e.kind != LexKind::seq || e.b == 0) return std::nullopt;
    for (unsigned i = 0; i < e.b; ++i)
      if (!one(g.lexKid[e.a + i])) return std::nullopt;
    return std::size_t{e.b};
  }

  template <class Cfg>
  static bool literal(const Cfg& g, std::uint16_t node, std::string& out) {
    return walk_literal(g, node, [&](char c) { out += c; }).has_value();
  }

  // The length of the literal `node` spells, without its bytes.
  template <class Cfg>
  static std::optional<std::size_t> literal_size(const Cfg& g,
                                                 std::uint16_t node) {
    return walk_literal(g, node, [](char) {});
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
void lex_relay_seed(Dfa& dfa, const Relays& relays, const Push& push) {
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
                       const Push& push) {
    std::ranges::sort(group);
    group.erase(std::ranges::unique(group).begin(), group.end());
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

  // The chain a state launches: the relays of its reading `r`, and a gate row
  // per trivia the keywords standing beside it hold back, each asserting
  // `cut.gate` of what they wait on.
  template <class Cfg, class Cut, class Reading, class Rows, class Push>
  std::uint16_t launched(Cfg& g, Cut& cut, const Reading& r, const Rows& rows,
                         const Push& push) {
    std::vector<std::uint16_t> group = r.relays;
    const std::uint16_t predicate =
        r.gates.empty() ? kNoLexNode : cut.gate(g, r.gated);
    for (const auto& gate : r.gates) {
      LexRelayGen row;
      row.term = gate.term;
      row.predicate = predicate;
      row.resume = gate.resume;
      row.gate = true;
      group.push_back(lex_relay_push(push, row));
    }
    return intern(group, rows, push);
  }
};

// The name pool as a reading takes it: only the bytes a terminal or a relay
// names, each offset moved down past the bytes nothing reads. Lengths stay
// explicit, since a relay's range may hold bytes that are no text.
struct LexNamePool {
  std::vector<char> bytes;
  // The new offset of every old one, the end of the pool included.
  std::vector<std::uint16_t> at;

  template <class Cfg>
  LexNamePool(const Cfg& g, std::span<const LexRelayGen> relays) {
    std::vector<bool> kept(g.nameLen, false);
    const auto keep = [&](std::uint16_t off, std::uint16_t len) {
      std::fill_n(kept.begin() + off, len, true);
    };
    for (unsigned t = 0; t < g.nterms; ++t) {
      keep(g.terms[t].nameOff, g.terms[t].nameLen);
      keep(g.terms[t].textOff, g.terms[t].textLen);
    }
    for (const LexRelayGen& r : relays) {
      keep(r.beforeOff, r.beforeLen);
      keep(r.afterOff, r.afterLen);
    }
    at.resize(std::size_t{g.nameLen} + 1u);
    for (std::size_t i = 0; i <= g.nameLen; ++i) {
      at[i] = static_cast<std::uint16_t>(bytes.size());
      if (i < g.nameLen && kept[i]) bytes.push_back(g.names[i]);
    }
  }

  std::uint16_t operator()(std::uint16_t off) const { return at[off]; }
};

// The row as a designated initializer, defaults left out; `name` maps an
// offset into the generator's name pool to the table's.
template <class Name>
std::string lex_relay_source(const LexRelayGen& r, Name&& name) {
  constexpr LexRelay d{};
  EmitFields field;
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
  return std::format("{{{}}}", field.list());
}

}  // namespace cursiv
