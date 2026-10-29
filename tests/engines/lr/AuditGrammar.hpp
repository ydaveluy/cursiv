#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace taudit {
template <cursiv::str Name, std::size_t Limit = std::size_t(-1)>
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view text = "unchanged";
  bool flagged = false;
  Node* child = nullptr;
};
using Doc = Node<"value;\"<>9223372036854775808">;

inline unsigned conversions = 0;
struct Overloaded {
  constexpr std::string_view operator()(std::string_view) const { return "wrong"; }
  std::string_view operator()(std::string_view, std::pmr::memory_resource* mem) const {
    ++conversions;
    char* s = static_cast<char*>(mem->allocate(1, alignof(char)));
    *s = '0' + conversions;
    return {s, 1};
  }
};

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Hidden : terminal<lit<"~">> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct GuardA : terminal<lit<"a", !cr<"_">>> {};
  struct GuardB : terminal<lit<"a", !cr<";">>> {};
  struct GuardC : terminal<lit<"a">> {};
  struct Short : terminal<lit<"a">> {};
  struct Long : terminal<lit<"ab">> {};
  struct C : rule<lit<"c">> {};
  struct A : rule<ref<C>> {};
  struct D : rule<ref<C>> {};
  struct Unit : rule<(lit<"p"> + ref<A> + ref<Short> + lit<"b">) |
                     (lit<"q"> + ref<A> + ref<Long>) |
                     (lit<"p"> + ref<D> + lit<"z">) |
                     (lit<"q"> + ref<D> + lit<"z">)> {};
  struct Left : rule<lit<"c">> {};
  struct Right : rule<lit<"c">> {};
  struct Ordered : rule<(lit<"x"> + ref<Left> + lit<"a"> + eof) |
                        (lit<"x"> + ref<Right> + lit<"a">)> {};
  struct Empty : rule<option(lit<"e">)> {};
  struct Leaf : rule<create<Doc>(lit<"n">)> {};
  struct Tree : rule<create<Doc>(assign<&Doc::child>(ref<Leaf>))> {};
  struct Converted : terminal<as<Overloaded{}>(lit<"j">)> {};
  struct Joined : rule<ref<Converted> + ref<Converted>> {};
  struct Single : rule<ref<Converted> + option(lit<"!">)> {};
  struct Part : terminal<as<[](std::span<const std::string_view> parts) {
    ++conversions;
    return parts.size() == 1 && parts[0] == "part"
      ? std::string_view{"converted part"} : std::string_view{"wrong"};
  }>(lit<"part">)> {};
  struct PartJoined : rule<ref<Part> + option(lit<"!">)> {};
  struct Wrapped : rule<as<[](std::string_view s) {
    return s == "1" ? std::string_view{"wrapped"} : std::string_view{"wrong"};
  }>(ref<Converted> + option(lit<"!">))> {};
  struct Constant
      : terminal<as<[] { return std::string_view{"constant"}; }>(lit<"k">)> {};
  struct Constants : rule<ref<Constant> + ref<Constant>> {};
  struct Failed : terminal<as<[] {
    return cursiv::ConversionResult<std::string_view>::err("bad part");
  }>(lit<"bad">)> {};
  struct Failure : rule<ref<Failed> + ref<Failed>> {};
  struct SingleFailure : rule<ref<Failed> + option(lit<"!">)> {};
  struct Band : rule<lit<"("> + assign<&Doc::text>(ref<WS>) + lit<")">> {};
  struct Pad : rule<option(lit<"^">)> {};
  struct Owned : rule<ref<Pad> + lit<"ownedword">> {};
  struct Shared : rule<ref<Pad> + lit<"sharedword">> {};
  struct Slot : rule<ref<Pad> + assign<&Doc::text>(lit<"slotword">)> {};
  struct Root
      : rule<create<Doc>(
            (lit<"owned"> + assign<&Doc::text>(ref<Owned>)) |
            (lit<"named"> + assign<&Doc::text>(ref<Shared>)) |
            (lit<"bare"> + ref<Shared>) |
            (lit<"slot"> + assign<&Doc::child>(ref<Slot>)) |
            (lit<"reserve"> + !lit<"reserved", !cr<"a-z">> +
             !lit<"relaxed", !cr<"_">> +
             !(lit<"united", !cr<"_">> |
               lit<"united", !cr<";">>)+assign<&Doc::text>(ref<ID>) +
             option(lit<"_ok"> | lit<";ok">)) |
            (lit<"u"> + ref<Unit>) |
            (lit<"guards"> + ((ref<GuardA> + lit<"_x">) |
                              (ref<GuardB> + lit<"_y">) |
                              (ref<GuardC> + lit<"_z">))) |
            tight(lit<"["> + assign<&Doc::text>(ref<WS>) + lit<"]">) |
            tight(lit<"{"> + assign<&Doc::text>(ref<Hidden>) + lit<"}">) |
            tight(assign<&Doc::child>(ref<Band>)) |
            (lit<"r"> + ref<Ordered> + lit<"d">) |
            (lit<"f"> + eof + assign<&Doc::text>(ref<Empty>)) |
            (lit<"tree"> + assign<&Doc::child>(ref<Tree>)) |
            (lit<"join"> + assign<&Doc::text>(ref<Joined>)) |
            (lit<"single"> + assign<&Doc::text>(ref<Single>)) |
            (lit<"partdirect"> + assign<&Doc::text>(ref<Part>)) |
            (lit<"partjoined"> + assign<&Doc::text>(ref<PartJoined>)) |
            (lit<"wrapped"> + assign<&Doc::text>(ref<Wrapped>)) |
            (lit<"const"> + assign<&Doc::text>(ref<Constants>)) |
            (lit<"fail"> + assign<&Doc::text>(ref<Failure>)) |
            (lit<"singlefail"> + assign<&Doc::text>(ref<SingleFailure>)) |
            flag<&Doc::flagged>(ikw<"FLAG">.doc<"flag documentation">()) |
            lit<"a\0b">.doc<"binary literal">())> {};
  using entry = Root;
  using ignored = WS;
  using hidden = Hidden;
};
}  // namespace taudit
