// The fuzzer on grammars of its own: `infix<>` as the entry, and the implied
// tokens and gap conditions of the asi grammar.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "asi/generated/G/Module.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/CstInvariants.hpp"
#include "cursiv/CursivActions.hpp"
#include "cursiv/CursivLexicon.hpp"
#include "cursiv/FixedSource.hpp"
#include "cursiv/FuzzLane.hpp"
#include "cursiv/GrammarCost.hpp"
#include "cursiv/GrammarGenerator.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "grammars/AsiGrammar.hpp"
#include "fuzz/InfixEntryGrammar.hpp"
#include "infixentry/generated/InfixEntryG/Module.hpp"

// A run the end of the text also closes is spelled with its closers alone.
template <class E>
using ClosedBy =
    typename cursiv::fuzzer::detail::closed_by<std::remove_cvref_t<E>>::type;
template <class E>
using Plain = std::remove_cvref_t<E>;
using ScopedKeyword = cursiv::detail::scoped_t<
    finfix::InfixEntryG, Plain<decltype(finfix::InfixEntryG::kw<"if">)>>;
static_assert(cursiv::fuzzer::detail::model::text_of<ScopedKeyword>::value == "if");
using FuzzGrammar = finfix::InfixEntryG;
using Nested = decltype(FuzzGrammar::nest<finfix::ast::Bin, &finfix::ast::Bin::left>(FuzzGrammar::lit<"+">));
static_assert(cursiv::fuzzer::detail::text_entry<FuzzGrammar, Nested>() == nullptr);
static_assert(cursiv::fuzzer::detail::model::kind_of<decltype(FuzzGrammar::indent)>() ==
              cursiv::fuzzer::detail::ElementKind::layout);
static_assert(cursiv::fuzzer::detail::model::kind_of<FuzzGrammar::terminal<FuzzGrammar::dedent>>() ==
              cursiv::fuzzer::detail::ElementKind::layout);
