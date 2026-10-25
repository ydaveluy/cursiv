#pragma once
#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
namespace tguardedclimb {
struct Expr : cursiv::Extends<cursiv::AstNode> {};
struct Ident : cursiv::Extends<Expr> {
  string_view name;
};
struct Number : cursiv::Extends<Expr> {
  string_view text;
};
struct Binary : cursiv::Extends<Expr> {
  Expr* left;
  string_view op;
  Expr* right;
};
struct Call : cursiv::Extends<Expr> {
  Expr* callee;
  vector<Expr*> args;
};
struct Postfix : cursiv::Extends<Expr> {
  Expr* operand;
  string_view name;
};
struct Lambda : cursiv::Extends<Expr> {
  string_view name;
  string_view type;
  Expr* body;
};
struct Bind : cursiv::Extends<Expr> {
  string_view type;
  string_view name;
  Expr* inner;
};
struct Part : cursiv::Extends<cursiv::AstNode> {
  string_view text;
  string_view name;
};
struct Pattern : cursiv::Extends<Expr> {
  string_view prefix;
  vector<Part*> parts;
};
struct String : cursiv::Extends<Expr> {
  string_view prefix;
  vector<Part*> parts;
};
struct Match : cursiv::Extends<Expr> {
  Expr* operand;
  string_view variable;
  Expr* pattern;
  Expr* body;
};
struct Generator : cursiv::Extends<cursiv::AstNode> {
  Expr* pattern;
  Expr* source;
};
struct For : cursiv::Extends<Expr> {
  vector<Generator*> generators;
  Expr* body;
};
inline constexpr auto drop_first = [](std::string_view s) {
  return s.substr(1);
};
inline constexpr auto drop_last = [](std::string_view s) {
  return s.substr(0, s.size() - 1);
};
struct Base : cursiv::GrammarSpec {
  static constexpr auto Symbol = !cr<R"(\x00-\x7f)"> + (uc::Sm | uc::So);
  static constexpr auto OpCharacter = cr<R"(!#%&*+:<>=?@^|~\/-)"> | Symbol;
  struct OperatorChar : terminal<!(lit<"//"> | lit<"/*">)+OpCharacter> {};
  static constexpr auto Opchar = ref<OperatorChar>;
  static constexpr auto Op = some(Opchar);
  static constexpr auto ReservedOp =
      (lit<"="> | lit<"=>"> | lit<":"> | lit<"<-"> | lit<"<:"> | lit<"<%"> |
       lit<">:"> | lit<"#"> | lit<"@">)+!Opchar;
  static constexpr auto Arrow = lit<"=>", !Opchar> | lit<"⇒">;
  static constexpr auto LeftArrow = lit<"<-", !Opchar> | lit<"←">;
  static constexpr auto OpName = !(lit<"⇒"> | lit<"←">)+!ReservedOp + Op;
  static constexpr auto word_boundary = cr<"a-zA-Z0-9_">;
  static constexpr auto Reserved =
      kw<"match"> | kw<"case"> | kw<"for"> | kw<"yield">;
  struct WS : terminal<some(s)> {};
  struct ID : terminal<!Reserved + some(cr<"a-z">)> {};
  struct OP_ID : terminal<OpName> {};
  static constexpr auto Name = ref<ID> | ref<OP_ID>;
  struct NUMBER : terminal<some(d)> {};
  struct Open : terminal<ref<ID> + lit<"\"">, text_conv<drop_last>> {};
  struct MultiOpen
      : terminal<ref<ID> + lit<"\"\"\"">, text_conv<[](std::string_view v) {
                   return v.substr(0, v.size() - 3);
                 }>> {};
  struct MultiClose : terminal<repeat<3, cursiv::kUnbounded>(lit<"\"">),
                               text_conv<[](std::string_view v) {
                                 return v.substr(0, v.size() - 3);
                               }>> {};
  struct Dollar : terminal<lit<"$">> {};
  struct Quotes : terminal<lit<"\""> + option(lit<"\"">) + !lit<"\"">> {};
  struct Ref : terminal<lit<"$"> + some(cr<"a-z">), text_conv<drop_first>> {};
  struct Text : terminal<some(cr<R"(^"$)">)> {};
  struct ShortPart : rule<assign<&Part::name>(ref<Ref>)> {};
  struct LongPart : rule<lit<"${"> + assign<&Part::name>(ref<ID>) + lit<"}">> {
  };
  template <auto Prefix, auto Parts>
  static constexpr auto interpolation =
      tight((assign<Prefix>(ref<MultiOpen>) +
             many(append<Parts>(create<Part>(assign<&Part::text>(
                                    ref<Text> | ref<Quotes> | ref<Dollar>)) |
                                ref<ShortPart> | ref<LongPart>)) +
             append<Parts>(
                 create<Part>(assign<&Part::text>(ref<MultiClose>)))) |
                (assign<Prefix>(ref<Open>) +
                 many(append<Parts>(
                     create<Part>(
                         assign<&Part::text>(ref<Text> | ref<Dollar>)) |
                     ref<ShortPart> | ref<LongPart>)) +
                 lit<"\"">));
  struct PatternRule : rule<create<Pattern>(
                           interpolation<&Pattern::prefix, &Pattern::parts>)> {
  };
  struct StringRule
      : rule<create<String>(interpolation<&String::prefix, &String::parts>)> {};
  struct Expression;
  struct PatternExpression;
  struct Atom
      : rule<ref<StringRule> | create<Ident>(assign<&Ident::name>(ref<ID>)) |
             create<Number>(assign<&Number::text>(ref<NUMBER>)) |
             (lit<"("> + ref<Expression> + lit<")"> + !Arrow)> {};
  struct Calls
      : rule<ref<Atom> +
             many(nest<Call, &Call::callee>(
                 lit<"("> +
                 option(some(append<&Call::args>(ref<Expression>), lit<",">)) +
                 lit<")">))> {};
  static constexpr auto Separator = gap::has(eol | lit<";">);
  static constexpr auto StatementBreak =
      gap::has(lit<";"> | (eol + many(cr<R"( \t\r)">) + eol));
  struct WordOp : rule<!Separator + ref<ID> + !StatementBreak> {};
  struct Infix;
  static constexpr auto OpcharNoColon = !lit<":"> + Opchar;
  static constexpr auto LeftTail = option(many(Opchar) + OpcharNoColon);
  static constexpr auto RightTail = many(Opchar) + lit<":">;
  struct OpPipe : terminal<&OpName + cr<"|"> + LeftTail> {};
  struct OpPipeR : terminal<&OpName + cr<"|"> + RightTail> {};
  struct OpCaret : terminal<&OpName + cr<"^"> + LeftTail> {};
  struct OpCaretR : terminal<&OpName + cr<"^"> + RightTail> {};
  struct OpAmp : terminal<&OpName + cr<"&"> + LeftTail> {};
  struct OpAmpR : terminal<&OpName + cr<"&"> + RightTail> {};
  struct OpEqBang : terminal<&OpName + cr<"=!"> + LeftTail> {};
  struct OpEqBangR : terminal<&OpName + cr<"=!"> + RightTail> {};
  struct OpAngle : terminal<&OpName + cr<"<>"> + LeftTail> {};
  struct OpAngleR : terminal<&OpName + cr<"<>"> + RightTail> {};
  struct OpColon : terminal<&OpName + lit<":"> + LeftTail> {};
  struct OpColonR : terminal<&OpName + lit<":"> + RightTail> {};
  struct OpAddSub : terminal<&OpName + cr<"+-"> + LeftTail> {};
  struct OpAddSubR : terminal<&OpName + cr<"+-"> + RightTail> {};
  struct OpMulDiv : terminal<&OpName + cr<"*%/"> + LeftTail> {};
  struct OpMulDivR : terminal<&OpName + cr<"*%/"> + RightTail> {};
  struct OpOther : terminal<&OpName + (cr<R"(#?@~\\)"> | Symbol) + LeftTail> {};
  struct OpOtherR : terminal<&OpName + (cr<R"(#?@~\\)"> | Symbol) + RightTail> {
  };

