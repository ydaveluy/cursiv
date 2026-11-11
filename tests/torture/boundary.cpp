// A literal with its own boundary: `hello` may not be followed by `;`. A
// repair reads what the lexer reads, so two documents that lex the same are
// repaired the same.

#include <cstdint>
#include <string>
#include <string_view>

#include "optionals/generated/OptionalsG/Parser.hpp"
#include "optionals/generated/OptionalsG/Table.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/lr/runtime/LrParser.hpp"
#include "cursiv/lr/runtime/lr_rescan.hpp"
#include "cursiv/Check.hpp"

namespace g = topt::generated::OptionalsG;

namespace {

// How many diagnostics of `kind` the parse reported.
std::size_t said(std::string_view text, cursiv::DiagnosticKind kind) {
  cursiv::Cst cst;
  cursiv::engine::lr_parser<g::kTable>.parse(text, cst, nullptr, &g::kDrive);
  std::size_t n = 0;
  for (const cursiv::ParseDiagnostic& d : cst.diagnostics())
    n += d.kind == kind ? 1u : 0u;
  return n;
}

void generic_typo_runs_keep_the_terminal_guard() {
  const auto rescan = [](std::string_view text) {
    cursiv::Cst cst;
    cursiv::CstBuilder builder(cst);
    cursiv::engine::LrContext c(text, builder);
    c.drive = &g::kDrive;
    std::uint64_t read = 0;
    bool cut = false;
    const auto candidates = cursiv::engine::lr_rescan_candidates<g::kTable>(
        c, 0, 0, ~cursiv::TextOffset{0}, read, cut);
    return candidates.empty() ? cursiv::engine::Typo{} : candidates.front();
  };
  for (const auto text : {"he llo x", "he\tllo x", "he\nllo x", "he\rllo x",
                          "h llo x", "h\tllo x", "h\nllo x", "h\rllo x"}) {
    const auto typo = rescan(text);
    CHECK(typo.term != cursiv::engine::kInvalidSymbol &&
          typo.cost == cursiv::engine::kRepairReread);
    CHECK(typo.end == std::string_view(text).size() - 2);
  }
  // Whitespace, punctuation and mixed runs pay the same fault. The DFA
  // supplies the prefix and suffix, without lexing the run as another token.
  for (const auto run : {"  ", "\r\n", " @", "@@@@", "/**/", "\n@ \t"}) {
    const auto text = std::string("he") + run + "llo x";
    const auto typo = rescan(text);
    CHECK(typo.term != cursiv::engine::kInvalidSymbol &&
          typo.cost == cursiv::engine::kRepairReread);
    CHECK(typo.end == text.size() - 2);
  }
  // More than four invalid bytes, two separated faults or a missing real
  // suffix exceed the same bounds, regardless of which bytes form the fault.
  for (const auto text : {"he     llo x", "he@@@@@llo x", "he/*x*/llo x",
                          "he l lo x", "he@l@lo x", "he ", "he@",
                          "he llo; x"})
    CHECK(rescan(text).term == cursiv::engine::kInvalidSymbol);
  // This literal's explicit guard refuses ';', but permits 'E'. A typo must
  // follow that same guard, rather than imposing an extra word boundary.
  const auto prefix = rescan("he lloExtra x");
  CHECK(prefix.term != cursiv::engine::kInvalidSymbol && prefix.end == 6);
  // A normal separator between already valid tokens is left alone.
  CHECK(said("hello world", cursiv::DiagnosticKind::replaced) == 0);
}

}  // namespace

void torture_boundary() {
  generic_typo_runs_keep_the_terminal_guard();
  // Spelled right: no diagnostic.
  CHECK(said("hello x", cursiv::DiagnosticKind::replaced) == 0);
  CHECK(said("hello x", cursiv::DiagnosticKind::deleted) == 0);
  // Misspelled, and the boundary is clear: one repair, and it is the word.
  CHECK(said("helxo x", cursiv::DiagnosticKind::replaced) == 1);
  // ...and before a byte the boundary refuses, there is no misspelling to
  // find: `hello;` is not a valid parse, so `helxo;` is not a repair of one.
  CHECK(said("helxo; x", cursiv::DiagnosticKind::replaced) == 0);
  CHECK(said("hello; x", cursiv::DiagnosticKind::replaced) == 0);
}