static_assert(cursiv::fuzzer::detail::Spell<finfix::InfixEntryG, ScopedKeyword, 0>::min == 2);
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

  // Ends never go back along the run, from one root to the next too.
  cursiv::Cst roots("ab");
  cursiv::CstBuilder b(roots);
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
  CHECK(cost[1].tokens == 4096 && cost[0].tokens == 4096);
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
// Adjacent tokens must satisfy the previous token's guard.
struct Glued : cursiv::GrammarSpec {
  struct WORD : terminal<some(cr<"a-z">)> {};
  struct Root : rule<create<Doc>(kw<"go"> + assign<&Doc::word>(ref<WORD>))> {};
  using entry = Root;
};
struct Opened : cursiv::GrammarSpec {
  struct Root : rule<create<Doc>(kw<"go"> + lit<"(">)> {};
  using entry = Root;
};
struct Guarded : cursiv::GrammarSpec {
  struct Root : rule<create<Doc>(lit<"go", !lit<"x">> + lit<"(">)> {};
  using entry = Root;
};
struct Layout : cursiv::GrammarSpec {
  struct WS : terminal<lit<" ">> {};
  struct Root : rule<create<Doc>(lit<"x"> + lit<"\n"> +
                                 (lit<"y"> | (indent + lit<"z"> + dedent)) +
                                 lit<"w">)> {};
  using entry = Root;
  using ignored = WS;
};
struct LineComment : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Line : terminal<lit<"!"> + many(cr<R"(^\r\n)">)> {};
  struct Root : rule<create<Doc>(ref<Line> + lit<"x">)> {};
  using entry = Root;
  using ignored = WS;
};
struct LineDirective : cursiv::GrammarSpec {
  struct Line : terminal<lit<"#"> + many(cr<R"(^\r\n)">) + option(eol)> {};
  struct Root : rule<create<Doc>(ref<Line> + lit<";">)> {};
  using entry = Root;
};
struct NamedLiteral : cursiv::GrammarSpec {
  struct X : terminal<lit<"x">> {};
  struct Root : rule<create<Doc>(ref<X> + lit<";"> + lit<"x">)> {};
  using entry = Root;
};
struct SharedPrefix : cursiv::GrammarSpec {
  struct A : terminal<cr<"a">> {};
  struct Word : terminal<ref<A> | lit<"ab">> {};
  struct Root : rule<create<Doc>(ref<Word> + lit<"b">)> {};
  using entry = Root;
};
struct TerminalAlias : SharedPrefix {
  struct Alias : terminal<ref<A>> {};
  struct Root : rule<create<Doc>(ref<Alias>)> {};
  using entry = Root;
};
struct SpacedPrefix : SharedPrefix {
  struct WS : terminal<lit<" ">> {};
  using ignored = WS;
};
struct VisibleSpace : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<create<Doc>(lit<"["> + tight(option(ref<WS>) + lit<"x"> + lit<"]">))> {};
  using entry = Root;
  using ignored = WS;
};
struct Adjacent : cursiv::GrammarSpec {
  struct WS : terminal<lit<" ">> {};
  struct Root : rule<create<Doc>(lit<"("> + gap::none + cr<"a-z"> +
                                 gap::none + any + lit<")"> + lit<";">)> {};
  using entry = Root;
  using ignored = WS;
};
struct NullableTail : cursiv::GrammarSpec {
  struct Stop : rule<create<Doc>> {};
  struct Tail : rule<(lit<"x"> + ref<Tail>) | ref<Stop>> {};
  using entry = Tail;
};
struct ConditionalTail : cursiv::GrammarSpec {
  struct End : rule<&lit<"z"> | eof> {};
  struct Tail : rule<(lit<"x"> + ref<Tail>) | ref<End> | lit<";">> {};
  struct Root : rule<create<Doc>(ref<Tail> + lit<"z">)> {};
  using entry = Root;
};
struct Digits : cursiv::GrammarSpec {
  struct Number : terminal<some(d)> {};
  struct Root : rule<create<Doc>(ref<Number> + ref<Number>)> {};
  using entry = Root;
};
struct SpacedDigits : Digits {
  struct WS : terminal<some(s)> {};
  using ignored = WS;
};
struct CommentTail : cursiv::GrammarSpec {
  struct Comment : terminal<lit<"#"> + many(cr<R"(^\r\n)">)> {};
  struct Root : rule<create<Doc>(ref<Comment> + lit<"x">)> {};
  using entry = Root;
};
struct SeparatedComment : CommentTail {
  struct WS : terminal<some(s)> {};
  using ignored = WS;
};
struct VisibleBlank : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Blank : terminal<some(cr<" ">)> {};
  struct Root : rule<create<Doc>(lit<"("> + ref<Blank> + lit<")">)> {};
  using entry = Root;
  using ignored = WS;
};
struct FinalToken : cursiv::GrammarSpec {
  struct End : terminal<lit<";"> + eof> {};
  struct Root : rule<create<Doc>(ref<End>)> {};
  using entry = Root;
};
struct PastEnd : FinalToken {
  struct Root : rule<create<Doc>(ref<End> + lit<"!">)> {};
  using entry = Root;
};
struct FinalTrivia : cursiv::GrammarSpec {
  struct Comment : terminal<lit<"#"> + eof> {};
  struct Root : rule<create<Doc>(lit<"x"> + lit<"y">)> {};
  using entry = Root;
  using hidden = list<Comment>;
};
struct Brackets : cursiv::GrammarSpec {
  struct Line : terminal<lit<"\n">> {};
  struct Open : terminal<lit<"(">> {};
  using indent_newline = Line;
  static constexpr auto indent_open = ref<Open>;
  static constexpr auto indent_block = lit<"{">;
  static constexpr auto indent_close = lit<"}"> | lit<")">;
  static constexpr auto item = ref<Line> | lit<"x">;
  struct Root : rule<create<Doc>(ref<Open> + item + lit<"{"> + item +
                                 lit<"}"> + item + lit<")"> + item)> {};
  using entry = Root;
};
struct ForcedLine : Brackets {
  struct Root : rule<create<Doc>(ref<Open> + ref<Line> + lit<")">)> {};
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
  CHECK(spelled(G::lit<"a", !G::lit<"b">> + G::cr<"b-c">) == "ac");
  CHECK(spelled(!(G::ikw<"IF"> | G::lit<"j">) + G::cr<"i-k">) == "k");
  CHECK(spelled<G>(
            cursiv::GrammarSpec::lit<"a", !cursiv::GrammarSpec::ref<G::NUM>> +
            cursiv::GrammarSpec::cr<"b">) == std::nullopt);
}

// The sentence obtained from the first choice at every decision.
template <class G>
std::optional<std::string> first_sentence() {
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
  auto sentence = gen.generate(first, tree);
  if (!sentence.complete) return std::nullopt;
  return std::move(sentence.text);
}

