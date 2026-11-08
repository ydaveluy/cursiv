// `infix<>` as the entry: the model gives a climb no children of its own
// (`GrammarCost`'s own note); reaching the entry through `rule_node`, which
// walks a rule's children, would write nothing at all, every time.

#include <memory>
#include <type_traits>

#include "cursiv/grammar/expr.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/CstInvariants.hpp"
#include "cursiv/CursivLexicon.hpp"
#include "cursiv/FuzzLane.hpp"
#include "cursiv/Check.hpp"
#include "fuzz/InfixEntryGrammar.hpp"
#include "infixentry/generated/InfixEntryG/Module.hpp"

// A run the end of the text also closes is spelled with its closers alone.
template <class E>
using ClosedBy =
    typename cursiv::fuzzer::detail::closed_by<std::remove_cvref_t<E>>::type;
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

// A tree far deeper than the stack compares, and the path to a divergence is
// spelled from the root.
void trees_compare_at_any_depth(const cursiv::Parser& parser,
                                const cursiv::AstReflection& reflection) {
  const cursiv::fuzzer::AstCompare compare{
      reflection, cursiv::fuzzer::detail::kActions<finfix::InfixEntryG>};
  std::string text = "1";
  for (int i = 0; i < 100000; ++i) text += "+1";
  const auto a = parser.parse(text);
  const auto b = parser.parse(text);
  CHECK(a.value != nullptr && b.value != nullptr);
  CHECK(compare.run(*a.value, *b.value).equal);

  const auto left = parser.parse("1+2+3");
  const auto right = parser.parse("1+9+3");
  const auto apart = compare.run(*left.value, *right.value);
  CHECK(!apart.equal && apart.where == "/left/right/text");
  CHECK(apart.want == "text=2" && apart.got == "text=9");
  const auto word = parser.parse("1+2+\"a\"\"b\"");
  const auto other = compare.run(*left.value, *word.value);
  CHECK(!other.equal && other.where == "/right");

  // An optional member left empty differs from one that holds a value, even
  // one no reader can show.
  const auto bare = parser.parse("held $a");
  const auto filled = parser.parse("held $a $b");
  CHECK(bare.value != nullptr && filled.value != nullptr);
  const auto held = compare.run(*bare.value, *filled.value);
  CHECK(!held.equal && held.where == "/first");
  CHECK(compare.run(*bare.value, *parser.parse("held $b").value).equal);
}

// Each child's subtree stays inside its parent's: a parent one record too
// short cuts its first child's subtree in two, though every span agrees.
void a_subtree_holds_its_children_whole() {
  const auto judged = [](cursiv::NodeCount last) {
    cursiv::Cst cst("ab");
    cursiv::CstBuilder b(cst);
    b.stream().leaf(0, 1, 0);
    b.stream().close(0, 1, 2, 0);
    b.stream().leaf(1, 2, 0);
    b.stream().close(0, 2, last, 0);
    return cursiv::fuzzer::detail::CstInvariants{}.run(cst, "ab");
  };
  CHECK(judged(4).ok);
  const auto cut = judged(3);
  CHECK(!cut.ok && cut.at == 1);
}

int main() {
  a_subtree_holds_its_children_whole();
  namespace g = finfix::generated::InfixEntryG;
  const std::unique_ptr<cursiv::Parser> parser = g::create_parser();
  const std::unique_ptr<cursiv::AstReflection> reflection =
      g::create_ast_reflection();
  trees_compare_at_any_depth(*parser, *reflection);
  // Every write of this grammar is modelled, a converter's whole list included.
  CHECK(cursiv::fuzzer::lane<finfix::InfixEntryG>(
            "infix_entry", *parser, *reflection, 300, 0x1f1f1f) == 0);
  cursiv::fuzzer::lane<finfix::InfixSequenceG>(
      "infix_sequence", *parser, *reflection, 300, 0x2f2f2f);
  return cursiv::test::report("gen_fuzz_test");
}
