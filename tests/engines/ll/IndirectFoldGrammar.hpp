#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllindirect {

struct Expression : cursiv::Extends<cursiv::AstNode> {};
struct Name : cursiv::Extends<Expression> { string_view text; };
struct Number : cursiv::Extends<Expression> { string_view text; };
struct Binary : cursiv::Extends<Expression> {
  Expression* left{};
  string_view op;
  Expression* right{};
};
struct Wrapped : cursiv::Extends<Expression> { Expression* value{}; };
struct Glob : cursiv::Extends<Expression> {
  Expression* value{};
  bool leading{};
  bool trailing{};
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<Expression*> values;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct NUMBER : terminal<some(d)> {};
  // The named infix terminal and anonymous glob terminal share a spelling.
  struct MULT : terminal<lit<"*">> {};
  struct NameRule : rule<assign<&Name::text>(ref<ID>)> {};
  struct NumberRule : rule<assign<&Number::text>(ref<NUMBER>)> {};
  struct Atom : rule<ref<NameRule> | ref<NumberRule>> {};
  struct Expr;
  struct GlobRule : rule<
      create<Glob>(flag<&Glob::leading>(lit<"*">) +
                   option(assign<&Glob::value>(ref<Expr>)) +
                   option(flag<&Glob::trailing>(lit<"*">))) |
      create<Glob>(assign<&Glob::value>(ref<Expr>) +
                   flag<&Glob::trailing>(lit<"*">))> {};
  struct Primary : rule<ref<Atom> | (lit<"("> + ref<Expr> + lit<")">) |
                        ref<GlobRule> |
                        create<Wrapped>(assign<&Wrapped::value>(ref<Expr>) +
                                        lit<"!">)> {};
  struct Expr
      : infix<Primary, Binary, levels(left(ref<MULT>), left(lit<"+">))> {};
  // A bare next expression remains valid: choosing glob '*' too early in
  // '1+2*3' therefore produces a clean but incorrect AST.
  struct Root : rule<some(append<&Document::values>(ref<Expr>)) + eof> {};
  using entry = Root;
  using ignored = WS;
};

struct ExposedPrimary : Base {
  struct Root : rule<append<&Document::values>(ref<Primary>) + eof> {};
  using entry = Root;
};

struct ExposedWrapper : Base {
  struct Root : rule<append<&Document::values>(ref<GlobRule>) + eof> {};
  using entry = Root;
};

template <class Public>
struct WithAliases : cursiv::extends<Public> {
  struct Alias2 : cursiv::Rule<cursiv::ref<typename Base::GlobRule>> {};
  struct Alias : cursiv::Rule<cursiv::ref<Alias2>> {};
  template <class>
  struct override;
  template <class R>
    requires std::is_same_v<R, Base::Primary>
  struct override<R>
      : cursiv::Rule<cursiv::ref<typename Base::Atom> |
                     (cursiv::lit<"("> + cursiv::ref<typename Base::Expr> +
                      cursiv::lit<")">) |
                     cursiv::ref<Alias> |
                     cursiv::create<Wrapped>(
                         cursiv::assign<&Wrapped::value>(
                             cursiv::ref<typename Base::Expr>) +
                         cursiv::lit<"!">)> {};
};
struct AliasedPrimaryLl : WithAliases<ExposedPrimary> {};
struct AliasedWrapperLl : WithAliases<ExposedWrapper> {};

struct Combined : WithAliases<Base> {
  struct Root : cursiv::Rule<cursiv::ref<Base::Root> |
      (cursiv::lit<"primary:"> + cursiv::ref<ExposedPrimary::Root>) |
      (cursiv::lit<"wrapper:"> + cursiv::ref<ExposedWrapper::Root>)> {};
  using entry = Root;
};
struct Ll : Combined {};
struct Lr : Combined {};

}  // namespace tllindirect
