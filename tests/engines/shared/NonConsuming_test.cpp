#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/grammar/ImageOf.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/ll/gen/ll_build.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/shared/gen/DeadBranch.hpp"
#include "cursiv/engines/shared/gen/GrammarFault.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/NestedRunWarning.hpp"
#include "cursiv/engines/shared/gen/SelfBoundedArm.hpp"
#include "cursiv/engines/shared/gen/WrittenReads.hpp"
#include "cursiv/test/Check.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace non_consuming_cases {

namespace {

struct EmptyKinds : cursiv::GrammarSpec {
  struct End : rule<eof> {};
  struct Alias : rule<ref<End>> {};
  struct Guarded : rule<gap::none + ref<Alias>> {};
  struct Either : rule<ref<Alias> | lit<"x">> {};
  struct Implied : rule<implied(lit<";">, eof)> {};
  struct Maybe : rule<option(lit<"b">)> {};
  struct Indirect : rule<ref<Maybe>> {};
  struct Consuming : rule<lit<"x"> + ref<Alias>> {};
};

template <auto Body>
struct Grammar : EmptyKinds {
  struct Root : rule<lit<"a"> + Body> {};
  using entry = Root;
};

template <auto Body>
void refuses_empty_turns(cursiv::GrammarFaultCode expected) {
  using G = Grammar<Body>;
  const auto ll = cursiv::ll_build_program(cursiv::LlLowering<G>::lower());
  const auto lr = cursiv::LrLowering<G>::lower();
  CHECK(ll.fault.code == expected);
  CHECK(lr.fault.code == expected);
  if (expected != cursiv::GrammarFaultCode::none) {
    CHECK(ll.cfg.rule_name(ll.fault.rule) == "Root");
    CHECK(lr.cfg.nt_name(lr.fault.rule) == "Root");
  }
  for (std::uint16_t r = 1; r < ll.cfg.nrules; ++r)
    if (ll.cfg.rule_name(r) == "End" || ll.cfg.rule_name(r) == "Alias")
      CHECK(!ll.ruleNullable[r]);
}

void fixed_points_require_a_finite_empty_derivation() {
  std::array<std::uint8_t, 5> values{};
  cursiv::detail::settle_empty([&](auto mark) {
    mark(values[0], values[1]);
    mark(values[1], values[0]);
    mark(values[2], values[3]);
    mark(values[3], values[2] || values[4]);
    mark(values[4], true);
  });
  CHECK(!values[0] && !values[1]);
  CHECK(values[2] && values[3] && values[4]);
}

}  // namespace

void run() {
  using E = EmptyKinds;
  using F = cursiv::GrammarFaultCode;
  fixed_points_require_a_finite_empty_derivation();
  refuses_empty_turns<E::many(E::ref<E::Alias>)>(F::nullableRepetition);
  refuses_empty_turns<E::some(E::ref<E::Guarded>)>(F::nullableRepetition);
  refuses_empty_turns<E::repeat<2, 3>(E::ref<E::Either>)>(
      F::nullableRepetition);
  refuses_empty_turns<E::repeat<2>(E::ref<E::Alias>)>(F::nullableRepetition);
  refuses_empty_turns<E::repeat<3>(E::ref<E::Either>)>(F::nullableRepetition);
  refuses_empty_turns<E::repeat<2>(E::ref<E::Implied>)>(F::nullableRepetition);
  refuses_empty_turns<E::repeat<0, 2>(E::ref<E::Guarded>)>(
      F::nullableRepetition);
  refuses_empty_turns<E::many(E::ref<E::Either>)>(F::nullableRepetition);
  refuses_empty_turns<E::many(E::ref<E::Implied>)>(F::nullableRepetition);
  refuses_empty_turns<E::ref<E::Guarded> & E::lit<"x">>(F::none);
  refuses_empty_turns<E::ref<E::Implied> & E::lit<"x">>(F::none);
  refuses_empty_turns<E::lit<"a"> & E::many(E::lit<"b">)>(F::none);
  refuses_empty_turns<E::lit<"a"> & E::ref<E::Indirect>>(F::none);
  refuses_empty_turns<E::lit<"a"> & E::ref<E::End>>(F::none);
  refuses_empty_turns<E::lit<"a">&(&E::lit<"b">)>(F::none);
  refuses_empty_turns<E::many(E::option(E::lit<"b">) & E::option(E::lit<"c">))>(
      F::nullableRepetition);
  refuses_empty_turns<E::many(E::ref<E::Indirect> & E::ref<E::Maybe>)>(
      F::nullableRepetition);
  refuses_empty_turns<E::many(E::option(E::lit<"b">) & E::lit<"c">)>(F::none);
  refuses_empty_turns<E::repeat<2>(E::ref<E::Consuming>)>(F::none);
  refuses_empty_turns<E::ref<E::Consuming> & E::lit<"y">>(F::none);

}

}  // namespace non_consuming_cases

