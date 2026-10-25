#pragma once

// A cell whose arms are told apart in the tree: seventeen alternatives, each
// naming the same token with an element of its own, a repetition between the
// token and the literal that decides. What the elected reading named is what
// the AST holds.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tnamedsplit {

namespace ast {
struct Item : cursiv::Extends<cursiv::AstNode> {};
struct A0 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A1 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A2 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A3 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A4 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A5 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A6 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A7 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A8 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A9 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A10 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A11 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A12 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A13 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A14 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A15 : cursiv::Extends<Item> {
  std::string_view name;
};
struct A16 : cursiv::Extends<Item> {
  std::string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  cursiv::vector<Item*> items;
};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct LINE : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct BLOCK : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<lit<"x">> {};
  struct C0 : rule<create<ast::A0>(assign<&ast::A0::name>(ref<ID>))> {};
  struct C1 : rule<create<ast::A1>(assign<&ast::A1::name>(ref<ID>))> {};
  struct C2 : rule<create<ast::A2>(assign<&ast::A2::name>(ref<ID>))> {};
  struct C3 : rule<create<ast::A3>(assign<&ast::A3::name>(ref<ID>))> {};
  struct C4 : rule<create<ast::A4>(assign<&ast::A4::name>(ref<ID>))> {};
  struct C5 : rule<create<ast::A5>(assign<&ast::A5::name>(ref<ID>))> {};
  struct C6 : rule<create<ast::A6>(assign<&ast::A6::name>(ref<ID>))> {};
  struct C7 : rule<create<ast::A7>(assign<&ast::A7::name>(ref<ID>))> {};
  struct C8 : rule<create<ast::A8>(assign<&ast::A8::name>(ref<ID>))> {};
  struct C9 : rule<create<ast::A9>(assign<&ast::A9::name>(ref<ID>))> {};
  struct C10 : rule<create<ast::A10>(assign<&ast::A10::name>(ref<ID>))> {};
  struct C11 : rule<create<ast::A11>(assign<&ast::A11::name>(ref<ID>))> {};
  struct C12 : rule<create<ast::A12>(assign<&ast::A12::name>(ref<ID>))> {};
  struct C13 : rule<create<ast::A13>(assign<&ast::A13::name>(ref<ID>))> {};
  struct C14 : rule<create<ast::A14>(assign<&ast::A14::name>(ref<ID>))> {};
  struct C15 : rule<create<ast::A15>(assign<&ast::A15::name>(ref<ID>))> {};
  struct C16 : rule<create<ast::A16>(assign<&ast::A16::name>(ref<ID>))> {};
  struct ItemRule : rule<(ref<C0> + many(lit<"a">) + lit<"end0">) |
                         (ref<C1> + many(lit<"a">) + lit<"end1">) |
                         (ref<C2> + many(lit<"a">) + lit<"end2">) |
                         (ref<C3> + many(lit<"a">) + lit<"end3">) |
                         (ref<C4> + many(lit<"a">) + lit<"end4">) |
                         (ref<C5> + many(lit<"a">) + lit<"end5">) |
                         (ref<C6> + many(lit<"a">) + lit<"end6">) |
                         (ref<C7> + many(lit<"a">) + lit<"end7">) |
                         (ref<C8> + many(lit<"a">) + lit<"end8">) |
                         (ref<C9> + many(lit<"a">) + lit<"end9">) |
                         (ref<C10> + many(lit<"a">) + lit<"end10">) |
                         (ref<C11> + many(lit<"a">) + lit<"end11">) |
                         (ref<C12> + many(lit<"a">) + lit<"end12">) |
                         (ref<C13> + many(lit<"a">) + lit<"end13">) |
                         (ref<C14> + many(lit<"a">) + lit<"end14">) |
                         (ref<C15> + many(lit<"a">) + lit<"end15">) |
                         (ref<C16> + many(lit<"a">) + lit<"end16">)> {};
  struct Root : rule<create<ast::Doc>(some(
                    append<&ast::Doc::items>(ref<ItemRule>), lit<";">))> {};
  using entry = Root;
  using ignore = WS;
  using hidden = list<BLOCK, LINE>;
};

}  // namespace tnamedsplit
