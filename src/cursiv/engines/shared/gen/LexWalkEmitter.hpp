// The walk of a lexer's automaton as generated source, for every engine: each
// state a rung of a ladder, a run of bytes crossed eight a step where its set
// has the shape for it. An engine's emitter is the policy the rungs ask for
// the match, the relays and the end of the walk.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/engines/shared/gen/EmitOutput.hpp"
#include "cursiv/engines/shared/runtime/LexMatch.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// A lexer's DFA states as ladder rungs of the generated walk; each reader
// supplies its match and run policy, so the normal walk keeps its shortcuts.
class LexDfaEmitter {
 public:
  // Past this a mode's ladder costs more instruction cache than the table
  // walk costs work.
  static constexpr unsigned kLadder = 320;

  LexDfaEmitter(std::span<const std::uint8_t, 256> cls,
                std::span<const std::uint16_t> next, unsigned ncls,
                unsigned ndfa)
      : _cls(cls), _next(next), _ncls(ncls), _ndfa(ndfa) {}

  unsigned states() const { return _ndfa; }

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
    std::vector front{lo};
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
    const auto done = [&]() -> std::string_view {
      if constexpr (requires { policy.done(st); })
        return policy.done(st);
      else
        return "Wdone";
    };
    const bool blind = policy.blind(st);
    const ByteSet kept = blind ? stay(st) : ByteSet{};
    const bool self = kept.any();
    if constexpr (requires { policy.emit_suffix(st, self); })
      policy.emit_suffix(st, self);
    policy.emit_bound_exit(st, spelled);
    policy.emit_match(st, "  ");
    policy.emit_relay_call(st, "  ");
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
          return std::ranges::find(g.cls, _cls[x]) != g.cls.end();
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
          return out.fmt("{}goto {};\n", ind, done());
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
      out.fmt("  if (i >= len) [[unlikely]] goto {};\n", done());
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
      out.fmt("  goto {};\n", done());
      return;
    }
    if (self) {
      out.fmt("  if (i >= len) [[unlikely]] goto {};\n", done());
      out.line("  b = static_cast<unsigned char>(src[i]);");
      if (policy.plain_run(st)) out.fmt("WC{}:\n", st);
      out.raw("  { static constexpr ::std::array<::std::uint64_t, 4> kStay");
      out.fmt("{{{{{}ull,{}ull,{}ull,{}ull}}}}", kept.w[0], kept.w[1],
              kept.w[2], kept.w[3]);
      out.line(";");
      out.line("    if (in_set<kStay>(b)) [[likely]] {");
      out.line("      ++i;");
      policy.emit_run_adv(kept, "i", "kStay");
      policy.emit_match(st, "      ");
      policy.emit_relay_call(st, "      ");
      if (!only_done()) {
        out.fmt("      if (i >= len) [[unlikely]] goto {};\n", done());
        if (groups.size() > 1)
          out.line("      b = static_cast<unsigned char>(src[i]);");
      }
      out.line("    }");
      out.line("  }");
      if (only_done()) {
        out.fmt("  goto {};\n", done());
        return;
      }
    } else {
      out.fmt("  if (i >= len) [[unlikely]] goto {};\n", done());
      if (groups.size() > 1)
        out.line("  b = static_cast<unsigned char>(src[i]);");
    }
    if (groups.size() == 1) {
      emit_edge(groups.front(), "  ");
      return;
    }
    // `default` is the arm gcc reaches last: the token end, else the widest.
    auto def = std::ranges::find(groups, EdgeKind::done, &EdgeGroup::kind);
    if (def == groups.end())
      def = std::ranges::max_element(
          groups, {}, [](const EdgeGroup& g) { return g.cls.size(); });

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

// The two eight-byte shapes of a run: everything from `lo` up but at most
// four bytes, or a union of a few ASCII ranges. The lowest lane a step flags
// is the run's end exactly: the emitted code jumps past the byte loop there,
// which reads only the last bytes, fewer than eight.
struct LexRunShape {
  // The widest byte `lt8` answers for: past it the subtraction borrows out
  // of the lowest lane, the one `ctzll` reads.
  static constexpr unsigned kSwarBelowMax = 0x80;