namespace dead_branch_cases {

// The branches no text reaches, one walk for both engines: a token past
// `eof`, wherever that `eof` stands. The table engine drops each such reading
// and keeps the rest; what it keeps is never named.

namespace {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" \t">)> {};
  struct EOL : terminal<lit<"\n">> {};
  struct ADORN : terminal<lit<"===">> {};
  struct TEXT : terminal<some(cr<"a-z">)> {};
  struct B : terminal<lit<"b">> {};
  struct C : terminal<lit<"c">> {};
  using ignore = WS;
};

// A branch of a choice under a repetition that reads on past `eof`.
struct Paragraph : Base {
  struct Para
      : rule<create<Doc>(ref<TEXT> + many((ref<EOL> + ref<TEXT>) |
                                          (ref<ADORN> + eof + ref<EOL>)))> {};
  using entry = Para;
};
// The `eof` branch of a choice a token follows, and a branch whose `eof`
// stands behind another condition.
struct Conditions : Base {
  struct Ended : rule<lit<"e"> + (eof | ref<B>) + ref<C>> {};
  struct Guarded : rule<(ref<ADORN> + !ref<B> + eof + ref<C>) | ref<B>> {};
  struct Root : rule<create<Doc>(many(ref<Ended> | ref<Guarded>))> {};
  using entry = Root;
};
struct Optional : Base {
  struct Root : rule<create<Doc>(lit<"o"> + eof + option(ref<EOL>))> {};
  using entry = Root;
};
// Each reading stands: a rule past `eof` may derive nothing, a token past an
// `eof` branch may be left out, and two conditions are read as written.
struct Alive : Base {
  struct Inner : rule<ref<B> + ref<C>> {};
  struct Ruled : rule<lit<"r"> + eof + ref<Inner>> {};
  struct Spared : rule<lit<"s"> + (eof | ref<B>) + option(ref<C>)> {};
  struct Clash : rule<lit<"k"> + &lit<"a"> + !lit<"a"> + ref<TEXT>> {};
  struct Last : rule<lit<"z"> + ref<TEXT> + eof> {};
  struct Root : rule<create<Doc>(many(ref<Ruled> | ref<Spared> | ref<Clash> |
                                      ref<Last>))> {};
  using entry = Root;
};

// A doc is prose on what it documents: the branch it wraps is the branch. An
// `eof` under prose ends the sequence as a bare one does, and a creation under
// prose is still a creation, so nothing is read past `eof` there.
struct Holder : cursiv::Extends<cursiv::AstNode> {
  Doc* item{};
};
struct Prose : Base {
  struct Ended
      : rule<lit<"e"> + cursiv::Doc<std::remove_cvref_t<decltype(eof)>, "end">{} +
             ref<C>> {};
  struct Made
      : rule<lit<"m"> + eof +
             assign<&Holder::item>(
                 cursiv::Doc<decltype(create<Doc>(lit<"x">)), "made">{})> {};
  struct Root : rule<create<Doc>(many(ref<Ended> | ref<Made>))> {};
  using entry = Root;
};

template <class G>
const cursiv::LrBuildCfg& lr() {
  return cursiv::LrLowering<G>::table().cfg;
}