// A level that groups nothing: two of its operators are no expression.
namespace fnonassoc {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Atom : cursiv::Extends<Expr> {
  string_view name;
};
struct Binary : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  string_view op;
  Expr* right = nullptr;
};
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Primary : rule<create<Atom>(assign<&Atom::name>(ref<ID>))> {};
  struct E : infix<Primary, Binary, levels(nonassoc(lit<"<">))> {};
  using entry = E;
  using ignored = WS;
};
}  // namespace fnonassoc

// The longest chain of operators, and the first of every other choice.
struct Chained final : cursiv::fuzzer::detail::DecisionSource {
  unsigned choose(cursiv::fuzzer::detail::DecisionKind kind,
                  unsigned n) override {
    return kind == cursiv::fuzzer::detail::DecisionKind::byte &&
                   n == cursiv::fuzzer::detail::kGrammarOpCap + 1
               ? n - 1
               : 0;
  }
};

// The walk writes no two operators of a non-associative level in a row.
void a_non_associative_level_is_not_chained() {
  namespace fd = cursiv::fuzzer::detail;
  using G = fnonassoc::G;
  const fd::GrammarCost cost(fd::kDeclaredModel<G>, fd::kActions<G>);
  const fd::GrammarGenerator gen{fd::kDeclaredModel<G>,
                                 fd::kLexicon<G>,
                                 fd::kActions<G>,
                                 cost,
                                 8,
                                 nullptr};
  Chained longest;
  cursiv::fuzzer::WalkTree tree;
  const fd::GrammarDerivation d = gen.generate(longest, tree);
  CHECK(d.complete);
  CHECK(std::ranges::count(d.text, '<') == 1);
}

// After adoption a write targets the adopted node and builds its operand separately.
namespace finline {
struct Child : cursiv::Extends<cursiv::AstNode> {};
struct Root : cursiv::Extends<cursiv::AstNode> {
  Child* child = nullptr;
  vector<reference<Child>> names;
  bool marked = false;
  bool empty = false;
};
struct G : cursiv::GrammarSpec {
  struct Name : terminal<as<&finfix::from_parts>(lit<"n">)> {};
  struct Refused : terminal<as<&finfix::checked>(lit<"no">)> {};
  struct Head : rule<create<Root>(lit<"h">)> {};
  struct Top
      : rule<flag<&Root::marked>(ref<Head> + lit<":"> +
                                 assign<&Root::child>(create<Child>(lit<"x">))) +
             append<&Root::names>(ref<Name>) + append<&Root::names>(ref<Refused>) +
             append<&Root::names>(lit<"z">) +
             flag<&Root::empty>(eof)> {};
  using entry = Top;
};
}  // namespace finline

void an_operand_builds_the_value_of_its_member() {
  namespace fd = cursiv::fuzzer::detail;
  using G = finline::G;
  const fd::GrammarCost cost(fd::kDeclaredModel<G>, fd::kActions<G>);
  const fd::GrammarGenerator gen{fd::kDeclaredModel<G>,
                                 fd::kLexicon<G>,
                                 fd::kActions<G>,
                                 cost,
                                 8,
                                 nullptr};
  fd::FixedSource first;
  cursiv::fuzzer::WalkTree tree;
  const fd::GrammarDerivation d = gen.generate(first, tree);
  CHECK(d.complete);
  const auto* const root = cursiv::ast_cast<finline::Root>(d.tree);
  CHECK(root != nullptr && root->child != nullptr && root->marked && root->empty);
  CHECK(root && root->names.size() == 2 && root->names[0].refText == "parts:n" &&
        root->names[1].refText == "z");
  CHECK(d.unwritten.empty());
}

// Two values a floating type tells apart never read as one.
void a_wide_float_is_not_read_as_a_double() {
  using cursiv::fuzzer::detail::Render;
  const long double one = 1.0L;
  const long double next = std::nextafter(one, 2.0L);
  std::string a, b;
  CHECK(!(Render<long double>::run(one, a) &&
          Render<long double>::run(next, b)) ||
        a != b);
}

