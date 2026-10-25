// An LR lexer's DFA states as ladder rungs of the generated walk; each reader
// supplies its match and run policy, so the normal walk keeps its shortcuts.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

class LrDfaEmitter {
 public:
  // Past this a mode's ladder costs more instruction cache than the table
  // walk costs work.
  static constexpr unsigned kLadder = 320;

  LrDfaEmitter(std::span<const std::uint8_t, 256> cls,
               std::span<const std::uint16_t> next, unsigned ncls,
               unsigned ndfa)
      : _cls(cls), _next(next), _ncls(ncls), _ndfa(ndfa) {}

  // The bytes that keep a walk in this state, in byte order.
  ByteSet stay(unsigned st) const {
    ByteSet out{};
    for (unsigned b = 0; b < 256; ++b)
      if (next(st, _cls[b]) == st) out.add(static_cast<unsigned char>(b));
    return out;
  }

  // The states a start reaches: the entry first -- execution falls into it --
  // and the rest in id order.
  const std::vector<unsigned>& states_from(unsigned lo) const {
    const auto [at, fresh] = _reachable.try_emplace(lo);
    if (!fresh) return at->second;
    const unsigned n = _ndfa;
    std::vector<char> seen(n, 0);
    auto& out = at->second;
    out.push_back(lo);
    seen[lo] = 1;
    for (std::size_t i = 0; i < out.size(); ++i) {
      const unsigned s = out[i];
      for (unsigned c = 0; c < _ncls; ++c) {
        const unsigned t = next(s, c);
        if (t == cursiv::kDeadState || t >= n || seen[t]) continue;
        seen[t] = 1;
        out.push_back(t);
      }
    }
    std::sort(out.begin() + 1, out.end());
    return out;
  }

  // The states a mode spells in source: the whole of it under the rung, past
  // it the start, its successors and the runs they reach. Every state kept is
  // entered by an edge that is itself spelled, or its label would stand on
  // text nothing can reach.
  const std::vector<unsigned>& ladder_from(unsigned lo) const {
    const auto& mine = states_from(lo);
    if (mine.size() <= kLadder) return mine;
    const auto [at, fresh] = _ladders.try_emplace(lo);
    if (!fresh) return at->second;
    std::vector<std::uint8_t> keep(_ndfa, 0);
    std::vector<unsigned> front{lo};
    keep[lo] = 1;
    for (unsigned k = 0; k < _ncls; ++k) {
      const std::uint16_t to = next(lo, k);
      if (to == kDeadState || keep[to] != 0) continue;
      keep[to] = 1;
      front.push_back(to);
    }
    for (const unsigned st : front)
      for (unsigned k = 0; k < _ncls; ++k) {
        const std::uint16_t to = next(st, k);
        if (to == kDeadState || keep[to] != 0) continue;
        if (next(to, k) == to) keep[to] = 1;
      }
    auto& out = at->second;
    for (const unsigned st : mine)
      if (keep[st] != 0) out.push_back(st);
    return out;
  }

