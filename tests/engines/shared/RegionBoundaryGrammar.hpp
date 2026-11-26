#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tregionboundary {
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
  Document* in{};
};

struct Words : cursiv::GrammarSpec {
  static constexpr auto word_boundary = cr<"a-z$">;
  struct Root : rule<create<Document>(kw<"foo"> + (lit<"$"> | lit<"_">))> {};
  using entry = Root;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NOTE : terminal<lit<"~">> {};
  struct AZ : terminal<lit<"az">> {};
  struct X : terminal<lit<"x">> {};
  struct AC : terminal<lit<"a"> + some(lit<"c">)> {};
  static constexpr auto item = append<&Document::items>(ref<X>);

  // The final required X and an iteration of many(X) can shift the same
  // token. Only the next position reads a token; completing the repetition
  // must not impose its inside configuration on the outside closing token.
  struct Required : rule<create<Document>(
      lit<"required:"> + tight(lit<"["> + many(item) + item) +
      lit<"]"> + lit<".">)> {};
  struct Nonempty : rule<create<Document>(
      lit<"some:"> + tight(lit<"["> + some(item)) +
      lit<"]"> + lit<".">)> {};
  struct AtLeastTwo
      : rule<create<Document>(
            lit<"two:"> +
            tight(lit<"["> + repeat<2, cursiv::GrammarSpec::kUnbounded>(item)) +
            lit<"]"> + lit<".">)> {};
  // The empty arm ends the region immediately after '['. The decision may
  // inspect ']' through caller trivia, but a chosen X still reads tightly.
  struct Optional : rule<create<Document>(
      lit<"optional:"> + tight(lit<"["> + option(item)) +
      lit<"]"> + lit<".">)> {};
  struct Fixed : rule<create<Document>(
      lit<"fixed:"> + tight(lit<"["> + item + item) +
      lit<"]"> + lit<".">)> {};
  struct ClosingInside : rule<create<Document>(
      lit<"inside:"> + tight(lit<"["> + many(item) + lit<"]">) +
      lit<".">)> {};
  struct Run : rule<many(item) + item> {};
  struct Called : rule<
      lit<"called:"> + tight(lit<"["> + ref<Run>) +
      lit<"]"> + lit<".">> {};
  struct OptionalRun : rule<option(item)> {};
  struct CalledOptional : rule<
      lit<"called-option:"> + tight(lit<"["> + ref<OptionalRun>) +
      lit<"]"> + lit<".">> {};
  struct CalledEntry : rule<
      lit<"called-entry:"> + tight(ref<OptionalRun>) + lit<"]"> + lit<".">> {};
  // A rule before what the region's tail may leave out ends the region on its
  // last token when that part is absent.
  struct CalledTail : rule<
      lit<"called-tail:"> + tight(lit<"["> + ref<Run> + option(lit<"!">)) +
      lit<"]"> + lit<".">> {};
  struct CalledMany : rule<
      lit<"called-many:"> + tight(lit<"["> + ref<Run> + many(lit<"!">)) +
      lit<"]"> + lit<".">> {};
  struct CalledUnordered : rule<
      lit<"called-unordered:"> +
      tight(lit<"["> + unordered(ref<Run>, option(lit<"!">))) + lit<"]"> +
      lit<".">> {};
  struct OptionalBang : rule<option(lit<"!">)> {};
  struct CalledNullable : rule<
      lit<"called-nullable:"> + tight(lit<"["> + ref<Run> + ref<OptionalBang>) +
      lit<"]"> + lit<".">> {};
  // An unchanged trivia configuration introduces no region boundary, even
  // when its last item is optional.
  struct Same : rule<create<Document>(
      lit<"same:"> + skip(ignore<WS>, hide<NOTE>,
                          lit<"["> + option(item)) +
      lit<"]"> + lit<".">)> {};
  // Sharing a first byte, or a proper prefix, does not make two tokens the
  // same. Caller trivia "az" also starts with that byte but hides no item.
  struct SameHead : rule<skip(ignore<WS>, hide<NOTE, AZ>, create<Document>(
      lit<"same-head:"> +
      tight(lit<"["> + option(append<&Document::items>(lit<"aa">))) +
      lit<"ab"> + lit<".">))> {};
  struct PatternHead : rule<skip(ignore<WS>, hide<NOTE, AZ>, create<Document>(
      lit<"pattern-head:"> +
      tight(lit<"["> + many(append<&Document::items>(ref<AC>))) +
      lit<"ab"> + lit<".">))> {};
  struct ShortItem : rule<create<Document>(
      lit<"short-item:"> +
      tight(lit<"["> + option(append<&Document::items>(lit<"a">))) +
      lit<"ab"> + lit<".">)> {};
  struct LongItem : rule<create<Document>(
      lit<"long-item:"> +
      tight(lit<"["> + option(append<&Document::items>(lit<"ab">))) +
      lit<"a"> + lit<".">)> {};
  // A nest that opens a region: the gap before its first token is the
  // caller's, as before any other first token of a region.
  struct NestHead : rule<
      lit<"nest-head:"> + create<Document>(item) +
      tight(nest<&Document::in>(lit<"."> + item) | nest<&Document::in>(lit<":"> + item)) + lit<";">> {};
  // A commit's loop that ends a region: the gap after its last iteration is the
  // caller's, as after a plain loop.
  struct CommitTail : rule<create<Document>(
      lit<"commit-tail:"> + tight(many(commit(lit<",">, item))) + lit<";">)> {};
  struct CommitNote : rule<create<Document>(
      lit<"commit-note:"> +
      skip(ignore<WS>, hide<>, lit<"["> + many(commit(lit<",">, item))) +
      lit<";">)> {};
  // Left-recursive runs that may read nothing open a region: each keeps its
  // own write.
  struct Commas : rule<(ref<Commas> + lit<",">) | option(lit<"y">)> {};
  struct Bangs : rule<(ref<Bangs> + lit<"!">) | option(lit<"z">)> {};
  struct Dots : rule<(ref<Dots> + lit<".">) | option(lit<"w">)> {};
  struct LeadingRuns : rule<create<Document>(
      lit<"runs:"> +
      tight(append<&Document::items>(ref<Commas>) +
            append<&Document::items>(ref<Bangs>) +
            append<&Document::items>(ref<Dots>) + lit<";">))> {};
  // A condition skips the note its caller reads here as a token, however many
  // stand before the name it looks for.
  struct SkipHas : rule<create<Document>(
      lit<"skip-has:"> +
      ((&lit<"x"> + many(lit<"~">) + (item | lit<"z">)) |
       (many(lit<"~">) + lit<"y">)) +
      lit<".">)> {};
  // ...and as far past the end of the record that asks it.
  struct SkipInner : rule<create<Document>(lit<"a"> + !lit<"x">)> {};
  struct SkipRecord : rule<create<Document>(
      lit<"skip-rec:"> + assign<&Document::in>(ref<SkipInner>) +
      many(lit<"~">) + option(item) + lit<".">)> {};
  struct SkipNested : rule<create<Document>(
      lit<"skip-nested:"> + &(!lit<"y"> + (lit<"x"> | lit<"y">)) +
      many(lit<"~">) + (item | lit<"y">) + lit<".">)> {};
  struct SkipNot : rule<create<Document>(
      lit<"skip-not:"> + !lit<"x"> + many(lit<"~">) + option(item) +
      lit<".">)> {};
  // Only the second arm skips blanks alone: line ends typed behind `<` run on
  // to the `?` there, and stop at the blank in the first arm.
  struct LINES : terminal<some(lit<"\n">)> {};
  struct BLANKS : terminal<some(lit<" ">)> {};
  struct ByLines : rule<ref<X>> {};
  struct ByBlanks : rule<ref<X>> {};
  struct Lines : rule<create<Document>(
                     lit<"lines:"> +
                     ((ref<ByLines> + lit<"<"> +
                       &(ref<LINES> + lit<"!">)+ref<LINES> + lit<"!">) |
                      (ref<ByBlanks> +
                       skip(ignore<BLANKS>, hide<>,
                            lit<"<"> + &(ref<LINES> + lit<"?">)+ref<LINES> +
                                lit<"?">))))> {};
  struct P1 : terminal<lit<"p">> {};
  struct P2 : terminal<lit<"p">> {};
  struct A : terminal<lit<"a">> {};
  struct Space : terminal<lit<" ">> {};
  static constexpr auto gate = &(gap::is(lit<" ">) + lit<"b">);
  struct GapCache : rule<create<Document>(
      (skip(ignore<A>, hide<>, ref<P1> + ref<Space>) + gate + lit<"b">) |
      (ref<P2> + ref<A> + gate + append<&Document::items>(lit<"b">)))> {};
  struct Root
      : rule<(lit<"gap-cache:"> + ref<GapCache>) |
             (lit<"section:"> + section<Words>(lit<"[">, lit<"]">)) |
             create<Document>(lit<"host:"> + kw<"foo"> + (lit<"$"> | lit<"_">)) |
             ref<Required> | ref<Nonempty> | ref<AtLeastTwo> | ref<Optional> |
             ref<Fixed> | ref<ClosingInside> | ref<Called> |
             ref<CalledOptional> | ref<CalledEntry> | ref<CalledTail> |
             ref<CalledMany> | ref<CalledUnordered> | ref<CalledNullable> |
             ref<Same> | ref<SameHead> | ref<PatternHead> | ref<ShortItem> |
             ref<LongItem> | ref<NestHead> | ref<CommitTail> | ref<CommitNote> |
             ref<LeadingRuns> | ref<SkipHas> | ref<SkipNot> | ref<SkipNested> |
             ref<SkipRecord> | ref<Lines>> {};
  using entry = Root;
  using ignored = WS;
  using hidden = NOTE;
};
}  // namespace tregionboundary