int main() {
  CHECK(cursiv::fuzzer::detail::shown(std::string_view{"a\0b\n\\x00", 8}) ==
        "a\\x00b\\x0a\\\\x00");
  a_subtree_holds_its_children_whole();
  CHECK(first_sentence<fveto::Spaced>());
  CHECK(!first_sentence<fveto::Tight>());
  CHECK(!first_sentence<fveto::Glued>());
  CHECK(first_sentence<fveto::Opened>());
  CHECK(first_sentence<fveto::Guarded>() == "go(");
  CHECK(first_sentence<fveto::Adjacent>() == "(a!) ;");
  CHECK(first_sentence<fveto::NullableTail>() == "xxxxxxxx");
  CHECK(first_sentence<fveto::ConditionalTail>() == "xxxxxxxx;z");
  CHECK(!first_sentence<fveto::Digits>());
  CHECK(first_sentence<fveto::SpacedDigits>() == "0\t0");
  CHECK(!first_sentence<fveto::CommentTail>());
  CHECK(first_sentence<fveto::SeparatedComment>() == "#\nx");
  CHECK(first_sentence<fveto::VisibleBlank>() == "( )");
  CHECK(first_sentence<fveto::FinalToken>() == ";");
  CHECK(!first_sentence<fveto::PastEnd>());
  CHECK(first_sentence<fveto::FinalTrivia>() == "xy");
  CHECK(first_sentence<fveto::Layout>() == "x \ny w");
  CHECK(first_sentence<fveto::LineComment>() == "!\nx");
  CHECK(first_sentence<fveto::LineDirective>() == "#\n;");
  CHECK(first_sentence<fveto::NamedLiteral>() == "x;x");
  CHECK(!first_sentence<fveto::SharedPrefix>());
  CHECK(first_sentence<fveto::TerminalAlias>() == "a");
  CHECK(first_sentence<fveto::SpacedPrefix>() == "a b");
  CHECK(first_sentence<fveto::VisibleSpace>() == "[x]");
  CHECK(first_sentence<fveto::Brackets>() == "(x{\n}x)\n");
  CHECK(!first_sentence<fveto::ForcedLine>());
  a_long_repetition_costs_its_rounds();
  a_refusal_holds_for_the_next_character();
  a_literal_guard_holds_after_its_text();
  a_non_associative_level_is_not_chained();
  a_wide_float_is_not_read_as_a_double();
  an_operand_builds_the_value_of_its_member();
  namespace g = finfix::generated::InfixEntryG;
  const std::unique_ptr<cursiv::Parser> parser = g::createParser();
  const std::unique_ptr<cursiv::AstReflection> reflection =
      g::createAstReflection();
  trees_compare_at_any_depth(*parser, *reflection);
  // Every write of this grammar is modelled, a converter's whole list included.
  CHECK(cursiv::fuzzer::lane<finfix::InfixEntryG>(
            "infix_entry", *parser, *reflection, 300, 0x1f1f1f) == 0);
  cursiv::fuzzer::lane<finfix::InfixSequenceG>(
      "infix_sequence", *parser, *reflection, 300, 0x2f2f2f);
  // An implied token is always written out: the walk cannot tell where the
  // parser would imply it.
  const std::unique_ptr<cursiv::Parser> asi =
      tasi::generated::G::createParser();
  cursiv::fuzzer::lane<tasi::G>(
      "asi", *asi, *tasi::generated::G::createAstReflection(), 300, 0x3f3f3f);
  // Every reading of the first `go` fails at `@`, three real tokens past the
  // bare `go`'s: the repair mends `@` alone.
  const cursiv::ParseResult mended = asi->parse("go a\ngo b\n@c\n");
  CHECK(mended.diagnostics.size() == 1 && mended.diagnostics[0].begin == 10);
  constexpr std::string_view block = "{ (a; } b(); } c();";
  const auto groups = asi->parse(block);
  const auto* doc = groups.valueAs<tasi::ast::Doc>();
  CHECK(groups.diagnostics.size() == 1 &&
        groups.diagnostics[0].kind == cursiv::DiagnosticKind::Replaced &&
        groups.diagnostics[0].begin == 2 && groups.diagnostics[0].end == 3);
  CHECK(doc && doc->body.size() == 2 && doc->body[0]->body.size() == 2 &&
        doc->body[0]->body[0]->body.size() == 1 && doc->body[0]->body[1]->head == "b" &&
        doc->body[1]->head == "c");
  CHECK(cursiv::fuzzer::detail::CstInvariants{}.run(groups.cst(), block).ok);
  return cursiv::test::report("gen_fuzz_test");
}
