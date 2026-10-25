#pragma once

// Two references that only a guard tells apart: the text behind the name
// decides which member it fills, before the name is read.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllcaretguard {

struct Target : cursiv::Extends<cursiv::AstNode> {};
struct First : cursiv::Extends<Target> {};
struct Second : cursiv::Extends<Target> {};
struct Link : cursiv::Extends<cursiv::AstNode> {
  reference<Target> to, other;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Name : rule<ref<ID>> {};
  struct Guarded
      : rule<create<Link>(
            kw<"link"> +
            ((&(ref<ID> + lit<"+">) + assign<&Link::to>(ref<Name>) +
              lit<"+">) |
             (&(ref<ID> + lit<"-">) + assign<&Link::other>(ref<Name>) +
              lit<"-">)))> {};
  struct Direct
      : rule<create<Link>(
            kw<"direct"> +
            ((&(ref<ID> + lit<"+">) + assign<&Link::to>(ref<ID>) + lit<"+">) |
             (&(ref<ID> + lit<"-">) + assign<&Link::other>(ref<ID>) +
              lit<"-">)))> {};
  struct Narrowed
      : rule<create<Link>(
            kw<"narrow"> +
            ((&(ref<ID> + lit<"+">) +
              assign<cursiv::xref<&Link::to, First>>(ref<Name>) + lit<"+">) |
             (&(ref<ID> + lit<"-">) +
              assign<cursiv::xref<&Link::other, Second>>(ref<Name>) +
              lit<"-">)))> {};
  struct Keyed
      : rule<create<Link>(
            (kw<"ka"> + assign<&Link::to>(ref<Name>) + lit<"+">) |
            (kw<"kb"> + assign<&Link::other>(ref<Name>) + lit<"-">))> {};
  struct Root : rule<ref<Guarded> | ref<Direct> | ref<Narrowed> | ref<Keyed>> {
  };
  using entry = Root;
  using ignore = WS;
};

struct Ll : Base {};
struct Lr : Base {};

}  // namespace tllcaretguard
