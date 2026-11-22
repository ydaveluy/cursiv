// A literal with its own boundary: `hello` may not be followed by `;`. A
// repair reads what the lexer reads, so two documents that lex the same are
// repaired the same.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "cursiv/engine/parser/Machine.hpp"
#include "cursiv/engine/recovery/Typos.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/Check.hpp"
#include "optionals/generated/OptionalsG/Module.hpp"
#include "optionals/generated/OptionalsG/Table.hpp"

namespace g = topt::generated::OptionalsG;

namespace {

// How many diagnostics of `kind` the parse reported.
std::size_t said(std::string_view text, cursiv::DiagnosticKind kind) {
  static const std::unique_ptr<cursiv::Parser> parser = g::createParser();
  const cursiv::ParseResult result = parser->parse(text);
  std::size_t n = 0;
  for (const cursiv::ParseDiagnostic& d : result.cst().diagnostics())
    n += d.kind == kind ? 1u : 0u;
  return n;
}

void generic_typo_runs_keep_the_terminal_guard() {
  // The misspellings of `text` as the start state reads it, the cheapest
  // first, from the fault mode's widest token as the repair finds it.
  const auto rescan = [](std::string_view text) {
    cursiv::Cst cst(text);
    cursiv::CstBuilder builder(cst);
    cursiv::engine::Machine m(g::kTables, cst.text(), builder.stream(),
                              cursiv::kNoCancel);
    const std::uint16_t mode = g::kTables.modeFault[g::kTables.stateMode[0]];
    const cursiv::engine::Token whole = m.walk(m.src, mode, 1, m.src);
    const auto plain =
        whole.terminal() != g::kTables.terminalCount
            ? static_cast<cursiv::TextOffset>(whole.end - m.src)
            : cursiv::TextOffset{0};
    cursiv::engine::LexWork work{~std::uint64_t{0}};
    return cursiv::engine::typos(m, 0, 0, plain, work);
  };
  const auto rereads = [&](std::string_view text, cursiv::TextOffset end) {
    const auto found = rescan(text);
    return !found.empty() && found.front().cost == cursiv::engine::kRereadCost &&
           found.front().end == end;
  };
  for (const auto text : {"he llo x", "he\tllo x", "he\nllo x", "he\rllo x",
                          "h llo x", "h\tllo x", "h\nllo x", "h\rllo x"})
    CHECK(rereads(text, std::string_view(text).size() - 2));
  // Whitespace, punctuation and mixed runs pay the same fault. The DFA
  // supplies the prefix and suffix, without lexing the run as another token.
  for (const auto run : {"  ", "\r\n", " @", "@@@@", "/**/", "\n@ \t"}) {
    const auto text = std::string("he") + run + "llo x";
    CHECK(rereads(text, static_cast<cursiv::TextOffset>(text.size() - 2)));
  }
  // More than four invalid bytes, two separated faults or a missing real
  // suffix exceed the same bounds, regardless of which bytes form the fault.
  for (const auto text : {"he     llo x", "he@@@@@llo x", "he/*x*/llo x",
                          "he l lo x", "he@l@lo x", "he ", "he@",
                          "he llo; x"})
    CHECK(rescan(text).empty());
  // This literal's explicit guard refuses ';', but permits 'E'. A typo must
  // follow that same guard, rather than imposing an extra word boundary.
  const auto prefix = rescan("he lloExtra x");
  CHECK(!prefix.empty() && prefix.front().end == 6);
  // A normal separator between already valid tokens is left alone.
  CHECK(said("hello world", cursiv::DiagnosticKind::Replaced) == 0);
}

}  // namespace

void torture_boundary() {
  generic_typo_runs_keep_the_terminal_guard();
  // Spelled right: no diagnostic.
  CHECK(said("hello x", cursiv::DiagnosticKind::Replaced) == 0);
  CHECK(said("hello x", cursiv::DiagnosticKind::Deleted) == 0);
  // Misspelled, and the boundary is clear: one repair, and it is the word.
  CHECK(said("helxo x", cursiv::DiagnosticKind::Replaced) == 1);
  // ...and before a byte the boundary refuses, there is no misspelling to
  // find: `hello;` is not a valid parse, so `helxo;` is not a repair of one.
  CHECK(said("helxo; x", cursiv::DiagnosticKind::Replaced) == 0);
  CHECK(said("hello; x", cursiv::DiagnosticKind::Replaced) == 0);
}