// Whether a production of `lhs` (any, where empty) reads the terminal `t`.
template <class G>
bool lr_reads(std::string_view t, std::string_view lhs = {}) {
  const auto& c = lr<G>();
  for (std::uint16_t k = 0; k < c.nprods; ++k) {
    const auto& q = c.prods[k];
    if (!lhs.empty() && c.nt_name(q.lhs) != lhs) continue;
    for (unsigned i = 0; i < q.len; ++i)
      if (cursiv::is_term(c.sym_at(q, i)) && c.term_name(c.sym_at(q, i)) == t)
        return true;
  }
  return false;
}

template <class G>
unsigned lr_count(std::string_view lhs) {
  const auto& c = lr<G>();
  unsigned n = 0;
  for (std::uint16_t k = 0; k < c.nprods; ++k)
    n += c.nt_name(c.prods[k].lhs) == lhs ? 1u : 0u;
  return n;
}

// Both engines lower the grammar without a refusal.
template <class G>
bool lowers() {
  const auto& r = cursiv::LrLowering<G>::table();
  const auto& l = cursiv::LlLowering<G>::table();
  return !r.fault && !r.cfg.lowerFail() && !l.fault;
}

std::vector<std::string> texts(const std::vector<cursiv::DeadBranch>& all) {
  std::vector<std::string> out;
  for (const auto& d : all) out.push_back(cursiv::dead_branch_text(d));
  return out;
}

}  // namespace

void run() {
  const auto para = cursiv::dead_branches(cursiv::grammar_image<Paragraph>());
  CHECK(para.size() == 1);
  CHECK(para.size() == 1 &&
        para[0] == cursiv::DeadBranch{"Para", "ADORN !. EOL", "", 2, 2, "EOL"});
  CHECK(texts(para) ==
        std::vector<std::string>{
            "rule `Para`: branch 2 of 2 `ADORN !. EOL` is never read -- `EOL` "
            "stands after `eof`, which nothing can follow"});
  CHECK(lowers<Paragraph>());
  CHECK(!lr_reads<Paragraph>("ADORN") && lr_reads<Paragraph>("EOL"));

  CHECK(texts(cursiv::dead_branches(cursiv::grammar_image<Conditions>())) ==
        std::vector<std::string>{
            "rule `Ended`: branch 1 of 2 `!.` in `!. | B` is never read -- "
            "`C` stands after `eof`, which nothing can follow",
            "rule `Guarded`: branch 1 of 2 `ADORN !B !. C` is never read -- "
            "`C` stands after `eof`, which nothing can follow"});
  CHECK(lowers<Conditions>());
  CHECK(lr_count<Conditions>("Ended") == 1);
  CHECK(lr_count<Conditions>("Guarded") == 1 && !lr_reads<Conditions>("ADORN"));

  CHECK(texts(cursiv::dead_branches(cursiv::grammar_image<Optional>())) ==
        std::vector<std::string>{"rule `Root`: `EOL?` is never read -- it "
                                 "stands after `eof`, which nothing can "
                                 "follow"});
  CHECK(lowers<Optional>());
  CHECK(lr_count<Optional>("Root") == 1 && !lr_reads<Optional>("EOL"));

  const auto prose = cursiv::dead_branches(cursiv::grammar_image<Prose>());
  CHECK(prose.size() == 1 && prose[0].rule == "Ended");

  CHECK(cursiv::dead_branches(cursiv::grammar_image<Alive>()).empty());
  CHECK(lowers<Alive>());
  CHECK(lr_reads<Alive>("r", "Ruled") && lr_reads<Alive>("TEXT", "Clash"));
  CHECK(lr_count<Alive>("Spared") == 3);

}

}  // namespace dead_branch_cases

namespace written_reads_cases {

// What the grammar as written may read, to the least fixpoint over rules that
// call each other: a rule that reaches itself reads nothing on the account of
// its other branches alone.

namespace {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct X : terminal<lit<"x">> {};
  struct Y : terminal<lit<"y">> {};
  using ignore = WS;

  struct Maybe : rule<option(ref<X>)> {};
  struct Wrapped : rule<ref<Maybe>> {};
  struct Needs : rule<ref<X> + ref<Maybe>> {};

