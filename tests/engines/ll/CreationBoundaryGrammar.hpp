#pragma once
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
namespace tllcreation {
struct Node;
struct Other : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  Node* child{};
};
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  Node* value{};
  vector<Node*> children;
  Other* other{};
};
struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  static constexpr auto child =
      create<Node> | create<Node>(assign<&Node::name>(ref<ID>));
  struct Root
      : rule<create<Node>(
                 assign<&Node::name>(lit<"root">) +
                 ((lit<"list["> +
                   some(append<&Node::children>(child), lit<",">) + lit<"]">) |
                  (lit<"single["> + assign<&Node::value>(child) + lit<"]">) |
                  (lit<"optional["> +
                   option(assign<&Node::value>(create<Node>(
                       lit<"!"> + option(assign<&Node::name>(ref<ID>))))) +
                   lit<"]">) |
                  (lit<"other["> +
                   assign<&Node::other>(
                       create<Other>(option(assign<&Other::name>(ref<ID>)) +
                                     lit<":"> + assign<&Other::child>(child))) +
                   lit<"]">))) +
             eof> {};
  using entry = Root;
  using ignored = WS;
};
// Inline anonymous leaves still belong to the surrounding parent.
struct Tree : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-zA-Z">)> {};
  struct Clade;
  static constexpr auto clade = create<Node> | ref<Clade>;
  struct Clade
      : rule<create<Node>((lit<"("> +
                           some(append<&Node::children>(clade), lit<",">) +
                           lit<")"> + option(assign<&Node::name>(ref<ID>))) |
                          assign<&Node::name>(ref<ID>))> {};
  struct Root : rule<clade + lit<";">> {};
  using entry = Root;
  using ignored = WS;
};
struct Combined : Base {
  struct Root : rule<ref<Base::Root> | (lit<"tree:"> + ref<Tree::Root>)> {};
  using entry = Root;
};
}  // namespace tllcreation
