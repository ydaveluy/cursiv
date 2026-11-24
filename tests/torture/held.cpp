// Tokens held over tasi::G, which ends a statement before a line break: one a
// repair holds keeps its terminal across a reduction whose tests name tokens,
// and one the machine holds where a search sets it back is read again.

#include <cstdint>
#include <memory>
#include <string_view>

#include "asi/generated/G/Module.hpp"
#include "asi/generated/G/Table.hpp"
#include "cursiv/engine/parser/Explorer.hpp"
#include "cursiv/engine/parser/Machine.hpp"
#include "cursiv/engine/parser/Parse.hpp"
#include "cursiv/engine/parser/Split.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/RecoveryLane.hpp"

namespace {

// The token an explorer shifts after `text` up to `begin`, where it holds
// `terminal` with `flags` over the bytes from `begin` to `end`.
cursiv::engine::Token shifted_after(std::string_view text, cursiv::TextOffset begin,
                                    cursiv::TextOffset end, std::uint16_t terminal,
                                    std::uint64_t flags) {
  namespace engine = cursiv::engine;
  const engine::Tables& tables = tasi::generated::G::kTables;
  cursiv::Cst cst(text);
  cursiv::CstBuilder builder(cst);
  engine::Machine m(tables, cst.text(), builder.stream(), cursiv::kNoCancel);
  engine::reserve_for(builder.stream(), text.size());
  const char* const whole = m.end;
  m.read_to(m.src + begin, tables.terminalCount);
  engine::run(m);
  m.read_to(whole, engine::kEndOfInput);
  engine::Cactus stacks;
  stacks.clear({m.frames, static_cast<std::size_t>(m.top - m.frames)});
  engine::Reading reading;
  reading.node = static_cast<std::uint32_t>(m.top - m.frames) - 1;
  reading.resume = 2 * m.under() + 1;
  reading.at = m.at;
  reading.tok = engine::token(begin, m.src + end, terminal);
  reading.tok.head |= flags;
  reading.indented = m.indented;
  engine::Explorer explorer(m, stacks, reading);
  CHECK(explorer.step() == engine::Exit::shifted);
  return explorer.taken;
}

}  // namespace

void torture_held() {
  namespace engine = cursiv::engine;
  // The terminal of `return`, as the fault mode reads it.
  const std::string_view word = "return";
  cursiv::Cst cst(word);
  cursiv::CstBuilder builder(cst);
  engine::Machine m(tasi::generated::G::kTables, cst.text(), builder.stream(), cursiv::kNoCancel);
  const std::uint16_t keyword =
      m.walk(m.src, tasi::generated::G::kTables.modeFault[0], 1, m.src).terminal();
  // `eturn` after a statement and a line break, held as `return` by a repair.
  const engine::Token held =
      shifted_after("a\neturn b;\n", 2, 7, keyword, engine::kReplaced | engine::kTold);
  CHECK(held.terminal() == keyword);
  CHECK((held.head & engine::kReplaced) != 0);
  // The text's own token is read again in the state reached.
  CHECK(shifted_after("a\neturn b;\n", 2, 7, keyword, 0).terminal() != keyword);
  // A repair after `a` reads on with `etrn` in hand, the text ends in `(`,
  // and the search sets the machine back before `etrn`, a keyword misspelt.
  const std::unique_ptr<cursiv::Parser> parser = tasi::generated::G::createParser();
  const cursiv::test::Proj back = cursiv::test::project(*parser, "a go etrn(");
  CHECK(back.accepted && back.recovered);
  // A reader set past the end of a text cut short gives the end.
  const engine::Tables& tables = tasi::generated::G::kTables;
  const std::string_view text = "a b";
  cursiv::Cst whole(text);
  cursiv::CstBuilder writes(whole);
  engine::Machine cut(tables, whole.text(), writes.stream(), cursiv::kNoCancel);
  engine::reserve_for(writes.stream(), text.size());
  cut.read_to(cut.src + 1, tables.terminalCount);
  engine::run(cut);
  const std::uint16_t mode = tables.stateMode[cut.under()];
  CHECK(mode != engine::kNoToken);
  if (mode != engine::kNoToken) {
    const engine::Token past = tables.modes[mode].read(cut, cut.src + 2, mode);
    CHECK(past.terminal() == tables.terminalCount && past.end == cut.end);
  }
}