  struct Infix
      : infix<Calls, &Binary::left, &Binary::op, &Binary::right,
              (left(ref<OpOther>) >> right(ref<OpOtherR>) >>
               left(ref<OpMulDiv>) >> right(ref<OpMulDivR>) >>
               left(ref<OpAddSub>) >> right(ref<OpAddSubR>) >>
               left(ref<OpColon>) >> right(ref<OpColonR>) >>
               left(ref<OpAngle>) >> right(ref<OpAngleR>) >>
               left(ref<OpEqBang>) >> right(ref<OpEqBangR>) >>
               left(ref<OpAmp>) >> right(ref<OpAmpR>) >> left(ref<OpCaret>) >>
               right(ref<OpCaretR>) >> left(ref<OpPipe>) >>
               right(ref<OpPipeR>) >> left(ref<WordOp>))> {};

  struct SimplePattern
      : rule<ref<PatternRule> | create<Ident>(assign<&Ident::name>(Name)) |
             create<Number>(assign<&Number::text>(ref<NUMBER>))> {};
  struct PatternInfix
      : infix<SimplePattern, &Binary::left, &Binary::op, &Binary::right,
              (left(ref<OpOther>) >> right(ref<OpOtherR>) >>
               left(ref<OpMulDiv>) >> right(ref<OpMulDivR>) >>
               left(ref<OpAddSub>) >> right(ref<OpAddSubR>) >>
               left(ref<OpColon>) >> right(ref<OpColonR>) >>
               left(ref<OpAngle>) >> right(ref<OpAngleR>) >>
               left(ref<OpEqBang>) >> right(ref<OpEqBangR>) >>
               left(ref<OpAmp>) >> right(ref<OpAmpR>) >> left(ref<OpCaret>) >>
               right(ref<OpCaretR>) >> left(ref<OpPipe>) >>
               right(ref<OpPipeR>) >> left(ref<WordOp>))> {};

