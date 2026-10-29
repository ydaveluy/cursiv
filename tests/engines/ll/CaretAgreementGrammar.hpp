#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllcaretagree {

struct Target : cursiv::Extends<cursiv::AstNode> {};
struct First : cursiv::Extends<Target> {};
struct Second : cursiv::Extends<Target> {};
struct Alpha : cursiv::Extends<cursiv::AstNode> {
  string_view to;
};
struct Beta : cursiv::Extends<cursiv::AstNode> {
  string_view to;
};
struct Link : cursiv::Extends<cursiv::AstNode> {
  reference<Target> to, other;
};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Name : rule<ref<ID>> {};
  struct Owner
      : rule<create<Alpha>(kw<"use"> + assign<&Alpha::to>(ref<ID>) + lit<"+">) |
             create<Beta>(kw<"use"> + assign<&Beta::to>(ref<ID>) + lit<"-">)> {
  };
  struct IndirectOwner
      : rule<create<Alpha>(kw<"via"> + assign<&Alpha::to>(ref<Name>) +
                           lit<"+">) |
             create<Beta>(kw<"via"> + assign<&Beta::to>(ref<Name>) +
                          lit<"-">)> {};
  struct Narrow
      : rule<create<Link>(
            kw<"narrow"> +
            ((assign<cursiv::xref<&Link::to, First>>(ref<Name>) + lit<"+">) |
             (assign<cursiv::xref<&Link::to, Second>>(ref<Name>) +
              lit<"-">)))> {};
  struct Same : rule<create<Link>(
                    kw<"same"> + ((assign<&Link::to>(ref<Name>) + lit<"+">) |
                                  (assign<&Link::to>(ref<Name>) + lit<"-">)))> {
  };
  struct SameNarrow
      : rule<create<Link>(
            kw<"filtered"> +
            ((assign<cursiv::xref<&Link::to, First>>(ref<Name>) + lit<"+">) |
             (assign<cursiv::xref<&Link::to, First>>(ref<Name>) + lit<"-">)))> {
  };
  struct Mixed
      : rule<create<Link>(
            kw<"mixed"> +
            ((assign<&Link::to>(ref<Name>) + lit<"+">) |
             (assign<cursiv::xref<&Link::to, First>>(ref<Name>) + lit<"-">)))> {
  };
  struct Field
      : rule<create<Link>(kw<"field"> +
                          ((assign<&Link::to>(ref<Name>) + lit<"+">) |
                           (assign<&Link::other>(ref<Name>) + lit<"-">)))> {};
  struct Bare : rule<create<Link>(kw<"bare"> +
                                  ((assign<&Link::to>(ref<Name>) + lit<"+">) |
                                   (ref<Name> + lit<"-">)))> {};
  struct OnlyAlpha
      : rule<create<Alpha>(kw<"alpha"> + assign<&Alpha::to>(ref<ID>) +
                           lit<"+">)> {};
  struct OnlyBeta
      : rule<create<Beta>(kw<"beta"> + assign<&Beta::to>(ref<ID>) + lit<"-">)> {
  };
  struct Root : rule<ref<Owner> | ref<IndirectOwner> | ref<Narrow> | ref<Same> |
                     ref<SameNarrow> | ref<Mixed> | ref<Field> | ref<Bare> |
                     ref<OnlyAlpha> | ref<OnlyBeta>> {};
  using entry = Root;
  using ignored = WS;
};

struct Ll : Base {};
struct Lr : Base {};

}  // namespace tllcaretagree
