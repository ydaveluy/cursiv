#include "cursiv/engine/recovery/Unclosed.hpp"

namespace cursiv::engine {

Unclosed unclosed(const ModeLexer& mode, TextOffset p, LexWork& work) {
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
  std::uint16_t body = 0;
  const auto walk = [&](std::uint16_t s, TextOffset i, bool resets) {
    Walked w{kNoToken, i, i};
    for (;;) {
      if (mode.accepts(s)) {
        if (resets) body = 0;
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
      const std::uint16_t to = mode.step(s, byte(i));
      if (to == 0) break;
      s = to;
      ++i;
    }
    w.halt = i;
    return w;
  };
  const Walked whole = walk(1, p, true);
  if (work.cut || whole.halt <= (whole.terminal != kNoToken ? whole.end : p)) return {};
  TextOffset i = whole.halt;
  if (body == 0) return {};
  for (;;) {
    const bool admits = i < len && mode.step(body, byte(i)) != 0;
    if (i >= len || (!admits && (src[i] == '\n' || src[i] == '\r'))) {
      out.terminal = mode.leads_to(body);
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
