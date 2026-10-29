#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllprefix {

struct Expression : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expression> { string_view text; };
struct Unary : cursiv::Extends<Expression> {
  Expression* value{};
  bool enabled{};
};
struct Wrapped : cursiv::Extends<Expression> { Expression* value{}; };
struct Bag : cursiv::Extends<Expression> { vector<Expression*> values; };
struct LeftTag : cursiv::Extends<Expression> {
  Expression* operand{};
  bool enabled{};
};
struct RightTag : cursiv::Extends<Expression> {
  vector<Expression*> parts;
  bool enabled{};
};
struct Let : cursiv::Extends<Expression> {
  Expression* value{};
};
struct RC : cursiv::Extends<Expression> {
  bool y{};
};
struct NS : cursiv::Extends<Expression> {
  RC* value{};
  bool y{};
};
struct NSAppend : cursiv::Extends<Expression> {
  vector<RC*> values;
  bool y{};
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<Expression*> values;
};
struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  // RC := x y | x; RS := NS(RC y) | RC. The outer first arm may
  // need RC's shorter arm, while RC keeps its own written priority.
  struct RcRule
      : rule<create<RC>((lit<"x"> + flag<&RC::y>(lit<"y">)) | lit<"x">)> {
  };
  struct RsRule : rule<create<NS>(assign<&NS::value>(ref<RcRule>) +
                                  flag<&NS::y>(lit<"y">)) |
                       ref<RcRule>> {};
  struct DirectRc : rule<kw<"rc"> + ref<RcRule>> {};
  struct RsAppendRule
      : rule<create<NSAppend>(append<&NSAppend::values>(ref<RcRule>) +
                              flag<&NSAppend::y>(lit<"y">)) |
             ref<RcRule>> {};
  struct AppendRs : rule<kw<"append"> + ref<RsAppendRule>> {};
  struct RsReversedRule
      : rule<ref<RcRule> | create<NS>(assign<&NS::value>(ref<RcRule>) +
                                      flag<&NS::y>(lit<"y">))> {};
  struct ReversedRs : rule<kw<"reverse"> + ref<RsReversedRule> + lit<";">> {};
  struct Atom : rule<assign<&Name::text>(ref<ID>)> {};
  struct UnaryRule : rule<
      create<Unary>(assign<&Unary::value>(ref<Atom>) +
                    flag<&Unary::enabled>(lit<"!">)) | ref<Atom>> {};
  struct WrappedRule : rule<
      create<Wrapped>(assign<&Wrapped::value>(ref<UnaryRule>) + lit<"?">) |
      ref<UnaryRule>> {};
  struct BagRule : rule<
      create<Bag>(append<&Bag::values>(ref<WrappedRule>) + lit<"$">) |
      ref<WrappedRule>> {};
  struct TaggedRule : rule<
      create<LeftTag>(assign<&LeftTag::operand>(ref<BagRule>) +
                      flag<&LeftTag::enabled>(lit<"%">)) |
      create<RightTag>(append<&RightTag::parts>(ref<BagRule>) +
                       flag<&RightTag::enabled>(lit<"&">)) | ref<BagRule>> {};
  struct LetRule : rule<create<Let>(
                       kw<"let"> +
                       assign<&Let::value>(
                           create<Unary>(assign<&Unary::value>(ref<Atom>) +
                                         flag<&Unary::enabled>(lit<"!">)) |
                           ref<Atom>) +
                       lit<";">)> {};
  struct Root
      : rule<(some(append<&Document::values>(
                  ref<RsRule> | ref<DirectRc> | ref<AppendRs> |
                  ref<ReversedRs> | ref<LetRule> | ref<TaggedRule>)) |
              // Here the choice itself stands inside an append: what it
              // creates wraps the first node, and the append takes the wrapper.
              (lit<"["> +
               some(append<&Document::values>(
                   create<Unary>(assign<&Unary::value>(ref<Atom>) +
                                 flag<&Unary::enabled>(lit<"!">)) |
                   create<Wrapped>(assign<&Wrapped::value>(ref<Atom>) +
                                   lit<"?">) |
                   ref<Atom>)) + lit<"]">)) + eof> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tllprefix
