#pragma once
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
namespace tllcounted {
struct Entry : cursiv::Extends<cursiv::AstNode> {};
struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Two : cursiv::Extends<Entry> {
  vector<Item*> items;
};
struct Three : cursiv::Extends<Entry> {
  vector<Item*> items;
};
struct Range : cursiv::Extends<Entry> {
  vector<Item*> items;
};
struct Around : cursiv::Extends<Entry> {
  vector<Item*> items;
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<Entry*> entries;
};
struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct A : terminal<lit<"a">> {};
  struct ItemRule : rule<create<Item>(assign<&Item::text>(ref<A>))> {};
  struct TwoRule
      : rule<create<Two>(repeat<2>(append<&Two::items>(ref<ItemRule>)))> {};
  struct ThreeRule
      : rule<create<Three>(repeat<3>(append<&Three::items>(ref<ItemRule>)))> {};
  struct CountChoice : rule<ref<TwoRule> | ref<ThreeRule>> {};
  struct Bounded : rule<create<Range>(
                       repeat<2, 4>(append<&Range::items>(ref<ItemRule>)))> {};
  struct OptionalCount : rule<create<Range>(repeat<0, 3>(
                             append<&Range::items>(ref<ItemRule>)))> {};
  struct Owed : rule<create<Range>(repeat<3, cursiv::kUnbounded>(
                    append<&Range::items>(ref<ItemRule>)))> {};
  struct Wide : rule<create<Range>(
                    repeat<0, 40>(append<&Range::items>(ref<ItemRule>)))> {};
  struct Spread : rule<create<Range>(
                      repeat<3, 30>(append<&Range::items>(ref<ItemRule>)))> {};
  struct Once
      : rule<create<Range>(repeat<1>(append<&Range::items>(ref<ItemRule>)))> {};
  struct AroundRequired
      : rule<create<Around>(append<&Around::items>(repeat<1>(ref<ItemRule>)))> {
  };
  struct AroundOptional : rule<create<Around>(append<&Around::items>(
                              repeat<0, 1>(ref<ItemRule>)))> {};
  struct Root
      : rule<create<Document>(
            many((lit<"c"> + append<&Document::entries>(ref<CountChoice>) +
                  lit<";">) |
                 (lit<"r"> + append<&Document::entries>(ref<Bounded>) +
                  lit<";">) |
                 (lit<"o"> + append<&Document::entries>(ref<OptionalCount>) +
                  lit<";">) |
                 (lit<"u"> + append<&Document::entries>(ref<Owed>) + lit<";">) |
                 (lit<"s"> + append<&Document::entries>(ref<Once>) + lit<";">) |
                 (lit<"x"> + append<&Document::entries>(ref<Wide>) + lit<";">) |
                 (lit<"y"> + append<&Document::entries>(ref<Spread>) +
                  lit<";">) |
                 (lit<"w"> + append<&Document::entries>(ref<AroundRequired>) +
                  lit<";">) |
                 (lit<"p"> + append<&Document::entries>(ref<AroundOptional>) +
                  lit<";">)) +
            eof)> {};
  using entry = Root;
  using ignored = WS;
};
struct Lr : Base {};
struct Long : Base {
  struct Past : rule<create<Range>(
                    repeat<0, 513>(append<&Range::items>(ref<ItemRule>)))> {};
  struct Floor : rule<create<Range>(
                     repeat<3, 513>(append<&Range::items>(ref<ItemRule>)))> {};
  struct Top
      : rule<create<Document>(
            many((lit<"x"> + append<&Document::entries>(ref<Past>) + lit<";">) |
                 (lit<"y"> + append<&Document::entries>(ref<Floor>) +
                  lit<";">)) +
            eof)> {};
  using entry = Top;
};
struct Combined : Base {
  struct Root : rule<ref<Base::Root> | (lit<"long:"> + ref<Long::Top>)> {};
  using entry = Root;
};
struct Ll : Combined {};
}  // namespace tllcounted
