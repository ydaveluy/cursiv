#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tregionboundary {
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
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
  struct AtLeastTwo : rule<create<Document>(
      lit<"two:"> + tight(lit<"["> + repeat<2, cursiv::kUnbounded>(item)) +
      lit<"]"> + lit<".">)> {};
  // The empty arm ends the region immediately after '['. The decision may
  // inspect ']' through caller trivia, but an elected X still reads tightly.
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
  struct Root : rule<ref<Required> | ref<Nonempty> | ref<AtLeastTwo> |
                     ref<Optional> | ref<Fixed> | ref<ClosingInside> |
                     ref<Called> | ref<CalledOptional> | ref<CalledEntry> |
                     ref<Same> | ref<SameHead> | ref<PatternHead> |
                     ref<ShortItem> | ref<LongItem>> {};
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
// Both boundary suites share one generated language per backend. Prefixes
// select their entries; each suite keeps its own AST types and assertions.
struct Suite : Base {
  struct Root : rule<ref<Base::Root> | ref<tregionconsumption::Base::Root>> {};
  using entry = Root;
};
struct Lr : Suite {};
struct Ll : Suite {};
}  // namespace tregionboundary
