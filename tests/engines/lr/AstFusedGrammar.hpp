#pragma once

// A creation, its writes and its nests fused onto one record: the node made
// there is the one the nests wrap and the one the rule's close adopts.

#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "IdentityGrammar.hpp"
#include "MemConvGrammar.hpp"

namespace tfused {

inline unsigned conversions = 0;

namespace ast {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  string_view alias;
  Node* child = nullptr;
  vector<Node*> children;
  vector<string_view> texts;
  bool flagged = false;
};
struct Wrap : cursiv::Extends<Node> {
  Node* inner = nullptr;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  static constexpr auto counted_text = [](std::string_view text) {
    ++conversions;
    return text;
  };
  struct CountedID
      : terminal<some(cr<"a-z">), cursiv::text_conv<counted_text>> {};
  struct Text0 : rule<ref<CountedID>> {};
  struct Text1 : rule<ref<Text0>> {};
  struct Text2 : rule<ref<Text1>> {};
  // The creation and the nest on one record.
  struct Bang
      : rule<create<ast::Node> + nest<ast::Wrap, &ast::Wrap::inner>(kw<"!">)> {
  };
  // The creation on the leaf, the nest on a later record, the close after.
  struct NamedBang
      : rule<create<ast::Node> + assign<&ast::Node::name>(ref<ID>) +
             nest<ast::Wrap, &ast::Wrap::inner>(kw<"!">)> {};
  // The same with a run of nests that may be empty.
  struct NamedBangs
      : rule<create<ast::Node> + assign<&ast::Node::name>(ref<ID>) +
             many(nest<ast::Wrap, &ast::Wrap::inner>(kw<"!">))> {};
  // These inline creations have the same type as their parent. A reference
  // already keeps their writes apart; spelling its body here must do so too.
  struct InlineNode
      : rule<create<ast::Node>(assign<&ast::Node::child>(create<ast::Node>(
            assign<&ast::Node::child>(create<ast::Node>(
                assign<&ast::Node::name>(ref<ID>))))))> {};
  struct InlineText
      : rule<create<ast::Node>(assign<&ast::Node::child>(
            create<ast::Node>(assign<&ast::Node::alias>(ref<ID>))))> {};
  struct InlineChoice
      : rule<create<ast::Node>(assign<&ast::Node::child>(
            create<ast::Node>(assign<&ast::Node::name>(ref<ID>)) |
            create<ast::Node>(assign<&ast::Node::name>(lit<"?">))))> {};
  struct InlineMany
      : rule<create<ast::Node>(some(append<&ast::Node::children>(
            create<ast::Node>(assign<&ast::Node::name>(ref<ID>)))))> {};
  struct InlineOption
      : rule<create<ast::Node>(option(assign<&ast::Node::child>(
            create<ast::Node>(assign<&ast::Node::name>(ref<ID>)))))> {};
  struct InlineNullable
      : rule<create<ast::Node>(assign<&ast::Node::child>(create<ast::Node>(
            option(assign<&ast::Node::name>(ref<ID>)))))> {};
  struct InlineRegion
      : rule<create<ast::Node>(
            tight(assign<&ast::Node::child>(create<ast::Node>(
                assign<&ast::Node::name>(ref<ID>) + lit<":"> +
                assign<&ast::Node::alias>(ref<ID>)))) +
            lit<";">)> {};
  struct InlineNests
      : rule<create<ast::Node>(assign<&ast::Node::child>(create<ast::Node>(
            assign<&ast::Node::name>(ref<ID>) +
            many(nest<ast::Wrap, &ast::Wrap::inner>(lit<"!">)))))> {};
  struct Commit
      : rule<(kw<"COMMIT"> | flag<&ast::Node::flagged>(kw<"END">)) +
             option(kw<"TRANSACTION">)> {};
  struct CommitAlias : rule<ref<Commit>> {};
  struct CommitHolder
      : rule<create<ast::Node>(assign<&ast::Node::child>(ref<CommitAlias>))> {};
  struct Alias0 : rule<ref<NamedBangs>> {};
  struct Alias1 : rule<ref<Alias0>> {};
  struct Alias2 : rule<ref<Alias1>> {};
  struct InlineAlias
      : rule<create<ast::Node>(assign<&ast::Node::child>(ref<Alias2>))> {};
  struct AppendOnce
      : rule<create<ast::Node>(append<&ast::Node::texts>(ref<Text2>))> {};
  struct AppendSequence
      : rule<create<ast::Node>(append<&ast::Node::texts>(ref<Text2>) + lit<","> +
                              append<&ast::Node::texts>(ref<Text2>))> {};
  struct AppendMany
      : rule<create<ast::Node>(many(append<&ast::Node::texts>(ref<Text2>)))> {};
  struct AppendOption
      : rule<create<ast::Node>(option(append<&ast::Node::texts>(ref<Text2>)))> {};
  struct NodeCommitOption
      : rule<create<ast::Node>(option(commit(
            lit<"+">, assign<&ast::Node::child>(ref<Alias2>))))> {};
  struct Root
      : rule<(kw<"$a"> + ref<Bang>) | (kw<"$b"> + ref<NamedBang>) |
             (kw<"$e"> + ref<NamedBangs>) | (kw<"$n"> + ref<InlineNode>) |
             (kw<"$t"> + ref<InlineText>) | (kw<"$c"> + ref<InlineChoice>) |
             (kw<"$m"> + ref<InlineMany>) | (kw<"$o"> + ref<InlineOption>) |
             (kw<"$z"> + ref<InlineNullable>) | (kw<"$s"> + ref<InlineRegion>) |
             (kw<"$w"> + ref<InlineNests>) | (kw<"$l"> + ref<InlineAlias>) |
             (kw<"$k"> + ref<CommitHolder>) | (kw<"$p"> + ref<AppendOnce>) |
             (kw<"$v"> + ref<AppendSequence>) | (kw<"$r"> + ref<AppendMany>) |
             (kw<"$u"> + ref<AppendOption>) |
             (kw<"$h"> + ref<NodeCommitOption>)> {};
  using entry = Root;
  using ignore = WS;
};

struct Lr : Base {
  struct Entry : rule<ref<Base::Root> | ref<tmemconv::G::Root> |
                      ref<tident::G::S>> {};
  using entry = Entry;
};

}  // namespace tfused
