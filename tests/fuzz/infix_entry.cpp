// The fuzzer on grammars of its own: `infix<>` as the entry, and the implied
// tokens and gap conditions of the asi grammar.

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include "asi/generated/G/Module.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/CstInvariants.hpp"
#include "cursiv/CursivLexicon.hpp"
#include "cursiv/FixedSource.hpp"
#include "cursiv/FuzzLane.hpp"
#include "cursiv/GrammarCost.hpp"
#include "cursiv/GrammarGenerator.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "engines/lr/AsiGrammar.hpp"
#include "fuzz/InfixEntryGrammar.hpp"
#include "infixentry/generated/InfixEntryG/Module.hpp"

// A run the end of the text also closes is spelled with its closers alone.
template <class E>
using ClosedBy =
    typename cursiv::fuzzer::detail::closed_by<std::remove_cvref_t<E>>::type;
template <class E>
using Plain = std::remove_cvref_t<E>;
static_assert(std::is_same_v<ClosedBy<decltype(cursiv::GrammarSpec::lit<"*/"> |
                                               cursiv::GrammarSpec::eof)>,
                             Plain<decltype(cursiv::GrammarSpec::lit<"*/">)>>);
static_assert(std::is_same_v<ClosedBy<decltype(cursiv::GrammarSpec::lit<"a"> |
                                               cursiv::GrammarSpec::lit<"b"> |
                                               cursiv::GrammarSpec::eof)>,
                             Plain<decltype(cursiv::GrammarSpec::lit<"a"> |
                                            cursiv::GrammarSpec::lit<"b">)>>);
static_assert(std::is_same_v<ClosedBy<decltype(cursiv::GrammarSpec::lit<"a"> |
                                               cursiv::GrammarSpec::lit<"b"> |
                                               cursiv::GrammarSpec::lit<"c">)>,
                             Plain<decltype(cursiv::GrammarSpec::lit<"a"> |
                                            cursiv::GrammarSpec::lit<"b"> |
                                            cursiv::GrammarSpec::lit<"c">)>>);

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
    cursiv::detail::CstBuilder b(cst);
    b.stream().leaf(0, 1, 0);
    b.stream().close(0, 1, 2, 0);
    b.stream().leaf(1, 2, 0);
    b.stream().close(0, 2, last, 0);
    return cursiv::fuzzer::detail::CstInvariants{}.run(cst, "ab");
  };
  CHECK(judged(4).ok);
  const auto cut = judged(3);
  CHECK(!cut.ok && cut.at == 1);

  // Ends never go back along the run, from one root to the next too.
  cursiv::Cst roots("ab");
  cursiv::detail::CstBuilder b(roots);
  b.stream().leaf(1, 2, 0);
  b.stream().leaf(0, 1, 0);
  const auto back = cursiv::fuzzer::detail::CstInvariants{}.run(roots, "ab");
  CHECK(!back.ok && back.at == 1);
}

// A repetition costs its child's cost times its minimum, held at unreachable
// rather than wrapped while the child is still unreachable.
void a_long_repetition_costs_its_rounds() {
  namespace fd = cursiv::fuzzer::detail;
  using K = fd::ElementKind;
  // 0 holds the repetition 1 of the rule 2, which reads the literal 3.
  struct Model final : fd::GrammarModel {
    cursiv::ElementId kids[3] = {1, 2, 3};
    [[nodiscard]] cursiv::ElementId positionCount() const noexcept override {
      return 4;
    }
    [[nodiscard]] cursiv::ElementId entryRule() const noexcept override {
      return 0;
    }
    [[nodiscard]] K kindOf(cursiv::ElementId e) const noexcept override {
      constexpr K kKinds[] = {K::group, K::repetition, K::parser_rule,
                              K::literal};
      return kKinds[e];
    }
    [[nodiscard]] std::span<const cursiv::ElementId> childrenOf(
        cursiv::ElementId e) const noexcept override {
      return e < 3 ? std::span<const cursiv::ElementId>(kids + e, 1)
                   : std::span<const cursiv::ElementId>{};
    }
    [[nodiscard]] std::string_view textOf(
        cursiv::ElementId e) const noexcept override {
      return e == 3 ? "a" : "";
    }
    [[nodiscard]] std::string_view nameOf(
        cursiv::ElementId) const noexcept override {
      return {};
    }
    [[nodiscard]] std::string_view featureOf(
        cursiv::ElementId) const noexcept override {
      return {};
    }
    [[nodiscard]] std::uint16_t minOf(
        cursiv::ElementId e) const noexcept override {
      return e == 1 ? 4096 : 0;
    }
    [[nodiscard]] std::uint16_t maxOf(
        cursiv::ElementId e) const noexcept override {
      return e == 1 ? fd::kModelUnbounded : 0;
    }
  };
  const fd::GrammarCost cost(Model{}, fd::kActions<finfix::InfixEntryG>);
  CHECK(cost[1] == 4096 && cost[0] == 4096);
}

