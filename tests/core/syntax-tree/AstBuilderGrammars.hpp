#pragma once

// The AST-builder scenarios share one generated grammar. The empty metadata
// case keeps its own grammar so its tables really contain no member columns.

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

// The member vocabulary over a three-level hierarchy.
namespace tab::hier {

struct Base : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Mid : cursiv::Extends<Base> {
  boolean flagged = false;
};
struct Leaf : cursiv::Extends<Mid> {
  int32 count = 0;
};
struct Holder : cursiv::Extends<cursiv::AstNode> {
  vector<Base*> items;
  optional<int32> level;
  variant<int64, string_view> tag;
  reference<Mid> target;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct INT : terminal<some(d)> {};
  // A converter makes the produced type exact, which is what lets a variant
  // member pick one alternative and only one.
  static constexpr auto num_conv = [](std::string_view t) {
    std::int64_t v = 0;
    for (char c : t) v = v * 10 + (c - '0');
    return v;
  };
  struct NUM : terminal<as<num_conv>(some(d))> {};
  // `create<Mid>` where the deduction would answer `Mid` anyway: `Mid` is a
  // strict base of what `LeafR` builds, and a type that is the base of another
  // rule's is one the inherited-member trap could have produced.
  struct MidR : rule<create<Mid> + kw<"mid"> + assign<&Base::name>(ref<ID>) +
                     option(flag<&Mid::flagged>(kw<"!">))> {};
  struct LeafR
      : rule<create<Leaf> + kw<"leaf"> + assign<&Base::name>(ref<ID>)> {};
  struct Root : rule<kw<"root"> + many(append<&Holder::items>(ref<MidR>)) +
                     many(append<&Holder::items>(ref<LeafR>)) +
                     option(assign<&Holder::level>(ref<INT>)) +
                     option(assign<&Holder::tag>(ref<NUM>)) +
                     option(assign<&Holder::target>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tab::hier

// Two climbs over one class.
namespace tab::climbs {

struct Two : cursiv::Extends<cursiv::AstNode> {
  Two* left = nullptr;
  string_view op;
  Two* right = nullptr;
  Two* l2 = nullptr;
  string_view op2;
  Two* r2 = nullptr;
  int32 value = 0;
};
struct Holder2 : cursiv::Extends<cursiv::AstNode> {
  Two* a = nullptr;
  Two* b = nullptr;
};

}  // namespace tab::climbs

// Regions and nesting.
namespace tab::region {

struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Wrap : cursiv::Extends<Node> {
  Node* inner = nullptr;
};

}  // namespace tab::region

// An enum member, written through its own converter, and after `refl` the
// same member with no converter at all.
namespace tab::enm {

enum class Vis { pub, priv };

struct Decl : cursiv::Extends<cursiv::AstNode> {
  Vis vis = Vis::pub;
  string_view name;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  static constexpr auto vis_conv = [](std::string_view t) {
    return t == "priv" ? Vis::priv : Vis::pub;
  };
  struct VIS : terminal<as<vis_conv>(some(cr<"a-z">))> {};
  struct Root
      : rule<(assign<&Decl::vis>(ref<VIS>) + assign<&Decl::name>(ref<ID>)) |
             (kw<"refl"> + assign<&Decl::vis>(ref<ID>))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tab::enm

// No member directive at all.
namespace tab::bare {

struct Node : cursiv::Extends<cursiv::AstNode> {};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Root : rule<create<Node> + kw<"x"> + ref<ID>> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tab::bare

// Assign, append and parent wiring.
namespace tab::wiring {

struct Node : cursiv::Extends<cursiv::AstNode> {};
struct Named : cursiv::Extends<Node> {
  string_view name;
};
struct Doc : cursiv::Extends<Node> {
  vector<Named*> items;
};

}  // namespace tab::wiring

// Optional, flag, converters, variants and the three reference storages.
namespace tab::vocab {

struct Target : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Rec : cursiv::Extends<cursiv::AstNode> {
  boolean flagged = false;
  optional<int32> level;
  int32 small = 0;
  variant<int64, string_view> tag;
  vector<Target*> targets;
  // The three storage forms of a reference, and the two ways a name is
  // produced: `ID` (a plain terminal) hands over a slice of the document,
  // `QNAME` a view of what its converter built in the tree's resource. The
  // writer is the same for both, and so are the handles it records.
  reference<Target> one;
  optional<reference<Target>> maybe;
  vector<reference<Target>> also;
};

}  // namespace tab::vocab

// A variant alternative that matches exactly.
namespace tab::exact {

struct Val : cursiv::Extends<cursiv::AstNode> {
  variant<boolean, float64, string_view> v;
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct BOOL : terminal<as<bool>(kw<"true"> | kw<"false">)> {};
  static constexpr auto num_conv = [](std::string_view t) {
    double v = 0;
    for (char c : t) v = v * 10 + (c - '0');
    return v;
  };
  struct NUM : terminal<as<num_conv>(some(d))> {};
  // The positioned result of the same value type: the deduction peels the
  // wrapper, so this rule's value type is `double` and the exactness round
  // sees `double` -- not `ConversionResult<double>`, which would match no
  // alternative at all.
  static constexpr auto pct_conv = [](std::string_view t) {
    double v = 0;
    for (char c : t)
      if (c != '%') v = v * 10 + (c - '0');
    return cursiv::ConversionResult<double>::ok(v / 100);
  };
  struct PCT : terminal<as<pct_conv>(some(d) + lit<"%">)> {};
  struct STR : terminal<some(cr<"a-z">)> {};
  // PCT before NUM: `42%` must not be read as `42` with a stray tail.
  struct Root : rule<assign<&Val::v>(ref<BOOL>) | assign<&Val::v>(ref<PCT>) |
                     assign<&Val::v>(ref<NUM>) | assign<&Val::v>(ref<STR>)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tab::exact

// An annotated terminal with no converter.
namespace tab::annot {

struct Val : cursiv::Extends<cursiv::AstNode> {
  variant<string_view, int64> v;  // slot 0 is the wrong landing for NUM
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  // A target type, no converter function.
  struct NUM : terminal<as<std::int64_t>(some(d))> {};
  struct WORD : terminal<some(cr<"a-z">)> {};
  struct Root : rule<assign<&Val::v>(ref<NUM>) | assign<&Val::v>(ref<WORD>)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace tab::annot

// The same law with no string_view alternative to fall into.
namespace tab::annot2 {

struct Num : cursiv::Extends<cursiv::AstNode> {
  variant<float64, int32> n;
};

}  // namespace tab::annot2

// A variant naming several node alternatives.
namespace tab::nodealt {

struct A : cursiv::Extends<cursiv::AstNode> {
  string_view a;
};
struct B : cursiv::Extends<cursiv::AstNode> {
  string_view b;
};
struct H : cursiv::Extends<cursiv::AstNode> {
  variant<std::monostate, A*, B*> v;
};

}  // namespace tab::nodealt

// A chain of nests over one node.
namespace tab::nested {

struct N : cursiv::Extends<cursiv::AstNode> {
  boolean pub = false;
  N* child = nullptr;
  string_view name;
};

}  // namespace tab::nested

// A nest whose wrapper is not the rule's declared type.
namespace tab::wrapper {

struct Inner : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  int64 weight = 0;
};
struct Wrap : cursiv::Extends<Inner> {
  Inner* child = nullptr;
  boolean flag = false;
};

}  // namespace tab::wrapper

// A parent adopting the result of a nesting rule.
namespace tab::adoptwrap {

struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<wrapper::Inner*> items;
};

}  // namespace tab::adoptwrap

namespace tab::move {

struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};

}  // namespace tab::move

// The climb's three direct children.
namespace tab::climb {

struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Num : cursiv::Extends<Expr> {
  int32 value = 0;
};
struct Bin : cursiv::Extends<Expr> {
  Expr* left = nullptr;
  string_view op;
  Expr* right = nullptr;
};

}  // namespace tab::climb

// A write hoisted out of a choice of creations.
namespace tab::hoist {

struct Base : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct L : cursiv::Extends<Base> {};
struct V : cursiv::Extends<Base> {};
struct Bag : cursiv::Extends<cursiv::AstNode> {
  vector<Base*> items;
};

}  // namespace tab::hoist

// A create<> region held by a directive one level up.
namespace tab::held {

struct Base : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct L : cursiv::Extends<Base> {};
struct V : cursiv::Extends<Base> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  Base* held = nullptr;
};

}  // namespace tab::held

// The spellings a bare literal writes into a decidable member.
namespace tab::spell {

enum class Mode { fast, slow };

struct Flag : cursiv::Extends<cursiv::AstNode> {
  boolean on = false;
  int32 count = 0;
  Mode mode = Mode::fast;
};

}  // namespace tab::spell

// The four shapes a text member admits, and the one it refuses.
namespace tab::textlaw {

struct N : cursiv::Extends<cursiv::AstNode> {
  string_view slice;
  string_view viewed;
  string_view annotated;
  string_view built;
};

}  // namespace tab::textlaw

// A conversion target of the grammar's own.
namespace tab::own {

struct Percent {
  int v = 0;
};

}  // namespace tab::own

template <>
struct cursiv::default_converter<tab::own::Percent> {
  static constexpr cursiv::ConversionResult<tab::own::Percent> convert(
      std::string_view t) {
    int n = 0;
    for (const char c : t) n = n * 10 + (c - '0');
    return n <= 100
               ? cursiv::ConversionResult<tab::own::Percent>::ok(
                     tab::own::Percent{n})
               : cursiv::ConversionResult<tab::own::Percent>::err("over 100");
  }
};

namespace tab::own {

struct Node : cursiv::Extends<cursiv::AstNode> {
  Percent share;
};

}  // namespace tab::own

// An optional node member.
namespace tab::optnode {

struct Item : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Bag : cursiv::Extends<cursiv::AstNode> {
  Item* head = nullptr;
  optional<Item*> tail;
};

}  // namespace tab::optnode

// A conversion carried by an inline reference.
namespace tab::inl {

struct Num : cursiv::Extends<cursiv::AstNode> {
  float64 value = 0;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Num*> nums;
};

constexpr double hexv(std::string_view t) {
  double v = 0;
  for (char c : t) v = v * 16 + (c <= '9' ? c - '0' : c - 'a' + 10);
  return v;
}

}  // namespace tab::inl

// A reference site narrowed below what its member accepts.
namespace tab::narrow {

struct Base2 : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};
struct Kind1 : cursiv::Extends<Base2> {};
struct Kind2 : cursiv::Extends<Base2> {};
struct Site : cursiv::Extends<cursiv::AstNode> {
  reference<Base2> broad;
};

}  // namespace tab::narrow

// Directives around a choice of creations.
namespace tab::deferred {

struct Pet : cursiv::Extends<cursiv::AstNode> {
  string_view name;
  vector<string_view> tags;
};
struct Dog : cursiv::Extends<Pet> {
  boolean good = false;
};
struct Cat : cursiv::Extends<Pet> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Pet*> pets;
};

}  // namespace tab::deferred

namespace tab {

// A body whose leading word is its own is offered as it stands; the others are
// entered through a `$name` word, which is where their documents begin.
struct All : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};

  struct ClimbsINT : terminal<some(d)> {};
  struct ClimbsPrim : rule<assign<&climbs::Two::value>(ref<ClimbsINT>)> {};
  struct ClimbsE1
      : infix<ClimbsPrim, climbs::Two, levels(left(kw<"*">), left(kw<"+">))> {};
  struct ClimbsE2
      : infix<ClimbsPrim, climbs::Two, levels(left(kw<"/">), left(kw<"@">)),
              &climbs::Two::l2, &climbs::Two::op2, &climbs::Two::r2> {};
  struct ClimbsRoot : rule<assign<&climbs::Holder2::a>(ref<ClimbsE1>) +
                           assign<&climbs::Holder2::b>(ref<ClimbsE2>)> {};

  struct RegionID : terminal<some(cr<"a-z">)> {};
  // A create at the root of a body is the rule's own creation, not a region
  // inside it: the rule's element carries it, at every level.
  struct RegionInner : rule<create<region::Node> +
                            assign<&region::Node::name>(ref<RegionID>)> {};
  // Declared `Node`, not `Wrap`: the head node comes from `RegionInner` and
  // the loop may not run at all, so what this rule hands back is a `Node` on
  // `"abc"` and a `Wrap` on `"abc *"`. Declaring `Wrap` is refused at compile
  // time by the creation validator, which names the reference at fault.
  struct RegionRoot
      : rule<ref<RegionInner> +
             many(nest<region::Wrap, &region::Wrap::inner>(kw<"*">))> {};

  struct WiringID : terminal<cr<"_a-zA-Z"> + many(w)> {};
  struct WiringItem
      : rule<kw<"item"> + assign<&wiring::Named::name>(ref<WiringID>)> {};
  struct WiringDoc
      : rule<kw<"doc"> + some(append<&wiring::Doc::items>(ref<WiringItem>))> {};

  struct VocabID : terminal<some(cr<"a-z">)> {};
  struct VocabINT : terminal<some(d)> {};
  // A user converter with the positioned error channel: a value out of the
  // rule's own range is a diagnostic, not a build failure.
  static constexpr auto vocab_small_conv = [](std::string_view t) {
    std::int32_t v = 0;
    for (char c : t) v = v * 10 + (c - '0');
    if (v > 999)
      return cursiv::ConversionResult<std::int32_t>::err(
          "small is at most 999");
    return cursiv::ConversionResult<std::int32_t>::ok(v);
  };
  struct VocabSMALL : terminal<as<vocab_small_conv>(some(d))> {};
  // The two branches of the tag write two different alternatives, so each
  // payload must name its value type exactly.
  static constexpr auto vocab_num_conv = [](std::string_view t) {
    std::int64_t v = 0;
    for (char c : t) v = v * 10 + (c - '0');
    return v;
  };
  struct VocabNUM : terminal<as<vocab_num_conv>(some(d))> {};
  struct VocabNAME : terminal<some(cr<"a-z">)> {};
  // A converter that materialises: uppercasing produces a name the document
  // does not spell, so it allocates in the resource the builder hands it and
  // answers a view of that -- which lives as long as the ParseResult.
  static constexpr auto vocab_qname_conv = [](std::string_view t,
                                              std::pmr::memory_resource* mem) {
    const std::span<char> room = cursiv::text_room(mem, t.size());
    for (std::size_t i = 0; i < t.size(); ++i)
      room[i] = static_cast<char>(t[i] - 'a' + 'A');
    return std::string_view(room.data(), room.size());
  };
  struct VocabQNAME : terminal<as<vocab_qname_conv>(some(cr<"a-z">))> {};
  struct VocabTgt
      : rule<kw<"target"> + assign<&vocab::Target::name>(ref<VocabID>)> {};
  struct VocabRoot
      : rule<many(append<&vocab::Rec::targets>(ref<VocabTgt>)) +
             option(flag<&vocab::Rec::flagged>(kw<"!">)) +
             option(kw<"level"> + assign<&vocab::Rec::level>(ref<VocabINT>)) +
             option(kw<"small"> + assign<&vocab::Rec::small>(ref<VocabSMALL>)) +
             option(kw<"tag"> + (assign<&vocab::Rec::tag>(ref<VocabNUM>) |
                                 assign<&vocab::Rec::tag>(ref<VocabNAME>))) +
             kw<"see"> + assign<&vocab::Rec::one>(ref<VocabID>) +
             option(kw<"maybe"> + assign<&vocab::Rec::maybe>(ref<VocabQNAME>)) +
             many(kw<"also"> + append<&vocab::Rec::also>(ref<VocabID>))> {};
  struct RebindRoot
      : rule<create<vocab::Rec> + assign<&vocab::Rec::one>(ref<VocabID>) +
             assign<&vocab::Rec::maybe>(ref<VocabID>) +
             append<&vocab::Rec::also>(ref<VocabID>) +
             append<&vocab::Rec::also>(ref<VocabID>)> {};

  struct Annot2INT : terminal<as<std::int32_t>(some(d))> {};
  struct Annot2Root : rule<assign<&annot2::Num::n>(ref<Annot2INT>)> {};

  struct NodealtID : terminal<some(cr<"a-z">)> {};
  struct NodealtAR : rule<lit<"@"> + assign<&nodealt::A::a>(ref<NodealtID>)> {};
  struct NodealtBR : rule<lit<"#"> + assign<&nodealt::B::b>(ref<NodealtID>)> {};
  struct NodealtRoot
      : rule<assign<&nodealt::H::v>(ref<NodealtAR> | ref<NodealtBR>)> {};

  struct NestIDT : terminal<some(cr<"a-z">)> {};
  struct NestQN : rule<some(ref<NestIDT>, lit<".">)> {};
  struct NestRoot : rule<option(flag<&nested::N::pub>(kw<"pub">)) +
                         assign<&nested::N::name>(ref<NestQN>) +
                         many(nest<nested::N, &nested::N::child>(kw<"!">))> {};

  // The rule builds an Inner, but a nest makes the result a Wrap: the
  // flag after it writes a member Wrap alone declares, and it is legal.
  struct WrapperID : terminal<some(cr<"a-z">)> {};
  struct WrapperRoot : rule<create<wrapper::Inner>(
                           assign<&wrapper::Inner::name>(ref<WrapperID>) +
                           nest<wrapper::Wrap, &wrapper::Wrap::child>(kw<"!">) +
                           option(flag<&wrapper::Wrap::flag>(kw<"?">)))> {
  };

  struct AdoptID : terminal<some(cr<"a-z">)> {};
  struct AdoptItem : rule<assign<&wrapper::Inner::name>(ref<AdoptID>) +
                          nest<wrapper::Wrap, &wrapper::Wrap::child>(kw<"!">)> {
  };
  struct AdoptDoc : rule<some(append<&adoptwrap::Doc::items>(ref<AdoptItem>))> {
  };

  struct MoveID : terminal<some(cr<"a-z">)> {};
  struct MoveRoot : rule<kw<"x"> + assign<&move::Node::name>(ref<MoveID>)> {};

  struct ClimbINT : terminal<some(d)> {};
  struct ClimbPrim : rule<assign<&climb::Num::value>(ref<ClimbINT>)> {};
  struct ClimbE
      : infix<ClimbPrim, climb::Bin, levels(left(kw<"*">), left(kw<"+">))> {};

  struct HoistID : terminal<some(cr<"a-z">)> {};
  struct HoistM : rule<create<hoist::V> + kw<"m">> {};
  // The write sits outside both regions, at the body root of a rule whose
  // declared type is the branches' base.
  struct HoistU
      : rule<create<hoist::Base> + assign<&hoist::Base::name>(ref<HoistID>) +
             (create<hoist::L>(lit<"[">) | create<hoist::V>(lit<"]">))> {};
  struct HoistRoot : rule<some(append<&hoist::Bag::items>(ref<HoistU>))> {};

  struct HeldId : terminal<some(cr<"a-z">)> {};
  struct HeldMid
      : rule<kw<"doc"> +
             (create<held::L>(kw<"let"> +
                              assign<&held::Base::name>(ref<HeldId>)) |
              create<held::V>(kw<"var"> +
                              assign<&held::Base::name>(ref<HeldId>))) +
             kw<"end">> {};
  struct HeldRoot : rule<assign<&held::Doc::held>(ref<HeldMid>)> {};

  struct WholeId : terminal<some(cr<"a-z">)> {};
  struct WholeMid
      : rule<create<held::L>(kw<"let"> +
                             assign<&held::Base::name>(ref<WholeId>)) |
             create<held::V>(kw<"var"> +
                             assign<&held::Base::name>(ref<WholeId>))> {};
  struct WholeRoot : rule<assign<&held::Doc::held>(ref<WholeMid>)> {};

  struct SpellR : rule<assign<&spell::Flag::on>(kw<"true"> | kw<"false">) +
                       assign<&spell::Flag::count>(kw<"42">) +
                       assign<&spell::Flag::mode>(kw<"fast"> | kw<"slow">)> {};

  // Trims the quotes: a slice into the input, which is what a view member
  // needs.
  static constexpr auto text_quoted_conv = [](std::string_view t) {
    return t.substr(1, t.size() - 2);
  };
  struct TextID : terminal<some(cr<"a-z">)> {};
  struct TextQUOTED
      : terminal<as<text_quoted_conv>(lit<"'"> + many(cr<"^'">) + lit<"'">)> {};
  // Materialises its answer, in the resource the builder hands it: lowercasing
  // spells bytes the document does not, and the view answered is a view of
  // the tree's own memory.
  static constexpr auto text_lower_conv = [](std::string_view t,
                                             std::pmr::memory_resource* mem) {
    const std::span<char> room = cursiv::text_room(mem, t.size());
    for (std::size_t i = 0; i < t.size(); ++i)
      room[i] = static_cast<char>(t[i] - 'A' + 'a');
    return std::string_view(room.data(), room.size());
  };
  struct TextLOWER : terminal<as<text_lower_conv>(some(cr<"A-Z">))> {};
  // Not in the body: the shape the law refuses, declared here so the refusal
  // is asserted beside the shapes it admits.
  static constexpr auto text_owns_conv = [](std::string_view t) {
    return std::string(t);
  };
  struct TextOWNS : terminal<as<text_owns_conv>(some(cr<"A-Z">))> {};
  // A target type, no converter function.
  struct TextNUM : terminal<as<std::int64_t>(some(d))> {};
  struct TextRoot : rule<assign<&textlaw::N::slice>(ref<TextID>) +
                         assign<&textlaw::N::viewed>(ref<TextQUOTED>) +
                         assign<&textlaw::N::annotated>(ref<TextNUM>) +
                         assign<&textlaw::N::built>(ref<TextLOWER>)> {};

  struct OwnPCT : terminal<as<own::Percent>(some(d))> {};
  struct OwnRoot
      : rule<create<own::Node>(assign<&own::Node::share>(ref<OwnPCT>))> {};

  struct OptID : terminal<some(cr<"a-z">)> {};
  struct OptIt
      : rule<create<optnode::Item>(assign<&optnode::Item::name>(ref<OptID>))> {
  };
  struct OptRoot : rule<create<optnode::Bag>(
                       assign<&optnode::Bag::head>(ref<OptIt>) +
                       option(assign<&optnode::Bag::tail>(ref<OptIt>)))> {};

  struct InlHEX : terminal<as<inl::hexv>(some(cr<"0-9a-f">))> {};
  struct InlN : rule<create<inl::Num> + assign<&inl::Num::value>(ref<InlHEX>)> {
  };
  struct InlRoot
      : rule<create<inl::Doc> + some(append<&inl::Doc::nums>(ref<InlN>))> {};

  struct NarrowID : terminal<some(cr<"a-z">)> {};
  struct NarrowRoot
      : rule<create<narrow::Site> + kw<"use"> +
             assign<cursiv::xref<&narrow::Site::broad, narrow::Kind1>>(
                 ref<NarrowID>)> {};

  struct DeferID : terminal<some(cr<"a-z">)> {};
  struct DeferPetR
      : rule<assign<&deferred::Pet::name>(ref<DeferID>) +
             many(kw<"#"> + append<&deferred::Pet::tags>(ref<DeferID>)) +
             (create<deferred::Dog>(
                  kw<"dog"> +
                  option(flag<&deferred::Dog::good>(kw<"!">))) |
              create<deferred::Cat>(kw<"cat">)) +
             many(kw<"@"> + append<&deferred::Pet::tags>(ref<DeferID>))> {};
  struct DeferRoot
      : rule<kw<"pets"> + many(append<&deferred::Doc::pets>(ref<DeferPetR>))> {
  };

  struct Entry
      : rule<ref<hier::G::Root> | ref<WiringDoc> | ref<VocabRoot> | ref<NodealtRoot> |
             ref<MoveRoot> | ref<HeldRoot> | ref<WholeRoot> | ref<SpellR> |
             ref<NarrowRoot> | ref<DeferRoot> |
             (kw<"$enum"> + ref<enm::G::Root>) |
             (kw<"$exact"> + ref<exact::G::Root>) |
             (kw<"$annot"> + ref<annot::G::Root>) |
             (kw<"$twoclimbs"> + ref<ClimbsRoot>) |
             (kw<"$region"> + ref<RegionRoot>) |
             (kw<"$annot2"> + ref<Annot2Root>) | (kw<"$nest"> + ref<NestRoot>) |
             (kw<"$wrapper"> + ref<WrapperRoot>) |
             (kw<"$adopt"> + ref<AdoptDoc>) | (kw<"$climb"> + ref<ClimbE>) |
             (kw<"$hoist"> + ref<HoistRoot>) | (kw<"$text"> + ref<TextRoot>) |
             (kw<"$own"> + ref<OwnRoot>) | (kw<"$rebind"> + ref<RebindRoot>) |
             (kw<"$opt"> + ref<OptRoot>) | (kw<"$inl"> + ref<InlRoot>)> {};
  using entry = Entry;
  using ignored = WS;
};

}  // namespace tab
