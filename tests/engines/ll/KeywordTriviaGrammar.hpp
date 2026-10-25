// A keyword that opens a comment, and the same word as a member name: where
// the state takes the keyword it is read, and everywhere else the comment
// stands. `/` stays against `//` and `/* */` by longest match.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tllkwtrivia {
struct Part : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Value : cursiv::Extends<cursiv::AstNode> {};
struct Ref : cursiv::Extends<Value> {
  string_view text;
};
struct Quotient : cursiv::Extends<Value> {
  Value* left{};
  Value* right{};
};
struct Assign : cursiv::Extends<cursiv::AstNode> {
  string_view target;
  vector<Part*> parts;
  Value* value{};
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Assign*> statements;
};

struct Basic : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct REMARK
      : terminal<ikw<"REM"> + some(cr<R"( \t)">) + many(cr<R"(^\r\n)">)> {};
  struct LINE : terminal<lit<"//"> + many(cr<R"(^\n)">)> {};
  struct BLOCK : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct ID : terminal<!ikw<"REM"> + cr<"a-zA-Z_"> + many(w)> {};
  struct PartRule
      : rule<create<Part>(assign<&Part::text>(ref<ID> | ikw<"REM">))> {};
  // Member separators select plain, guarded and mixed keyword readings.
  struct KW_SPACE : terminal<ikw<"REM"> + &lit<" ">> {};
  struct KW_REFUSED : terminal<ikw<"REM"> + !lit<"(">> {};
  struct PART : terminal<(cr<"a-zA-Z_"> + many(w)) | ikw<"REM">> {};
  struct ConditionedPart
      : rule<create<Part>(assign<&Part::text>(ref<ID> | ref<KW_SPACE>))> {};
  struct RefusedPart
      : rule<create<Part>(assign<&Part::text>(ref<ID> | ref<KW_REFUSED>))> {};
  struct MixedPart
      : rule<create<Part>(assign<&Part::text>(ref<PART>))> {};
  struct RefRule : rule<create<Ref>(assign<&Ref::text>(ref<ID>))> {};
  struct QuotientRule
      : rule<create<Quotient>(assign<&Quotient::left>(ref<RefRule>) + lit<"/"> +
                              assign<&Quotient::right>(ref<RefRule>))> {};
  struct AssignRule
      : rule<create<Assign>(
            assign<&Assign::target>(ref<ID>) +
            many((lit<"."> + append<&Assign::parts>(ref<PartRule>)) |
                 (lit<"?"> + append<&Assign::parts>(ref<ConditionedPart>)) |
                 (lit<"!"> + append<&Assign::parts>(ref<RefusedPart>)) |
                 (lit<":"> + append<&Assign::parts>(ref<MixedPart>))) + lit<"="> +
            assign<&Assign::value>(ref<QuotientRule> | ref<RefRule>) +
            lit<";">)> {};
  struct Root
      : rule<create<Doc>(many(append<&Doc::statements>(ref<AssignRule>)))> {};
  using entry = Root;
  using ignore = WS;
  using hidden = list<REMARK, LINE, BLOCK>;
};
struct BasicLl : Basic {};
struct BasicLr : Basic {};

// The same keyword cut also applies to comments that recognize a recursive
// group or echo a capture behind the keyword.

struct Recognized : Basic {
  struct Group
      : terminal<lit<"("> + many(ref<Group> | cr<R"(^()\r\n)">) + lit<")">> {};
  struct REMARK : terminal<ikw<"REM"> + some(cr<R"( \t)">) +
                           option(ref<Group>) + many(cr<R"(^\r\n)">)> {};
  using hidden = list<REMARK, LINE, BLOCK>;
};
struct RecognizedLl : Recognized {};
struct RecognizedLr : Recognized {};

struct Run {};
struct Captured : Basic {
  struct REMARK
      : terminal<ikw<"REM"> + some(cr<R"( \t)">) + mark<Run>(many(lit<"x">)) +
                 same<Run> + many(cr<R"(^\r\n)">)> {};
  using hidden = list<REMARK, LINE, BLOCK>;
};
struct CapturedLl : Captured {};
struct CapturedLr : Captured {};

// A word boundary of code points: a no-break space ends the keyword, and the
// comment that would go on behind it does not take the keyword with it.
struct Wide : cursiv::GrammarSpec {
  static constexpr auto word_boundary = uc::XID_Continue;
  struct WS : terminal<some(uc::Zs | cr<R"(\t\r\n)">)> {};
  struct REMARK : terminal<ikw<"REM"> + many(cr<R"(^\r\n)">)> {};
  struct ID : terminal<!ikw<"REM"> + cr<"a-zA-Z_"> + many(w)> {};
  struct PartRule
      : rule<create<Part>(assign<&Part::text>(ref<ID> | ikw<"REM">))> {};
  struct RefRule : rule<create<Ref>(assign<&Ref::text>(ref<ID>))> {};
  struct AssignRule
      : rule<create<Assign>(
            assign<&Assign::target>(ref<ID>) +
            many(lit<"."> + append<&Assign::parts>(ref<PartRule>)) + lit<"="> +
            assign<&Assign::value>(ref<RefRule>) + lit<";">)> {};
  struct Root
      : rule<create<Doc>(many(append<&Doc::statements>(ref<AssignRule>)))> {};
  using entry = Root;
  using ignore = WS;
  using hidden = REMARK;
};
struct WideLl : Wide {};
struct WideLr : Wide {};

struct Parts : cursiv::Extends<cursiv::AstNode> {
  vector<Part*> parts;
};

// A comment that reads no more than a name yields to it: a bare `REM` is a
// name, `REM hello` a comment, and `REMx` a name.
struct Bare : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct NL : terminal<some(cr<R"(\r\n)">)> {};
  struct REMARK
      : terminal<ikw<"REM"> + option(lit<" "> + many(cr<R"(^\r\n)">))> {};
  struct ID : terminal<cr<"a-zA-Z"> + many(w)> {};
  struct Entry : rule<create<Part>(assign<&Part::text>(ref<ID> | ikw<"RESUME"> |
                                                       ikw<"REDIM">))> {};
  struct Root
      : rule<create<Parts>(many(append<&Parts::parts>(ref<Entry>) | ref<NL>))> {
  };
  using entry = Root;
  using ignore = WS;
  using hidden = REMARK;
};
struct BareLl : Bare {};
struct BareLr : Bare {};

}  // namespace tllkwtrivia
