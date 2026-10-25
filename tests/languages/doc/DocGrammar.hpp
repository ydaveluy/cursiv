#pragma once

// A nested document: objects, arrays and four scalars. The smallest grammar
// whose table still has the shape the fuzzer is written for -- recursion
// through two rules, several terminals, and goto edges to chase.

#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace fdoc {

namespace ast {
struct Value : cursiv::AstNode {};
struct Scalar : cursiv::Extends<Value> {
  std::string_view text;
};
struct Member : cursiv::AstNode {
  std::string_view name;
  Value* value = nullptr;
};
struct Object : cursiv::Extends<Value> {
  vector<Member*> members;
};
struct Array : cursiv::Extends<Value> {
  vector<Value*> items;
};
struct Doc : cursiv::AstNode {
  Value* value = nullptr;
};
}  // namespace ast

struct DocG : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t\n\r)">)> {};
  struct NUM : terminal<option(lit<"-">) + some(d) +
                        option(lit<"."> + some(d))> {};
  struct STR : terminal<lit<"\""> + many(cr<R"(^")">) + lit<"\"">> {};
  struct BOOL : terminal<kw<"true"> | kw<"false">> {};
  struct NULLK : terminal<kw<"null">> {};

  struct ObjectRule;
  struct ArrayRule;
  static constexpr auto scalar =
      create<ast::Scalar>(assign<&ast::Scalar::text>(
          ref<STR> | ref<NUM> | ref<BOOL> | ref<NULLK>));
  static constexpr auto value = ref<ObjectRule> | ref<ArrayRule> | scalar;

  struct MemberRule : rule<assign<&ast::Member::name>(ref<STR>) + lit<":"> +
                           assign<&ast::Member::value>(value)> {};
  struct ObjectRule
      : rule<lit<"{"> +
             many(append<&ast::Object::members>(ref<MemberRule>), lit<",">) +
             lit<"}">> {};
  struct ArrayRule : rule<lit<"["> +
                          many(append<&ast::Array::items>(value), lit<",">) +
                          lit<"]">> {};
  struct DocRule : rule<assign<&ast::Doc::value>(value)> {};
  using entry = DocRule;
  using ignore = WS;
};

}  // namespace fdoc