  bool swar = false;
  unsigned char lo = 0;
  unsigned char b[4]{};
  unsigned n = 0;
  bool ranges = false;
  unsigned char from[4]{};
  unsigned char to[4]{};
  unsigned nr = 0;
  bool high = false;  // every byte past 0x7F is a member

  bool paired(unsigned k) const {
    if ((from[k] >> 5) != (to[k] >> 5)) return false;
    for (unsigned j = 0; j < nr; ++j)
      if (from[j] == (from[k] ^ 0x20u) && to[j] == (to[k] ^ 0x20u)) return true;
    return false;
  }

  static LexRunShape of(const ByteSet& stay) {
    LexRunShape r;
    // Only a content-shaped run pays for the eight-byte step.
    const unsigned count = stay.count();
    if (count <= 4) return r;
    // Below lo every byte must be absent; above it at most four are absent.
    // Removing lo bytes from the complement gives its first possible bound.
    if (const unsigned lo = count < 252 ? 252 - count : 0;
        lo <= kSwarBelowMax) {
      bool below = false;
      for (unsigned x = 0; x < lo; ++x)
        below = below || stay.has(static_cast<unsigned char>(x));
      if (!below) {
        r.swar = true;
        r.lo = static_cast<unsigned char>(lo);
        for (unsigned x = lo; x < 256; ++x)
          if (!stay.has(static_cast<unsigned char>(x)))
            r.b[r.n++] = static_cast<unsigned char>(x);
        return r;
      }
    }
    unsigned nr = 0;
    for (unsigned x = 0; x < 0x80; ++x) {
      if (!stay.has(static_cast<unsigned char>(x))) continue;
      if (nr != 0 && r.to[nr - 1] + 1u == x) {
        r.to[nr - 1] = static_cast<unsigned char>(x);
        continue;
      }
      if (nr == 4) return r;
      r.from[nr] = r.to[nr] = static_cast<unsigned char>(x);
      ++nr;
    }
    if (nr == 0) return r;
    // The ranges are read off the low seven bits, so the high half is a
    // member whole or not at all.
    unsigned high = 0;
    for (unsigned x = 0x80; x < 256; ++x)
      if (stay.has(static_cast<unsigned char>(x))) ++high;
    if (high != 0 && high != 0x80) return r;
    r.ranges = true;
    r.nr = nr;
    r.high = high != 0;
    return r;
  }
};

// Writes the advance of `var` over a run of `stay` bytes, eight at a time where
// the set has a SWAR shape; `setName` names the set as `in_set` reads it. Each
// run's exit label is its own.
class LexRunEmitter {
 public:
  void advance(EmitOutput& o, const ByteSet& stay, std::string_view var,
               std::string_view setName, bool content = true) {
    LexRunShape r = LexRunShape::of(stay);
    if (!content) r.ranges = false;
    if (!r.swar && !r.ranges) {
      o.fmt(
          "    while ({0} < len && in_set<{1}>("
          "static_cast<unsigned char>(src[{0}]))) ++{0};\n",
          var, setName);
      return;
    }
    const unsigned id = _runId++;
    o.fmt(
        "    while (len - {0} >= 8) {{\n"
        "      const ::std::uint64_t w = ld64(src + {0});\n",
        var);
    if (r.swar) {
      assert(r.lo <= LexRunShape::kSwarBelowMax &&
             "lt8 answers no byte above it");
      o.fmt("      const ::std::uint64_t bad = lt8(w, {})", r.lo);
      for (unsigned k = 0; k < r.n; ++k) o.fmt(" | eq8(w, {})", r.b[k]);
    } else {
      o.raw("      const ::std::uint64_t bad = (~(");
      std::string_view sep;
      for (unsigned k = 0; k < r.nr; ++k) {
        const bool folded = r.paired(k);
        if (folded && (r.from[k] & 0x20u)) continue;
        o.fmt("{}rng8(w, {}, {}{})", sep, r.from[k], r.to[k],
              folded ? ", 0x5f" : "");
        sep = " | ";
      }
      o.raw(r.high ? ") & kHi & ~w)" : ") & kHi) | (w & kHi)");
    }
    o.fmt(
        ";\n"
        "      if (bad) {{ {0} += static_cast<::cursiv::TextOffset>("
        "::std::countr_zero(bad) >> 3); goto Lrun{1}; }}\n"
        "      {0} += 8;\n"
        "    }}\n"
        "    while ({0} < len && in_set<{2}>("
        "static_cast<unsigned char>(src[{0}]))) \n++{0};\n"
        "Lrun{1}:;\n",
        var, id, setName);
  }