  // One state as a rung; an edge outside `spelled` continues in the rows.
  template <class Output, class Policy>
  void state(Output& out, Policy& policy, unsigned st,
             const std::uint8_t* spelled = nullptr) const {
    out.fmt("W{}:\n", st);
    out.line("  ;");
    policy.emit_bound_exit(st, spelled);
    policy.emit_match(st, "  ");
    policy.emit_relay_call(st, "  ");
    const bool blind = policy.blind(st);
    const ByteSet kept = blind ? stay(st) : ByteSet{};
    const bool self = kept.any();
    enum class EdgeKind : std::uint8_t { done, direct, table };
    struct EdgeGroup {
      EdgeKind kind;
      std::uint16_t to;
      std::vector<unsigned> cls;
    };
    std::vector<EdgeGroup> groups;
    const auto add_group = [&](EdgeKind kind, std::uint16_t to, unsigned cls) {
      const auto it = std::ranges::find_if(groups, [&](const EdgeGroup& g) {
        return g.kind == kind && g.to == to;
      });
      if (it == groups.end())
        groups.push_back(EdgeGroup{kind, to, {cls}});
      else
        it->cls.push_back(cls);
    };
    for (unsigned k = 0; k < _ncls; ++k) {
      const std::uint16_t to = next(st, k);
      if (self && to == st) continue;
      if (to == kDeadState)
        add_group(EdgeKind::done, kDeadState, k);
      else if (spelled == nullptr || spelled[to] != 0)
        add_group(EdgeKind::direct, to, k);
      else
        add_group(EdgeKind::table, to, k);
    }

    const auto byte_test = [&](const EdgeGroup& g) {
      std::string cond;
      for (unsigned b = 0; b < 256;) {
        const auto in = [&](unsigned x) {
          return std::find(g.cls.begin(), g.cls.end(), _cls[x]) != g.cls.end();
        };
        if (!in(b)) {
          ++b;
          continue;
        }
        unsigned e = b;
        while (e + 1 < 256 && in(e + 1)) ++e;
        if (!cond.empty()) cond += " || ";
        cond += b == e ? std::format("b == {}", b)
                       : std::format("(b >= {} && b <= {})", b, e);
        b = e + 1;
      }
      return cond;
    };

    const auto emit_edge = [&](const EdgeGroup& g, std::string_view ind) {
      switch (g.kind) {
        case EdgeKind::done:
          return out.fmt("{}goto Wdone;\n", ind);
        case EdgeKind::direct:
          return out.fmt("{}++i; goto W{};\n", ind, g.to);
        case EdgeKind::table:
          return out.fmt("{}++i; st = {}; goto Wtab;\n", ind, g.to);
      }
    };
    const auto only_done = [&] {
      return groups.empty() ||
             (groups.size() == 1 && groups.front().kind == EdgeKind::done);
    };
    if (const int to = self ? -1 : policy.continuation_of(st, spelled);
        to >= 0) {
      out.line("  if (i >= len) [[unlikely]] goto Wdone;");
      out.line("  b = static_cast<unsigned char>(src[i]);");
      for (const auto& g : groups) {
        if (g.kind == EdgeKind::done ||
            (g.kind == EdgeKind::direct && g.to == to))
          continue;
        out.fmt("  if ({}) {{\n", byte_test(g));
        emit_edge(g, "    ");
        out.line("  }");
      }
      out.fmt("  goto WC{};\n", to);
      return;
    }
    if (!self && only_done()) {
      out.line("  goto Wdone;");
      return;
    }
    if (self) {
      out.line("  if (i >= len) [[unlikely]] goto Wdone;");
      out.line("  b = static_cast<unsigned char>(src[i]);");
      if (policy.plain_run(st)) out.fmt("WC{}:\n", st);
      out.raw("  { static constexpr ::std::uint64_t kStay[4] = ");
      out.fmt("{{{}ull,{}ull,{}ull,{}ull}}", kept.w[0], kept.w[1], kept.w[2],
              kept.w[3]);
      out.line(";");
      out.line("    if (in_set(kStay, b)) [[likely]] {");
      out.line("      ++i;");
      policy.emit_run_adv(kept, "i", "kStay");
      policy.emit_match(st, "      ");
      policy.emit_relay_call(st, "      ");
      if (!only_done()) {
        out.line("      if (i >= len) [[unlikely]] goto Wdone;");
        if (groups.size() > 1)
          out.line("      b = static_cast<unsigned char>(src[i]);");
      }
      out.line("    }");
      out.line("  }");
      if (only_done()) {
        out.line("  goto Wdone;");
        return;
      }
    } else {
      out.line("  if (i >= len) [[unlikely]] goto Wdone;");
      if (groups.size() > 1)
        out.line("  b = static_cast<unsigned char>(src[i]);");
    }
    if (groups.size() == 1) {
      emit_edge(groups.front(), "  ");
      return;
    }
    // `default` is the arm gcc reaches last: the token end, else the widest.
    auto def = std::find_if(
        groups.begin(), groups.end(),
        [](const EdgeGroup& g) { return g.kind == EdgeKind::done; });
    if (def == groups.end())
      def = std::max_element(groups.begin(), groups.end(),
                             [](const EdgeGroup& x, const EdgeGroup& y) {
                               return x.cls.size() < y.cls.size();
                             });

    // Keep the class lookup where it combines emitted byte cases.
    bool byteCases = true;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
      if (it == def) continue;
      for (const unsigned c : it->cls)
        byteCases = byteCases && std::ranges::count(_cls, c) == 1;
    }
    out.line(byteCases ? "  switch (b) {" : "  switch (kTable.cls[b]) {");
    for (auto it = groups.begin(); it != groups.end(); ++it) {
      if (it == def) continue;
      for (const unsigned c : it->cls)
        out.fmt("    case {}:\n",
                byteCases ? std::ranges::find(_cls, c) - _cls.begin() : c);
      emit_edge(*it, "      ");
    }
    out.line("    default:");
    emit_edge(*def, "      ");
    out.line("  }");
  }

 private:
  std::uint16_t next(unsigned state, unsigned cls) const {
    return _next[std::size_t{state} * _ncls + cls];
  }

  std::span<const std::uint8_t, 256> _cls;
  std::span<const std::uint16_t> _next;
  unsigned _ncls;
  unsigned _ndfa;
  mutable std::unordered_map<unsigned, std::vector<unsigned>> _reachable{};
  mutable std::unordered_map<unsigned, std::vector<unsigned>> _ladders{};
};

}  // namespace cursiv
