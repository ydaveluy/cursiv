#pragma once

// An operator rule that states a condition on the gap past its token reads
// that gap, not the one in front of the token.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace toperatorcondition {

struct Expression : cursiv::Extends<cursiv::AstNode> {};
struct Number : cursiv::Extends<Expression> {
  string_view text;
};
struct Binary : cursiv::Extends<Expression> {
  Expression* left{};
  string_view op;
  Expression* right{};
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  Expression* value{};
};
struct Unary : cursiv::Extends<Expression> {
  Expression* operand{};
};
struct Sequence : cursiv::Extends<cursiv::AstNode> {
  vector<Expression*> items;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUMBER : terminal<some(d)> {};
  struct PLUS : terminal<lit<"+">> {};
  struct Atom : rule<create<Number>(assign<&Number::text>(ref<NUMBER>))> {};
};

struct AfterBase : Base {
  struct Add : rule<ref<PLUS> + !gap::line> {};
  struct Sum : infix<Atom, Binary, left(ref<Add>)> {};
  struct Root
      : rule<create<Document>(assign<&Document::value>(ref<Sum>) + eof)> {};
  using entry = Root;
  using ignored = WS;
};

struct BothBase : Base {
  struct Add : rule<!gap::line + ref<PLUS> + gap::line> {};
  struct Sum : infix<Atom, Binary, left(ref<Add>)> {};
  struct Root
      : rule<create<Document>(assign<&Document::value>(ref<Sum>) + eof)> {};
  using entry = Root;
  using ignored = WS;
};

struct MixedBase : Base {
  struct TIMES : terminal<lit<"*">> {};
  struct Add : rule<ref<PLUS> + !gap::line> {};
  struct Sum : infix<Atom, Binary, levels(left(ref<TIMES>), left(ref<Add>))> {};
  struct Root
      : rule<create<Document>(assign<&Document::value>(ref<Sum>) + eof)> {};
  using entry = Root;
  using ignored = WS;
};

struct ListBase : Base {
  struct Sign
      : rule<create<Unary>(ref<PLUS> + assign<&Unary::operand>(ref<Atom>))> {};
  struct Operand : rule<ref<Sign> | ref<Atom>> {};
  struct Add : rule<ref<PLUS> + !gap::line> {};
  struct Sum : infix<Operand, Binary, left(ref<Add>)> {};
  struct Root
      : rule<create<Sequence>(many(append<&Sequence::items>(ref<Sum>)) + eof)> {
  };
  using entry = Root;
  using ignored = WS;
};

struct Suite : Base {
  struct Root : rule<(lit<"after:"> + ref<AfterBase::Root>) |
                     (lit<"both:"> + ref<BothBase::Root>) |
                     (lit<"mixed:"> + ref<MixedBase::Root>) |
                     (lit<"list:"> + ref<ListBase::Root>)> {};
  using entry = Root;
  using ignored = WS;
};
struct Ll : Suite {};
struct Lr : Suite {};

}  // namespace toperatorcondition
