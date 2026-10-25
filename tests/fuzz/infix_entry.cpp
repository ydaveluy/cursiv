// `infix<>` as the entry: the model gives a climb no children of its own
// (`GrammarCost`'s own note); reaching the entry through `rule_node`, which
// walks a rule's children, would write nothing at all, every time.

#include <memory>
#include <type_traits>

#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/fuzz/CursivLexicon.hpp"
#include "cursiv/fuzz/FuzzLane.hpp"
#include "cursiv/test/Check.hpp"
#include "fuzz/InfixEntryGrammar.hpp"
#include "infixentry/generated/InfixEntryG/Module.hpp"

// A run the end of the text also closes is spelled with its closers alone.
template <class E>
using ClosedBy =
    typename cursiv::fuzzer::closed_by<std::remove_cvref_t<E>>::type;
template <class E>
using Plain = std::remove_cvref_t<E>;
static_assert(std::is_same_v<ClosedBy<decltype(cursiv::lit<"*/"> | cursiv::eof)>,
                             Plain<decltype(cursiv::lit<"*/">)>>);
static_assert(
    std::is_same_v<ClosedBy<decltype(cursiv::lit<"a"> | cursiv::lit<"b"> |
                                     cursiv::eof)>,
                   Plain<decltype(cursiv::lit<"a"> | cursiv::lit<"b">)>>);
static_assert(
    std::is_same_v<ClosedBy<decltype(cursiv::lit<"a"> | cursiv::lit<"b"> |
                                     cursiv::lit<"c">)>,
                   Plain<decltype(cursiv::lit<"a"> | cursiv::lit<"b"> |
                                  cursiv::lit<"c">)>>);

int main() {
  namespace g = finfix::generated::InfixEntryG;
  const std::unique_ptr<cursiv::Parser> parser = g::create_parser();
  const std::unique_ptr<cursiv::AstReflection> reflection =
      g::create_ast_reflection();
  cursiv::fuzzer::lane<finfix::InfixEntryG>(
      "infix_entry", *parser, *reflection, 300, 0x1f1f1f);
  cursiv::fuzzer::lane<finfix::InfixSequenceG>(
      "infix_sequence", *parser, *reflection, 300, 0x2f2f2f);
  return cursiv::test::report("gen_fuzz_test");
}