  struct Cycle2;
  struct Cycle1 : rule<ref<Cycle2> | lit<"a">> {};
  struct Cycle2 : rule<ref<Cycle1> + ref<Maybe>> {};
  struct Left : rule<(ref<Left> + ref<X>) | ref<Maybe>> {};
  struct Ring2;
  struct Ring1 : rule<ref<Ring2> | ref<Maybe>> {};
  struct Ring2 : rule<ref<Ring1> + ref<Maybe>> {};

  struct Xs : rule<some(ref<X>)> {};
  struct Lead : rule<option(ref<Y>) + ref<Xs>> {};
  struct Rec : rule<(ref<Rec> + ref<X>) | ref<Xs>> {};
  struct Held : rule<ref<Xs> + ref<X>> {};
  struct Root : rule<create<Doc>(ref<Needs> + lit<";">)> {};
  using entry = Root;
};

template <class R>
cursiv::ImageId call() {
  return cursiv::grammar_image<G>().node(
      cursiv::detail::kNode<G, cursiv::Ref<R>>);
}

template <class R>
bool nothing() {
  cursiv::WrittenReads f(cursiv::grammar_image<G>());
  return f.nothing(call<R>());
}
template <class R>
bool run() {
  cursiv::WrittenReads f(cursiv::grammar_image<G>());
  return f.run(call<R>());
}

}  // namespace

void run() {
  CHECK(nothing<G::Maybe>());
  CHECK(nothing<G::Wrapped>());
  CHECK(!nothing<G::Needs>());
  CHECK(!nothing<G::X>());

  CHECK(!nothing<G::Cycle1>());
  CHECK(!nothing<G::Cycle2>());
  CHECK(nothing<G::Left>());
  CHECK(nothing<G::Ring1>());
  CHECK(nothing<G::Ring2>());

  CHECK(run<G::Xs>());
  CHECK(run<G::Lead>());
  CHECK(run<G::Rec>());
  CHECK(!run<G::Held>());
  CHECK(!run<G::Maybe>());

  cursiv::WrittenReads f(cursiv::grammar_image<G>());
  CHECK(f.run(call<G::Lead>()));
  CHECK(f.nothing(call<G::Ring2>()));
  CHECK(!f.nothing(call<G::Cycle1>()));

}

}  // namespace written_reads_cases

