#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

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
struct Named : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  reference<Target> to;
  string_view text;
};
struct Tagged : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  reference<Target> other;
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
  // Rules sharing a prefix, then a member each behind data rules: one through a
  // list, and one with a string beside it.
  struct Word : rule<ref<ID> | kw<"word">> {};
  struct Words : rule<some(ref<Word>, lit<".">)> {};
  struct STR : terminal<lit<"'"> + many(cr<"a-z">) + lit<"'">> {};
  struct NamedR
      : rule<create<Named>(assign<&Named::name>(ref<Word>) +
                           option(lit<":"> + (assign<&Named::to>(ref<Words>) |
                                              assign<&Named::text>(ref<STR>))) +
                           option(lit<"{"> + lit<"}">))> {};
  struct TaggedR : rule<create<Tagged>(assign<&Tagged::name>(ref<Word>) +
                                       lit<":"> +
                                       assign<&Tagged::other>(ref<Word>) +
                                       lit<"<"> + lit<">">)> {};
  struct Shared : rule<kw<"shared"> + (ref<NamedR> | ref<TaggedR>)> {};
  struct Root : rule<ref<Owner> | ref<IndirectOwner> | ref<Narrow> | ref<Same> |
                     ref<SameNarrow> | ref<Mixed> | ref<Field> | ref<Bare> |
                     ref<OnlyAlpha> | ref<OnlyBeta> | ref<Shared>> {};
  using entry = Root;
  using ignored = WS;
};

struct Ll : Base {};
}  // namespace tllcaretagree
