#pragma once

#include <string_view>

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "engines/shared/GuardedClimbAst.hpp"

// Calls, a guarded word infix or postfix, and the pattern boundaries that
// return from the climb. Keep the fixture limited to what its test reads.
namespace tguardedclimb {
struct Base : cursiv::GrammarSpec {
  static constexpr auto Symbol = !cr<R"(\x00-\x7f)"> + (uc::Sm | uc::So);
  static constexpr auto OpCharacter = cr<R"(!#%&*+:<>=?@^|~\/-)"> | Symbol;
  struct OperatorChar : terminal<!(lit<"//"> | lit<"/*">)+OpCharacter> {};
  static constexpr auto Opchar = ref<OperatorChar>;
  static constexpr auto Arrow = lit<"=>", !Opchar> | lit<"⇒">;
  static constexpr auto LeftArrow = lit<"<-", !Opchar> | lit<"←">;
  static constexpr auto word_boundary = cr<"a-zA-Z0-9_">;
  static constexpr auto Reserved =
      kw<"match"> | kw<"case"> | kw<"for"> | kw<"yield">;
  struct WS : terminal<some(s)> {};
  struct ID : terminal<!Reserved + some(cr<"a-z">)> {};
  struct NUMBER : terminal<some(d)> {};
  struct Open : terminal<as<[](std::string_view s) {
    return s.substr(0, s.size() - 1);
  }>(ref<ID> + lit<"\"">)> {};
  struct Ref : terminal<as<[](std::string_view s) {
    return s.substr(1);
  }>(lit<"$"> + some(cr<"a-z">))> {};
  struct Text : terminal<some(cr<R"(^"$)">)> {};
  struct ShortPart : rule<assign<&Part::name>(ref<Ref>)> {};
  struct LongPart : rule<lit<"${"> + assign<&Part::name>(ref<ID>) + lit<"}">> {};
  struct PatternRule
      : rule<create<Pattern>(tight(
            assign<&Pattern::prefix>(ref<Open>) +
            many(append<&Pattern::parts>(
                create<Part>(assign<&Part::text>(ref<Text>)) |
                ref<ShortPart> | ref<LongPart>)) + lit<"\"">))> {};
  struct PatternExpression
      : rule<ref<PatternRule> | create<Bind>(assign<&Bind::name>(ref<ID>))> {};

  struct Expression;
  struct Atom
      : rule<create<Ident>(assign<&Ident::name>(ref<ID>)) |
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
  struct Infix : infix<Calls, Binary, levels(left(ref<WordOp>))> {};
  static constexpr auto Boundary = StatementBreak | eof | &lit<"}"> |
                                   &lit<")"> | &lit<","> |
                                   &(kw<"match"> | kw<"yield">);
  static constexpr auto SignBoundary =
      Separator | !(lit<"+", !Opchar> | lit<"-", !Opchar> | lit<"~", !Opchar> |
                    lit<"!", !Opchar>);
  struct PostfixRule
      : rule<ref<Infix> +
             option(nest<Postfix, &Postfix::operand>(
                 !Separator + assign<&Postfix::name>(ref<ID>) + Boundary)) +
             SignBoundary> {};
  struct Expression
      : rule<create<For>(
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
  using ignored = WS;
};
struct Ll : Base {};
struct Lr : Base {};
}  // namespace tguardedclimb
