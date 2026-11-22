#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace tasciiword {

struct Doc : cursiv::Extends<cursiv::AstNode> {};

struct Words : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT : terminal<lit<"#"> + many(cr<R"(^\r\n)">)> {};
  struct ID : terminal<(uc::L | cr<"_">) + many(uc::L | uc::Nd | cr<"_">)> {};
  struct LONG : terminal<some(cr<"a-z">) + lit<"é!">> {};
  struct TAG : terminal<lit<"@a"> | lit<"@b">> {};
  struct TAGNAME : terminal<lit<"@"> + many(cr<"A-Za-z">)> {};
  struct GUARDED : terminal<lit<"~"> + some(lit<"x">) + &lit<"xxxxxxxxxxxx">> {};
  struct LEFT : terminal<cr<"="> + &(lit<"12345"> + cr<"ab">)> {};
  struct RIGHT : terminal<cr<"="> + &(lit<"12345"> + cr<"bc">)> {};
  struct SHORT : terminal<cr<"="> + &lit<"1">> {};
  struct EQUALS : terminal<cr<"="> + option(ref<EQUALS>)> {};
  struct Root : rule<create<Doc>(many((ref<ID> | kw<"if"> | ikw<"while"> |
                                      ref<LONG> | ref<TAG> | ref<TAGNAME> | ref<GUARDED> |
                                      (lit<"%"> + (kw<"if"> | ref<ID>)) |
                                      (lit<"$"> + &kw<"if"> + ref<ID>) |
                                      (lit<":"> + ((!gap::none + lit<"-", !s> + lit<"a">) |
                                                    (lit<"-"> + lit<"b">) |
                                                    (lit<"-", &cr<"bc">> + lit<"c">))) |
                                      (ref<LEFT> + lit<"12345"> + ref<ID> + lit<"+">) |
                                      (ref<RIGHT> + lit<"12345"> + ref<ID> + lit<"-">) |
                                      (ref<SHORT> + lit<"12345"> + ref<ID> + lit<"?">) |
                                      (ref<EQUALS> + lit<"12345"> + ref<ID> + lit<"*">)) + lit<";">))> {};
  using entry = Root;
  using ignored = WS;
  using hidden = COMMENT;
};

struct Bytes : cursiv::GrammarSpec {
  struct RAW : terminal<some(byte)> {};
  struct Root : rule<create<Doc>(ref<RAW>)> {};
  using entry = Root;
};

}  // namespace tasciiword