namespace self_bounded_arm_cases {

// The arms that begin and end with a call to their own rule, read off the
// grammar as written: what is refused and what is not.

namespace {

struct Expression : cursiv::Extends<cursiv::AstNode> {};
struct Atom : cursiv::Extends<Expression> {};
struct Binary : cursiv::Extends<Expression> {
  Expression* left{};
  string_view op;
  Expression* right{};
};
struct Ternary : cursiv::Extends<Expression> {
  Expression* test{};
  Expression* yes{};
  Expression* no{};
};
struct Call : cursiv::Extends<Expression> {
  Expression* callee{};
  Expression* index{};
};
struct List : cursiv::Extends<Expression> {
  vector<Expression*> items;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUM : terminal<some(d)> {};
  struct Plus : rule<lit<"+">> {};
  using ignore = WS;
};

struct Operator : Base {
  struct E : rule<(ref<E> + lit<"+"> + ref<E>) | ref<NUM>> {};
  using entry = E;
};
struct Juxtaposed : Base {
  struct E : rule<(ref<E> + ref<E>) | ref<NUM>> {};
  using entry = E;
};
struct Mixfix : Base {
  struct E : rule<ref<NUM> | (ref<E> + lit<"?"> + ref<E> + lit<":"> + ref<E>)> {};
  using entry = E;
};
struct Wrapped : Base {
  struct E : rule<create<Binary>(assign<&Binary::left>(ref<E>) +
                                 assign<&Binary::op>(lit<"+">) +
                                 assign<&Binary::right>(ref<E>)) |
                  create<Atom>(lit<"a">)> {};
  using entry = E;
};
struct WrappedMixfix : Base {
  struct E : rule<create<Ternary>(assign<&Ternary::test>(ref<E>) + lit<"?"> +
                                  assign<&Ternary::yes>(ref<E>) + lit<":"> +
                                  assign<&Ternary::no>(ref<E>)) |
                  create<Atom>(lit<"a">)> {};
  using entry = E;
};
struct Operand : Base {
  struct E : rule<create<Binary>(assign<&Binary::left>(ref<E>) +
                                 assign<&Binary::op>(ref<Plus>) +
                                 assign<&Binary::right>(ref<E>)) |
                  create<Atom>(lit<"a">)> {};
  using entry = E;
};
struct Condition : Base {
  struct E : rule<(ref<E> + lit<"+"> + ref<E> + !lit<"+">) | ref<NUM>> {};
  using entry = E;
};
struct LeadingCondition : Base {
  struct E : rule<(!lit<"-"> + ref<E> + lit<"+"> + ref<E>) | ref<NUM>> {};
  using entry = E;
};
struct Nested : Base {
  struct E : rule<(ref<E> + nest<Binary, &Binary::left>(
                                assign<&Binary::op>(lit<"+">) +
                                assign<&Binary::right>(ref<E>))) |
                  create<Atom>(lit<"a">)> {};
  using entry = E;
};
struct Collected : Base {
  struct E : rule<create<List>(append<&List::items>(ref<E>) + lit<","> +
                               append<&List::items>(ref<E>)) |
                  create<Atom>(lit<"a">)> {};
  using entry = E;
};
struct Choosing : Base {
  struct E : rule<create<Binary>((assign<&Binary::left>(ref<E>) + lit<"+"> +
                                  assign<&Binary::right>(ref<E>)) |
                                 assign<&Binary::op>(ref<NUM>))> {};
  using entry = E;
};
struct Many : Base {
  struct E : rule<(ref<E> + lit<"+"> + ref<E>) | (ref<E> + lit<"-"> + ref<E>) |
                  (ref<E> + ref<E>) | ref<NUM>> {};
  struct Root : rule<ref<E> + eof> {};
  using entry = Root;
};

struct Levels : Base {
  struct E;
  struct F : rule<create<Atom>(lit<"a">) | (lit<"("> + ref<E> + lit<")">)> {};
  struct T : rule<create<Binary>(assign<&Binary::left>(ref<T>) +
                                 assign<&Binary::op>(lit<"*">) +
                                 assign<&Binary::right>(ref<F>)) |
                  ref<F>> {};
  struct E : rule<create<Binary>(assign<&Binary::left>(ref<E>) +
                                 assign<&Binary::op>(lit<"+">) +
                                 assign<&Binary::right>(ref<T>)) |
                  ref<T>> {};
  using entry = E;
};
struct EndsWithToken : Base {
  struct E : rule<create<Call>(assign<&Call::callee>(ref<E>) + lit<"["> +
                               assign<&Call::index>(ref<E>) + lit<"]">) |
                  create<Atom>(lit<"a">)> {};
  using entry = E;
};
struct BeginsWithToken : Base {
  struct E : rule<create<Call>(lit<"-"> + assign<&Call::callee>(ref<E>)) |
                  create<Atom>(lit<"a">)> {};
  using entry = E;
};
struct Postfix : Base {
  struct E : rule<create<Call>(assign<&Call::callee>(ref<E>) + lit<"!">) |
                  create<Atom>(lit<"a">)> {};
  using entry = E;
};
struct Unit : Base {
  struct E : rule<ref<NUM> | (many(ref<E> + lit<"+">) + ref<E>)> {};
  using entry = E;
};
struct Mutual : Base {
  struct F;
  struct E : rule<(ref<F> + lit<"+"> + ref<F>) | ref<NUM>> {};
  struct F : rule<ref<E> | lit<"x">> {};
  using entry = E;
};
struct Ranked : Base {
  struct Primary : rule<create<Atom>(lit<"a">)> {};
  struct E : infix<Primary, &Binary::left, &Binary::op, &Binary::right,
                   (left(lit<"*">) >> left(lit<"+">))> {};
  using entry = E;
};

template <class G>
std::vector<std::string> said() {
  std::vector<std::string> out;
  for (const auto& a : cursiv::self_bounded_arms(cursiv::grammar_image<G>()))
    out.push_back(cursiv::self_bounded_arm_text(a));
  return out;
}

template <class G>
std::size_t arms(const char* rule) {
  std::size_t n = 0;
  for (const auto& a : cursiv::self_bounded_arms(cursiv::grammar_image<G>())) n += a.rule == rule;
  return n;
}

}  // namespace

void run() {
  CHECK(said<Operator>() ==
        std::vector<std::string>{
            "rule `E`: the arm `E '+' E` begins and ends with `E`, which says "
            "nothing of how its operands nest -- declare the operators with "
            "infix<>, or give each level its own rule (`E := E + T | T`)"});
  CHECK(arms<Juxtaposed>("E") == 1);
  CHECK(arms<Mixfix>("E") == 1);
  CHECK(arms<Wrapped>("E") == 1);
  CHECK(arms<WrappedMixfix>("E") == 1);
  CHECK(arms<Operand>("E") == 1);
  CHECK(arms<Condition>("E") == 1);
  CHECK(arms<LeadingCondition>("E") == 1);
  CHECK(arms<Nested>("E") == 1);
  CHECK(arms<Collected>("E") == 1);
  CHECK(arms<Choosing>("E") == 1);
  CHECK(arms<Many>("E") == 3);

  CHECK(said<Levels>().empty());
  CHECK(said<EndsWithToken>().empty());
  CHECK(said<BeginsWithToken>().empty());
  CHECK(said<Postfix>().empty());
  CHECK(said<Unit>().empty());
  CHECK(said<Mutual>().empty());
  CHECK(said<Ranked>().empty());

}

}  // namespace self_bounded_arm_cases

