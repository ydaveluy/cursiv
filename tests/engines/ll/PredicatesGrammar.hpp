#pragma once

// A condition in a rule starts on the token in hand, so each probe ends in a
// run of the tokens its condition reads first: any other token would be the
// one in hand there. Where the probe wants a longer token to be the one in
// hand, the absorbing REST stands alone.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "engines/ll/PredicatesAst.hpp"

namespace tpredicates {

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct REST : terminal<some(any)> {};
  struct DIGITS : terminal<some(d)> {};
  struct Split : rule<ref<ID> + lit<"b">> {};
  struct QualifiedName : rule<ref<ID> + many(lit<"."> + ref<ID>)> {};
  struct Guarded : rule<ref<ID> + gap::none + lit<"{">> {};
  struct Reserved : terminal<!kw<"if"> + some(cr<"a-z">)> {};
  struct NumberDot : terminal<some(d) + lit<".">> {};
  static constexpr auto Tight = tight(ref<ID> + lit<"{">);
  static constexpr auto Choice = (lit<"a"> | ref<ID>)+lit<"b">;
  static constexpr auto Optional = option(ref<ID>) + lit<"b">;
  static constexpr auto Repeated = many(ref<ID>) + lit<"b">;
  static constexpr auto Exact = repeat<2>(ref<ID>) + lit<"{">;
  struct ChoiceRead : rule<Choice> {};
  struct OptionalRead : rule<Optional> {};
  struct RepeatedRead : rule<Repeated> {};
  struct ExactRead : rule<Exact> {};
  static constexpr auto Committed =
      option(commit(lit<"a">, lit<"x">)) + lit<"a">;
  struct CommittedRead : rule<Committed> {};
  static constexpr auto TailC = lit<"x"> + many(commit(lit<"a">, lit<"b">));
  template <class E>
  static constexpr auto Region =
      tight(skip(ignore<WS>, hide<COMMENT>, E{} + gap::none + lit<"b">));
  struct RegionMany : rule<Region<decltype(many(lit<"a">))>> {};
  struct RegionSome : rule<Region<decltype(some(lit<"a">))>> {};
  struct RegionExact : rule<Region<decltype(repeat<2>(lit<"a">))>> {};
  struct RegionBounded : rule<Region<decltype(repeat<1, 3>(lit<"a">))>> {};
  struct RegionManyC
      : rule<Region<decltype(many(commit(lit<"a">, option(lit<"c">))))>> {};
  struct RegionSomeC
      : rule<Region<decltype(some(commit(lit<"a">, option(lit<"c">))))>> {};
  struct RegionBoundedC
      : rule<Region<decltype(repeat<2, 3>(
            commit(lit<"a">, option(lit<"c">))))>> {};
  struct IF : terminal<kw<"if">> {};
  struct KeywordChoice : rule<(kw<"if"> + lit<"x">) | (ref<ID> + lit<"y">)> {};
  struct EqualPriority : rule<(kw<"if"> + lit<"x">) | (ref<IF> + lit<"y">)> {};
  struct MixedTerminal : terminal<kw<"if"> | lit<"raw">> {};
  struct MixedChoice
      : rule<(ref<MixedTerminal> + lit<"x">) | (ref<ID> + lit<"y">)> {};
  struct TwoLetters : terminal<repeat<2>(cr<"a-z">)> {};
  struct MixedIf : terminal<kw<"if"> | lit<"if">> {};
  struct MixedBoundary
      : rule<(ref<MixedIf> + lit<"x">) | (ref<TwoLetters> + lit<"y">)> {};
  struct KeywordOption : rule<option(ref<ID>) + kw<"if">> {};
  struct KeywordMany : rule<many(ref<ID>) + kw<"if">> {};
  struct CountedC
      : rule<repeat<2, 4>(commit(lit<"a">, option(lit<"a">))) + lit<"a">> {};
  static constexpr auto OverId = some(ref<ID>);
  static constexpr auto OverIdBrace = some(ref<ID> | lit<"{">);
  static constexpr auto OverIdBraces =
      some(ref<ID> | lit<"{"> | lit<"}"> | ref<DIGITS>);
  static constexpr auto OverIdDigits = some(ref<ID> | ref<DIGITS>);
  static constexpr auto OverPrefixes =
      some(lit<"a"> | lit<"ab"> | lit<"c"> | lit<"x">);
  static constexpr auto OverChoice = some(lit<"a"> | ref<ID>);
  static constexpr auto OverIdB = some(ref<ID> | lit<"b">);
  static constexpr auto OverReserved = some(ref<Reserved>);
  static constexpr auto OverDotted = some(ref<NumberDot> | ref<DIGITS>);
  static constexpr auto OverCommit = some(lit<"a"> | lit<"x">);
  static constexpr auto OverTail =
      some(lit<"x"> | lit<"a"> | lit<"b"> | lit<"c">);
  static constexpr auto OverLetters = some(lit<"a"> | lit<"b"> | lit<"c">);
  static constexpr auto OverKeywords = some(kw<"if"> | ref<ID>);
  static constexpr auto OverTies = some(kw<"if"> | ref<IF> | ref<ID>);
  static constexpr auto OverMixed = some(ref<MixedTerminal> | ref<ID>);
  static constexpr auto OverBoundary =
      some(ref<MixedIf> | ref<TwoLetters> | lit<"x"> | lit<"y">);
  static constexpr auto OverKeywordsDigits =
      some(kw<"if"> | ref<ID> | ref<DIGITS>);
  static constexpr auto OverLiteral = some(lit<"a">);
  template <class R, class Over>
  static constexpr auto Probing = &(ref<R> + eof) + Over{};
  struct Root
      : rule<create<Node>(
            ((lit<"p:"> + &ref<Split> + OverId) |
             (lit<"n:"> + !ref<Split> + OverId) |
             (lit<"r:"> + assign<&Node::text>(ref<Split>)) |
             (lit<"@"> +
              !(ref<QualifiedName> +
                !kw<"{">)+assign<&Node::text>(ref<QualifiedName>) +
              lit<"{">) |
             (lit<":"> + assign<&Node::text>(ref<QualifiedName>) + lit<"{">) |
             (lit<"seq:"> + &(ref<ID> + &lit<"{">)+OverIdBrace) |
             (lit<"nested:"> + !(ref<ID> + !(lit<"{"> + lit<"}">)) +
              OverIdBraces) |
             (lit<"prefix-negation:"> +
              !((lit<"a"> | lit<"ab">)+!lit<"c">)+OverPrefixes) |
             (lit<"and:"> + &(&ref<ID>)+OverIdDigits) |
             (lit<"head:"> + &ref<ID> + OverId) |
             (lit<"tight-head:"> + tight(&ref<ID> + ref<ID>)) |
             (lit<"not:"> + !&ref<ID> + OverIdDigits) |
             (lit<"andnot:"> + &!ref<ID> + OverIdDigits) |
             (lit<"notnot:"> + !!ref<ID> + OverIdDigits) |
             (lit<"gap:"> + &ref<Guarded> + OverIdBrace) |
             (lit<"tight:"> + &Tight + OverIdBrace) |
             (lit<"choice:"> + &(Choice + eof) + OverChoice) |
             (lit<"choice-read:"> + assign<&Node::text>(ref<ChoiceRead>)) |
             (lit<"optional:"> + &(Optional + eof) + OverIdB) |
             (lit<"optional-read:"> + assign<&Node::text>(ref<OptionalRead>)) |
             (lit<"many:"> + &(Repeated + eof) + OverIdB) |
             (lit<"many-read:"> + assign<&Node::text>(ref<RepeatedRead>)) |
             (lit<"exact:"> + &(Exact + eof) + OverIdBrace) |
             (lit<"exact-read:"> + assign<&Node::text>(ref<ExactRead>)) |
             (lit<"guard:"> + &ref<Reserved> + OverReserved) |
             (lit<"guard-read:"> + assign<&Node::text>(ref<Reserved>)) |
             (lit<"inline:"> + &ref<terminal<some(d) + lit<".">>> +
              OverDotted) |
             (lit<"inline-read:"> + assign<&Node::text>(ref<NumberDot>)) |
             (lit<"commit:"> + &(Committed + eof) + OverCommit) |
             (lit<"commit-read:"> + assign<&Node::text>(ref<CommittedRead>)) |
             (lit<"tail-c:"> + &TailC + OverTail) |
             (lit<"tight-tail-c:"> + tight(&TailC) + OverTail) |
             (lit<"region-many:"> +
              Probing<RegionMany, decltype(OverLetters)>) |
             (lit<"region-many-read:"> + assign<&Node::text>(ref<RegionMany>)) |
             (lit<"region-some:"> +
              Probing<RegionSome, decltype(OverLetters)>) |
             (lit<"region-some-read:"> + assign<&Node::text>(ref<RegionSome>)) |
             (lit<"region-exact:"> +
              Probing<RegionExact, decltype(OverLetters)>) |
             (lit<"region-exact-read:"> +
              assign<&Node::text>(ref<RegionExact>)) |
             (lit<"region-bounded:"> +
              Probing<RegionBounded, decltype(OverLetters)>) |
             (lit<"region-bounded-read:"> +
              assign<&Node::text>(ref<RegionBounded>)) |
             (lit<"region-many-c:"> +
              Probing<RegionManyC, decltype(OverLetters)>) |
             (lit<"region-many-c-read:"> +
              assign<&Node::text>(ref<RegionManyC>)) |
             (lit<"region-some-c:"> +
              Probing<RegionSomeC, decltype(OverLetters)>) |
             (lit<"region-some-c-read:"> +
              assign<&Node::text>(ref<RegionSomeC>)) |
             (lit<"region-bounded-c:"> +
              Probing<RegionBoundedC, decltype(OverLetters)>) |
             (lit<"region-bounded-c-read:"> +
              assign<&Node::text>(ref<RegionBoundedC>)) |
             (lit<"keyword-choice:"> +
              Probing<KeywordChoice, decltype(OverKeywords)>) |
             (lit<"keyword-choice-read:"> +
              assign<&Node::text>(ref<KeywordChoice>)) |
             (lit<"equal-priority:"> +
              Probing<EqualPriority, decltype(OverTies)>) |
             (lit<"equal-priority-read:"> +
              assign<&Node::text>(ref<EqualPriority>)) |
             (lit<"mixed-choice:"> +
              Probing<MixedChoice, decltype(OverMixed)>) |
             (lit<"mixed-choice-read:"> +
              assign<&Node::text>(ref<MixedChoice>)) |
             (lit<"mixed-boundary:"> +
              Probing<MixedBoundary, decltype(OverBoundary)>) |
             (lit<"mixed-boundary-read:"> +
              assign<&Node::text>(ref<MixedBoundary>)) |
             (lit<"keyword-option:"> +
              Probing<KeywordOption, decltype(OverKeywords)>) |
             (lit<"keyword-option-read:"> +
              assign<&Node::text>(ref<KeywordOption>)) |
             (lit<"keyword-many:"> +
              Probing<KeywordMany, decltype(OverKeywords)>) |
             (lit<"keyword-many-read:"> +
              assign<&Node::text>(ref<KeywordMany>)) |
             (lit<"counted-c:"> + Probing<CountedC, decltype(OverLiteral)>) |
             (lit<"counted-c-read:"> + assign<&Node::text>(ref<CountedC>)) |
             (lit<"composite:"> + &ref<ID> + !kw<"if"> + gap::none +
              assign<&Node::text>(ref<ID>)) |
             (lit<"either:"> + ((!ref<ID>) | (&ref<ID> + &kw<"if">)) +
              OverKeywordsDigits) |
             (lit<"white:"> + gap::is(some(s)) + assign<&Node::text>(ref<ID>)) |
             (lit<"raw:"> + gap::has(repeat<2, 4>(s) + lit<"x">) +
              assign<&Node::text>(ref<ID>)) |
             (lit<"unicode-gap:"> + gap::has(lit<"x", !cr<"α">>) +
              assign<&Node::text>(ref<ID>)) |
             (lit<"unicode-inner:"> + &(ref<ID> + gap::has(lit<"x", !cr<"α">>) +
                                        lit<"{">)+OverIdBrace) |
             (lit<"end:"> + &(ref<ID> + eof) + OverId) |
             (lit<"absorb-id:"> + &ref<ID> + assign<&Node::text>(ref<REST>)) |
             (lit<"absorb-not:"> + !ref<ID> + assign<&Node::text>(ref<REST>)) |
             (lit<"absorb-lit:"> + !lit<"a"> +
              assign<&Node::text>(ref<REST>))) +
            eof)> {};
  using entry = Root;
  using ignored = WS;
  using hidden = COMMENT;
};

struct Ll : Base {};
}  // namespace tpredicates