// A word, then a number: the separator the walk writes between them is a tab.
// A word no digit may follow stands there; one no whitespace may follow does
// not.
namespace fveto {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  string_view word;
  string_view num;
};
struct Spaced : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct WORD : terminal<some(cr<"a-z">) + !d> {};
  struct NUM : terminal<some(d)> {};
  struct DIGIT : terminal<d> {};
  struct Root : rule<create<Doc>(assign<&Doc::word>(ref<WORD>) +
                                 assign<&Doc::num>(ref<NUM>))> {};
  using entry = Root;
  using ignored = WS;
};
struct Tight : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct WORD : terminal<some(cr<"a-z">) + !s> {};
  struct NUM : terminal<some(d)> {};
  struct Root : rule<create<Doc>(assign<&Doc::word>(ref<WORD>) +
                                 assign<&Doc::num>(ref<NUM>))> {};
  using entry = Root;
  using ignored = WS;
};
// No trivia, so tokens follow one another: a keyword's word boundary refuses
// a word after it but not a bracket, and a guard that is no class refuses the
// literal.
struct Glued : cursiv::GrammarSpec {
  struct WORD : terminal<some(cr<"a-z">)> {};
  struct Root : rule<create<Doc>(kw<"go"> + assign<&Doc::word>(ref<WORD>))> {};
  using entry = Root;
};
struct Opened : cursiv::GrammarSpec {
  struct Root : rule<create<Doc>(kw<"go"> + lit<"(">)> {};
  using entry = Root;
};
struct Unheld : cursiv::GrammarSpec {
  struct Root : rule<create<Doc>(lit<"go", !lit<"x">> + lit<"(">)> {};
  using entry = Root;
};
}  // namespace fveto

// What a spelling writes with the first choice of every decision, or nothing
// when it refuses.
template <class C = finfix::InfixEntryG, class E>
std::optional<std::string> spelled(E) {
  namespace fd = cursiv::fuzzer::detail;
  fd::FixedSource first;
  fd::SpellCtx cx{first};
  std::string out;
  if (!fd::Spell<C, E, fd::kSpellDepth>::run(out, fd::SpellStyle::shortest, cx))
    return std::nullopt;
  return out;
}

// What an assertion refuses holds for the next character spelled, whatever
// spells it; a refusal past ASCII refuses every character past ASCII.
void a_refusal_holds_for_the_next_character() {
  CHECK(spelled(!cursiv::GrammarSpec::cr<"ab"> +
                (cursiv::GrammarSpec::lit<"a"> |
                 cursiv::GrammarSpec::lit<"c">)) == std::nullopt);
  CHECK(spelled(!cursiv::GrammarSpec::cr<"ab"> +
                (cursiv::GrammarSpec::lit<"c"> |
                 cursiv::GrammarSpec::lit<"a">)) == "c");
  CHECK(spelled(!cursiv::GrammarSpec::cr<"ab"> +
                cursiv::GrammarSpec::cr<"a-c">) == "c");
  CHECK(spelled(!cursiv::GrammarSpec::cr<"é"> + cursiv::GrammarSpec::cr<"é">) ==
        std::nullopt);
  CHECK(spelled(!cursiv::GrammarSpec::cr<"é"> +
                cursiv::GrammarSpec::lit<"é">) == std::nullopt);
  CHECK(spelled(!cursiv::GrammarSpec::cr<"é"> + cursiv::GrammarSpec::cr<"a">) ==
        "a");
}

// A literal's guard refuses after its text: a class, a terminal that reads
// one, or a keyword's word boundary where the word ends in it. Any other guard
// refuses the spelling.
void a_literal_guard_holds_after_its_text() {
  using G = fveto::Spaced;
  CHECK(spelled(cursiv::GrammarSpec::lit<"a", !cursiv::GrammarSpec::cr<"b">> +
                cursiv::GrammarSpec::cr<"b-c">) == "ac");
  CHECK(spelled<G>(
            cursiv::GrammarSpec::lit<"a", !cursiv::GrammarSpec::ref<G::DIGIT>> +
            cursiv::GrammarSpec::cr<"0~">) == "a~");
  CHECK(spelled(cursiv::GrammarSpec::kw<"if"> +
                cursiv::GrammarSpec::cr<"0~">) == "if~");
  CHECK(spelled(cursiv::GrammarSpec::kw<"["> + cursiv::GrammarSpec::cr<"0~">) ==
        "[0");
  CHECK(spelled(cursiv::GrammarSpec::lit<"a", !cursiv::GrammarSpec::lit<"b">> +
                cursiv::GrammarSpec::cr<"b-c">) == std::nullopt);
  CHECK(spelled<G>(
            cursiv::GrammarSpec::lit<"a", !cursiv::GrammarSpec::ref<G::NUM>> +
            cursiv::GrammarSpec::cr<"b">) == std::nullopt);
}

// Whether the walk writes a sentence of `G` with the first choice of every
// decision: what a token's last assertion refuses holds for the byte after it.
template <class G>
bool walks_a_sentence() {
  namespace fd = cursiv::fuzzer::detail;
  const fd::GrammarCost cost(fd::kDeclaredModel<G>, fd::kActions<G>);
  const fd::GrammarGenerator gen{fd::kDeclaredModel<G>,
                                 fd::kLexicon<G>,
                                 fd::kActions<G>,
                                 cost,
                                 8,
                                 nullptr};
  fd::FixedSource first;
  cursiv::fuzzer::WalkTree tree;
  return gen.generate(first, tree).complete;
}

int main() {
  a_subtree_holds_its_children_whole();
  CHECK(walks_a_sentence<fveto::Spaced>());
  CHECK(!walks_a_sentence<fveto::Tight>());
  CHECK(!walks_a_sentence<fveto::Glued>());
  CHECK(walks_a_sentence<fveto::Opened>());
  CHECK(!walks_a_sentence<fveto::Unheld>());
  a_long_repetition_costs_its_rounds();
  a_refusal_holds_for_the_next_character();
  a_literal_guard_holds_after_its_text();
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
  // An implied token is always written out: the walk cannot tell where the
  // parser would imply it.
  const std::unique_ptr<cursiv::Parser> asi =
      tasi::generated::G::create_parser();
  cursiv::fuzzer::lane<tasi::G>(
      "asi", *asi, *tasi::generated::G::create_ast_reflection(), 300, 0x3f3f3f);
  return cursiv::test::report("gen_fuzz_test");
}