namespace nested_run_warning_cases {

// The runs nested in a run, read off the grammar as written: in place, behind
// a rule, behind a choice, or behind parts that read nothing. A repetition
// whose item stops at one length, or whose runs the lexer reads whole, stays
// silent.

namespace {

struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Doc*> kids;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct X : terminal<lit<"x">> {};
  struct Y : terminal<lit<"y">> {};
  using ignore = WS;
};

struct Direct : Base {
  struct Root : rule<create<Doc>(many(some(ref<X>)) + lit<";">)> {};
  using entry = Root;
};
struct Twice : Base {
  struct Root : rule<create<Doc>(some(some(ref<X>)) + lit<";">)> {};
  using entry = Root;
};
struct Behind : Base {
  struct Xs : rule<some(ref<X>)> {};
  struct Root : rule<create<Doc>(many(ref<Xs>) + lit<";">)> {};
  using entry = Root;
};
struct Chained : Base {
  struct Xs : rule<some(ref<X>)> {};
  struct Mid : rule<ref<Xs>> {};
  struct Root : rule<create<Doc>(many(ref<Mid>) + lit<";">)> {};
  using entry = Root;
};
struct Chosen : Base {
  struct Xs : rule<some(ref<X>)> {};
  struct Item : rule<ref<Xs> | ref<Y>> {};
  struct Root : rule<create<Doc>(many(ref<Item>) + lit<";">)> {};
  using entry = Root;
};
struct Inline : Base {
  struct Root : rule<create<Doc>(many(some(ref<X>) | ref<Y>) + lit<";">)> {};
  using entry = Root;
};
struct Spared : Base {
  struct Xs : rule<some(ref<X>)> {};
  struct Item : rule<option(ref<Y>) + ref<Xs>> {};
  struct Root : rule<create<Doc>(some(ref<Item>) + lit<";">)> {};
  using entry = Root;
};
struct Trailed : Base {
  struct Root
      : rule<create<Doc>(some(some(ref<X>) + option(lit<",">)) + lit<";">)> {};
  using entry = Root;
};
struct Counted : Base {
  struct Root : rule<create<Doc>(repeat<2>(some(ref<X>)) + lit<";">)> {};
  using entry = Root;
};
struct Grouped : Base {
  struct Leaf : rule<create<Doc>(lit<"x">)> {};
  struct Root : rule<create<Doc>(many(append<&Doc::kids>(create<Doc>(
                                     some(append<&Doc::kids>(ref<Leaf>))))) +
                                 lit<";">)> {};
  using entry = Root;
};
struct Leading : Base {
  struct Xs : rule<some(ref<X>)> {};
  struct Root
      : rule<create<Doc>(many(many(ref<X>) + ref<Xs>) + lit<";">)> {};
  using entry = Root;
};

struct Flat : Base {
  struct Root : rule<create<Doc>(many(ref<X>) + lit<";">)> {};
  using entry = Root;
};
struct Single : Base {
  struct Root : rule<create<Doc>(option(some(ref<X>)) + lit<";">)> {};
  using entry = Root;
};
struct Separated : Base {
  struct Root : rule<create<Doc>(some(some(ref<X>), lit<",">) +
                                 many(some(ref<X>), lit<",">) + lit<";">)> {};
  using entry = Root;
};
struct Followed : Base {
  struct Word : rule<ref<X> + lit<"w">> {};
  struct Root : rule<create<Doc>(many(ref<Word>) + many(ref<X> + some(ref<Y>)) +
                                 many(some(ref<Y>) + ref<X>) + lit<";">)> {};
  using entry = Root;
};
struct Fixed : Base {
  struct Root : rule<create<Doc>(some(repeat<2>(ref<X>)) + lit<";">)> {};
  using entry = Root;
};
struct Lexical : Base {
  struct Word : terminal<many(some(cr<"a-z">))> {};
  struct Root : rule<create<Doc>(many(ref<Word>) + lit<";">)> {};
  using entry = Root;
};
struct Recursive : Base {
  struct Head : rule<lit<"h">> {};
  struct List : rule<(ref<List> + ref<X>) | ref<Head>> {};
  struct Root : rule<create<Doc>(many(ref<List>) + lit<";">)> {};
  using entry = Root;
};

template <class G>
std::vector<std::string> said() {
  std::vector<std::string> out;
  for (const auto& w : cursiv::nested_run_warnings(cursiv::grammar_image<G>()))
    out.push_back(cursiv::nested_run_warning_text(w));
  return out;
}

template <class G>
bool names(const char* rule, const char* run) {
  const auto all = cursiv::nested_run_warnings(cursiv::grammar_image<G>());
  return all.size() == 1 && all[0] == cursiv::NestedRunWarning{rule, run};
}

}  // namespace