 private:
  unsigned _runId = 0;
};

// What the rungs ask of an automaton `View` names (`LexFirstByte.hpp`),
// besides the runs `lex_run_blind` crosses blind: a state whose every live
// edge loops on itself, accepting without a bound...
template <class View>
bool lex_plain_run(const View& v, std::uint16_t st) {
  if (v.accept(st) == 0 || v.bounded(st) || v.relay(st) != kNoRelay)
    return false;
  bool self = false;
  for (unsigned k = 0; k < v.classes(); ++k) {
    const auto to = v.next(st, k);
    if (to != st && to != kDeadState) return false;
    self = self || to == st;
  }
  return self;
}

// ...and the plain run a state continues into on most of its bytes, read from
// the run's own test; -1 where none.
template <class View>
int lex_continuation_of(const View& v, std::uint16_t st,
                        const std::uint8_t* spelled) {
  if (v.accept(st) == 0 || v.bounded(st) || v.relay(st) != kNoRelay) return -1;
  std::vector<unsigned> tried;
  for (unsigned k = 0; k < v.classes(); ++k) {
    const auto to = v.next(st, k);
    if (to == st || to == kDeadState || (spelled && !spelled[to]) ||
        std::ranges::find(tried, to) != tried.end())
      continue;
    tried.push_back(to);
    if (v.accept(to) != v.accept(st) || !lex_plain_run(v, to)) continue;
    unsigned common = 0, exceptions = 0;
    bool same = true;
    for (unsigned b = 0; b < 256; ++b) {
      const auto edge = v.next(st, v.cls(b));
      if (const auto run = v.next(to, v.cls(b));
          (edge == kDeadState) != (run == kDeadState)) {
        same = false;
        break;
      }
      common += edge == to;
      exceptions += edge != kDeadState && edge != to;
    }
    if (same && common > 1 && exceptions <= 4) return to;
  }
  return -1;
}

// The rungs both engines spell alike over the automaton `View` names: the runs
// a state crosses, the match it takes and the relays it launches, to compete
// with that match. An engine's emitter adds the walk around them.
template <class View>
struct LexWalkRungs : EmitOutput {
  explicit LexWalkRungs(View v) : view(v) {}

  const View view;
  LexRunEmitter runs;

  bool blind(unsigned st) const {
    return lex_run_blind(view, static_cast<std::uint16_t>(st));
  }
  bool plain_run(unsigned st) const {
    return lex_plain_run(view, static_cast<std::uint16_t>(st));
  }
  int continuation_of(unsigned st, const std::uint8_t* spelled) const {
    return lex_continuation_of(view, static_cast<std::uint16_t>(st), spelled);
  }
  void emit_match(unsigned st, std::string_view ind) {
    if (view.accept(static_cast<std::uint16_t>(st)) != 0)
      fmt("{}match.take(kTape, {}u, src, i, len, far);\n", ind, st);
  }
  void emit_relay_call(unsigned st, std::string_view ind) {
    for (auto k = view.relay(static_cast<std::uint16_t>(st)); k != kNoRelay;
         k = view.relay_of(k).next)
      fmt("{}relays.launch(kTape, {}u, src, len, start, i);\n", ind, k);
  }
  void emit_run_adv(const ByteSet& stay, std::string_view var,
                    std::string_view setName, bool content = true) {
    runs.advance(*this, stay, var, setName, content);
  }
};

