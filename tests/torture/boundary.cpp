// A literal whose boundary is its own: `hello` may not be followed by `;`.
// A repair reads what the lexer reads, so a misspelling before a byte the
// boundary refuses is not one -- two documents that lex alike are repaired
// alike.

#include <cstdint>
#include <string>
#include <string_view>

#include "boundary/generated/BoundaryG/Parser.hpp"
#include "boundary/generated/BoundaryG/Table.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/engines/lr/runtime/lr_rescan.hpp"
#include "cursiv/test/Check.hpp"

namespace g = tbnd::generated::BoundaryG;

namespace {

// How many of one kind the reading reported.
std::size_t said(std::string_view text, cursiv::DiagnosticKind kind) {
  cursiv::Cst cst;
  cursiv::lr_parser<g::kTable>.parse(text, cst, nullptr, g::kDrivePtr);
  std::size_t n = 0;
  for (const cursiv::ParseDiagnostic& d : cst.diagnostics())
    n += d.kind == kind ? 1u : 0u;
  return n;
}

void generic_typo_runs_keep_the_terminal_guard() {
  const auto rescan = [](std::string_view text) {
    cursiv::Cst cst;
    cursiv::CstBuilder builder(cst);
    cursiv::LrContext c(text, builder);
    c.drive = g::kDrivePtr;
    std::uint64_t read = 0;
    bool cut = false;
    const auto candidates = cursiv::lr_rescan_candidates<g::kTable>(
        c, 0, 0, static_cast<cursiv::TextOffset>(text.size()), read, cut);
    return candidates.empty() ? cursiv::Typo{} : candidates.front();
  };
  for (const auto text : {"he llo x", "he\tllo x", "he\nllo x", "he\rllo x",
                          "h llo x", "h\tllo x", "h\nllo x", "h\rllo x"}) {
    const auto typo = rescan(text);
    CHECK(typo.ok && typo.faults == 1);
    CHECK(typo.end == std::string_view(text).size() - 2);
  }
  // Whitespace, punctuation and mixed runs pay the same fault. The DFA
  // supplies the prefix and suffix, without lexing the run as another token.
  for (const auto run : {"  ", "\r\n", " @", "@@@@", "/**/", "\n@ \t"}) {
    const auto text = std::string("he") + run + "llo x";
    const auto typo = rescan(text);
    CHECK(typo.ok && typo.faults == 1);
    CHECK(typo.end == text.size() - 2);
  }
  // More than four invalid bytes, two separated faults or a missing real
  // suffix exceed the same bounds, regardless of which bytes form the fault.
  for (const auto text : {"he     llo x", "he@@@@@llo x", "he/*x*/llo x",
                          "he l lo x", "he@l@lo x", "he ", "he@",
                          "he llo; x"})
    CHECK(!rescan(text).ok);
  // This literal's explicit guard refuses ';', but permits 'E'. A typo must
  // follow that same guard, rather than imposing an extra word boundary.
  const auto prefix = rescan("he lloExtra x");
  CHECK(prefix.ok && prefix.end == 6);
  // A normal separator between already valid tokens is left alone.
  CHECK(said("hello world", cursiv::DiagnosticKind::replaced) == 0);
}

}  // namespace

void torture_boundary() {
  generic_typo_runs_keep_the_terminal_guard();
  // Spelled right and closed right: nothing to say.
  CHECK(said("hello x", cursiv::DiagnosticKind::replaced) == 0);
  CHECK(said("hello x", cursiv::DiagnosticKind::deleted) == 0);
  // Misspelled, and the boundary is clear: one repair, and it is the word.
  CHECK(said("helxo x", cursiv::DiagnosticKind::replaced) == 1);
  // ...and before a byte the boundary refuses, there is no misspelling to
  // find: `hello;` is not a reading, so `helxo;` is not a repair of one.
  CHECK(said("helxo; x", cursiv::DiagnosticKind::replaced) == 0);
  CHECK(said("hello; x", cursiv::DiagnosticKind::replaced) == 0);
}