void run() {
  CHECK(names<Direct>("Root", "(X+)*"));
  CHECK(said<Direct>() ==
        std::vector<std::string>{
            "rule `Root`: `(X+)*` repeats a run, so a run of items reads as "
            "one group or as several and the election picks one -- write one "
            "repetition unless the groups are wanted"});
  CHECK(names<Twice>("Root", "(X+)+"));
  CHECK(names<Behind>("Root", "Xs*"));
  CHECK(names<Chained>("Root", "Mid*"));
  CHECK(names<Chosen>("Root", "Item*"));
  CHECK(cursiv::nested_run_warnings(cursiv::grammar_image<Inline>()).size() == 1);
  CHECK(names<Spared>("Root", "Item+"));
  CHECK(cursiv::nested_run_warnings(cursiv::grammar_image<Trailed>()).size() == 1);
  CHECK(cursiv::nested_run_warnings(cursiv::grammar_image<Counted>()).size() == 1);
  CHECK(cursiv::nested_run_warnings(cursiv::grammar_image<Grouped>()).size() == 1);
  CHECK(cursiv::nested_run_warnings(cursiv::grammar_image<Leading>()).size() == 1);

  CHECK(cursiv::nested_run_warnings(cursiv::grammar_image<Flat>()).empty());
  CHECK(cursiv::nested_run_warnings(cursiv::grammar_image<Single>()).empty());
  CHECK(cursiv::nested_run_warnings(cursiv::grammar_image<Separated>()).empty());
  CHECK(cursiv::nested_run_warnings(cursiv::grammar_image<Followed>()).empty());
  CHECK(cursiv::nested_run_warnings(cursiv::grammar_image<Fixed>()).empty());
  CHECK(cursiv::nested_run_warnings(cursiv::grammar_image<Lexical>()).empty());
  CHECK(cursiv::nested_run_warnings(cursiv::grammar_image<Recursive>()).empty());

}

}  // namespace nested_run_warning_cases

int main() {
  cursiv::test::count_expectations();
  non_consuming_cases::run();
  dead_branch_cases::run();
  written_reads_cases::run();
  self_bounded_arm_cases::run();
  nested_run_warning_cases::run();
  return cursiv::test::report("NonConsuming_test");
}
