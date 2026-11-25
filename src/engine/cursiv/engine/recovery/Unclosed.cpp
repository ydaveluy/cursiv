#include "cursiv/engine/recovery/Unclosed.hpp"

#include <algorithm>

namespace cursiv::engine {

Unclosed unclosed(const ModeLexer& mode, TextOffset p, LexWork& work, LexVisits& visits) {
  const Tables& t = mode.t;
  const char* const src = mode.m.src;
  const auto len = static_cast<TextOffset>(mode.m.end - src);
  const auto byte = [src](TextOffset i) { return static_cast<unsigned char>(src[i]); };
  Unclosed out;
  if (p >= len || t.lexOpeners[byte(p)] == 0) return out;
  // The walk from state `s` at `i`: its longest token, and where it stops.
  struct Walked {
    std::uint16_t terminal;
    TextOffset end;
    TextOffset halt;
  };
  // The state byte `b` leads to from `s`, none where the mode reads nothing
  // through it; each state the mode is asked about is met.
  const auto step = [&](std::uint16_t s, unsigned char b) -> std::uint16_t {
    const std::uint16_t to = t.transition(s, t.byteClass[b]);
    if (to == 0) return 0;
    visits.add(to);
    return mode.lives(to) ? to : std::uint16_t{0};
  };
  // The terminals the matcher reads whose first bytes end at `i`, read from
  // `p`: where the furthest ends, and the first the text ends inside.
  TextOffset matched = p;
  Unclosed lead;
  const auto match = [&](std::uint16_t s, TextOffset i) {
    for (LexResolution r = mode.m.reading(mode.win.at(0, t.lexStates[s].accept), src + i, &work);
         r.test == LexTest::match && !work.cut;
         r = mode.m.reading(r.otherwise, src + i, &work)) {
      const char* found = nullptr;
      const auto read = mode.m.on_text([&] {
        found = mode.m.match(r.waits, src + p);
        return found != nullptr;
      });
      if (found != nullptr) {
        matched = std::max(matched, static_cast<TextOffset>(found - src));
        work.spend(static_cast<TextOffset>(found - src) - p);
      } else if (read.atCut && lead.terminal == kNoToken) {
        lead = {.terminal = r.token, .open = i, .end = len};
        work.spend(len - p);
      }
    }
  };
  std::uint16_t body = 0;
  const auto walk = [&](std::uint16_t s, TextOffset i, bool resets) {
    Walked w{kNoToken, i, i};
    for (;;) {
      visits.add(s);
      if (mode.accepts(s)) {
        if (resets) {
          body = 0;
          match(s, i);
        }
      } else if (t.lexBodies[s] != 0) {
        if (body == 0) out.open = i;
        body = s;
      }
      if (mode.m.src + i >= mode.m.mark) mode.m.watch(mode.m.src + i);
      if (const Ending e = mode.ending(s, i, work); e.terminal != kNoToken) {
        w.terminal = e.terminal;
        w.end = e.end;
      }
      if (i >= len || !work.spend()) break;
      const std::uint16_t to = step(s, byte(i));
      if (to == 0) break;
      s = to;
      ++i;
    }
    w.halt = i;
    return w;
  };
  const Walked whole = walk(1, p, true);
  const TextOffset read = std::max(whole.terminal != kNoToken ? whole.end : p, matched);
  if (work.cut) return {};
  if (lead.terminal != kNoToken && read < len) return lead;
  if (whole.halt <= read) return {};
  TextOffset i = whole.halt;
  if (body == 0) return {};
  for (;;) {
    const bool admits = i < len && step(body, byte(i)) != 0;
    if (i >= len || (!admits && (src[i] == '\n' || src[i] == '\r'))) {
      out.body = body;
      out.end = i;
      return out;
    }
    const TextOffset from = admits ? i : i + 1;
    const Walked rest = walk(body, from, false);
    if (work.cut) return {};
    if (rest.terminal != kNoToken) {
      out.terminal = rest.terminal;
      out.end = rest.end;
      out.closed = true;
      return out;
    }
    i = rest.halt;
  }
}

}  // namespace cursiv::engine