  struct PatternExpression
      : rule<create<Bind>(assign<&Bind::name>(Name) + lit<":", !Opchar> +
                          assign<&Bind::type>(Name)) |
             create<Bind>(assign<&Bind::name>(Name) +
                          option(lit<"@", !Opchar> +
                                 assign<&Bind::inner>(ref<PatternInfix>))) |
             ref<PatternInfix>> {};
  static constexpr auto Boundary = StatementBreak | eof | &lit<"}"> |
                                   &lit<")"> | &lit<","> |
                                   &(kw<"match"> | kw<"yield">);
  static constexpr auto SignBoundary =
      Separator | !(lit<"+", !Opchar> | lit<"-", !Opchar> | lit<"~", !Opchar> |
                    lit<"!", !Opchar>);
  struct PostfixRule
      : rule<ref<Infix> +
             option(nest<Postfix, &Postfix::operand>(
                 !Separator + assign<&Postfix::name>(Name) + Boundary)) +
             SignBoundary> {};
  struct Expression
      : rule<create<Lambda>(lit<"("> +
                            option(assign<&Lambda::name>(Name) +
                                   option(lit<":", !Opchar> +
                                          assign<&Lambda::type>(Name))) +
                            lit<")"> + Arrow +
                            assign<&Lambda::body>(ref<Expression>)) |
             create<Lambda>(assign<&Lambda::name>(Name) + Arrow +
                            assign<&Lambda::body>(ref<Expression>)) |
             create<For>(
                 kw<"for"> + lit<"("> +
                 some(append<&For::generators>(create<Generator>(
                     assign<&Generator::pattern>(ref<PatternExpression>) +
                     LeftArrow +
                     assign<&Generator::source>(ref<Expression>)))) +
                 option(lit<",">) + lit<")"> + option(kw<"yield">) +
                 assign<&For::body>(ref<Expression>)) |
             (ref<PostfixRule> +
              option(nest<Match, &Match::operand>(
                  kw<"match"> + lit<"{"> + kw<"case"> +
                  assign<&Match::pattern>(ref<PatternExpression>) + Arrow +
                  assign<&Match::body>(ref<Expression>) + lit<"}">)))> {};
  using entry = Expression;
  using ignore = WS;
};
struct Ll : Base {};
struct Lr : Base {};
}  // namespace tguardedclimb