// The relay rests' runs as the function `name` (`relay_run` of a table): per
// state a rest reaches and crosses blind, the advance over the bytes that keep
// it there; `relays` are the table's relays, `v` its automaton.
template <class View, class Relays>
void lex_emit_relay_runs(EmitOutput& o, const LexDfaEmitter& dfa,
                         LexRunEmitter& runs, const View& v,
                         const Relays& relays, std::string_view name) {
  o.fmt(
      "::cursiv::TextOffset {}(::std::uint16_t state, const char* src,\n"
      "    ::cursiv::TextOffset len, ::cursiv::TextOffset i) {{\n"
      "  (void)src;\n  (void)len;\n  switch (state) {{\n",
      name);
  std::vector<std::uint8_t> used(dfa.states(), 0);
  for (const LexRelay& r : relays)
    for (const unsigned st : dfa.states_from(r.resume)) used[st] = 1;
  for (unsigned st = 0; st < dfa.states(); ++st) {
    if (!used[st] || !lex_run_blind(v, static_cast<std::uint16_t>(st)))
      continue;
    const ByteSet stay = dfa.stay(st);
    if (!stay.any()) continue;
    o.fmt("    case {}u: {{\n", st);
    o.fmt(
        "    static constexpr ::std::array<::std::uint64_t, 4> kStay{{"
        "{{{}ull,{}ull,{}ull,{}ull}}}};\n",
        stay.w[0], stay.w[1], stay.w[2], stay.w[3]);
    runs.advance(o, stay, "i", "kStay");
    o.raw("    return i;\n    }\n");
  }
  o.raw("    default: return i;\n  }\n}\n");
}

// The byte-run helpers a generated walk reads, from the shared runtime.
inline void lex_emit_byte_runs(EmitOutput& o) {
  o.line("using ::cursiv::eq8;");
  o.line("using ::cursiv::in_set;");
  o.line("using ::cursiv::kHi;");
  o.line("using ::cursiv::ld64;");
  o.line("using ::cursiv::lt8;");
  o.line("using ::cursiv::rng8;");
  o.line("");
}

// The signature of the walk of every mode.
inline constexpr std::string_view kLexWalkSignature =
    "::cursiv::Scan walk_all(const char* src, ::cursiv::TextOffset len,\n"
    "         ::std::uint16_t mode, ::cursiv::TextOffset i)";

// The walk of every mode as one function, `walk_all(src, len, mode, i)`: a
// computed goto enters the mode's ladder, each state it spells is a rung, a
// state past the ladder is read in the rows from `Wtab`, and the walk ends at
// `Wdone`. The policy writes the rungs' match and relays
// (`LexDfaEmitter::state`), `walk_before` ahead of the function, the rows and
// the end; `linkage` prefixes the definition.
template <class Policy>
void lex_emit_walk(EmitOutput& o, const LexDfaEmitter& dfa, Policy& policy,
                   std::span<const std::uint16_t> starts, bool relays,
                   std::string_view linkage) {
  std::vector<unsigned> order;
  std::vector<std::uint8_t> spelled(dfa.states(), 0);
  bool partial = false;
  for (const auto start : starts) {
    const auto& mine = dfa.ladder_from(start);
    partial = partial || mine.size() != dfa.states_from(start).size();
    for (const unsigned st : mine)
      if (!spelled[st]) {
        spelled[st] = 1;
        order.push_back(st);
      }
  }
  policy.walk_before(partial);
  o.fmt("{}{} {{\n", linkage, kLexWalkSignature);
  o.raw(R"(  const ::cursiv::TextOffset start = i;
  ::cursiv::TextOffset far = i;
  ::cursiv::LexMatch match{i, 0xFFFFu};
  auto& end = match.end;
  auto& term = match.term;
  unsigned char b;
  (void)src;
  (void)len;
  (void)b;
  (void)start;
  (void)far;
)");
  if (relays) o.line("  Relays relays;");
  policy.walk_entry();
  if (partial) o.line("  ::std::uint16_t st = 0;");
  o.raw("  static const void* const kWEntry[] = {");
  for (unsigned m = 0; m < starts.size(); ++m) {
    if (m) o.raw(",");
    if (m % 8 == 0) o.raw("\n      ");
    o.fmt("&&W{}", starts[m]);
  }
  o.line("};");
  o.line("  goto *kWEntry[mode];");
  for (const unsigned st : order)
    dfa.state(o, policy, st, partial ? spelled.data() : nullptr);
  if (partial) policy.emit_rows_loop();
  policy.walk_foot();
}

}  // namespace cursiv