namespace tregionconsumption {
struct Block : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  Block* block{};
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NOTE : terminal<lit<"~">> {};
  struct X : terminal<lit<"x">> {};
  static constexpr auto item = append<&Block::items>(ref<X>);
  static constexpr auto optionalBlock =
      assign<&Document::block>(create<Block>(option(item)));
  static constexpr auto nonemptyBlock =
      assign<&Document::block>(create<Block>(some(item)));
  static constexpr auto repeatedBlock =
      assign<&Document::block>(create<Block>(many(item)));

  // The nullable repetition has its own AST/CST owner. Opening that owner's
  // record must not commit the decision's caller trivia before an item.
  struct Owned
      : rule<create<Document>(lit<"owned:"> + tight(lit<"["> + optionalBlock) +
                              lit<"]"> + lit<".">)> {};
  struct Nonempty
      : rule<create<Document>(lit<"block-some:"> + tight(lit<"["> + nonemptyBlock) +
                              lit<"]"> + lit<".">)> {};

  // Without an opening token inside tight(), the first item's leading gap
  // belongs to the caller. All gaps between later items remain tight.
  struct Entry : rule<create<Document>(lit<"entry:"> + tight(nonemptyBlock) +
                                       lit<"]"> + lit<".">)> {};
  struct Optional : rule<create<Document>(lit<"maybe:"> + tight(optionalBlock) +
                                          lit<"]"> + lit<".">)> {};
  struct Repeated : rule<create<Document>(lit<"many:"> + tight(repeatedBlock) +
                                          lit<"]"> + lit<".">)> {};
  struct Root : rule<ref<Owned> | ref<Nonempty> | ref<Entry> | ref<Optional> |
                     ref<Repeated>> {};
  using entry = Root;
  using ignored = WS;
  using hidden = NOTE;
};
}  // namespace tregionconsumption

namespace tregionboundary {
// Prefixes select the two boundary suites in the same generated language.
struct Suite : Base {
  struct Root : rule<ref<Base::Root> | ref<tregionconsumption::Base::Root>> {};
  using entry = Root;
};
}  // namespace tregionboundary
