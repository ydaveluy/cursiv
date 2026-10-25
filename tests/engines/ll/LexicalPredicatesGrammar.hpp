#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tlexicalpredicates {

struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};

struct Tag {};

struct Base : cursiv::GrammarSpec {
  static constexpr auto word_boundary = uc::XID_Continue;
  struct WS : terminal<some(s)> {};
  struct Comment : terminal<through(lit<"/*">, lit<"*/">)> {};
  struct B : terminal<lit<"b">> {};
  struct QualifiedName : terminal<lit<"name">> {};
  struct STR : terminal<lit<"'"> + many(!cr<"'"> + any) + lit<"'">> {};
  struct Positive : terminal<lit<"a"> + &lit<"b"> + lit<"b">> {};
  struct Nested : terminal<lit<"a"> + !(lit<"b"> + !lit<"c">)+lit<"b">> {};
  struct Loose : terminal<lit<"a"> + !(lit<"b"> + !lit<"c">)+some(any)> {};
  struct FinalGuard : terminal<lit<"|", !(cr<"="> | eof)>> {};
  struct NestedGuard : terminal<lit<"a", !(lit<"b"> + !lit<"c">)> + lit<"b">> {
  };
  struct PositiveGuard : terminal<lit<"a", &(lit<"b"> + lit<"c">)> + lit<"b">> {
  };
  struct NamedGuard : terminal<lit<"a", !(ref<B> + !lit<"c">)> + ref<B>> {};
  struct Name
      : terminal<lit<"a"> + !(ref<QualifiedName> + !kw<"{">)+lit<"name{">> {};
  struct NameGuard
      : terminal<lit<"a", !(ref<QualifiedName> + !kw<"{">)> + lit<"name{">> {};
  struct Doubled : terminal<lit<"a"> + !!lit<"b"> + &(&lit<"b">)+lit<"b">> {};
  struct Negative : terminal<lit<"a"> + !&lit<"c"> + &!lit<"c"> + lit<"b">> {};
  struct End : terminal<lit<"a", &eof>> {};
  struct NotEnd : terminal<lit<"a", !(cr<"="> | eof)> + lit<"b">> {};
  struct StringGuard : terminal<lit<"a", !ref<STR>> + some(any)> {};
  struct Ambiguous
      : terminal<lit<"a"> + !((lit<"b"> | lit<"bc">)+!lit<"d">)+some(any)> {};
  struct Repeated
      : terminal<lit<"a"> + &(some(lit<"b">) + lit<"c">)+some(lit<"b">)> {};
  struct Nullable : terminal<lit<"a"> + &(many(option(lit<"b">)) +
                                          lit<"c">)+some(lit<"b">)> {};
  struct Loop : terminal<lit<"a"> + many(&lit<"b"> + lit<"b">) + &eof> {};
  struct EmptyLoop : terminal<lit<"a"> + many(&lit<"b">) + lit<"b">> {};
  struct Longest
      : terminal<lit<"a"> + &(lit<"bc"> | lit<"b">)+(lit<"b"> | lit<"bc">)> {};
  struct Keyword : terminal<lit<"a"> + &!kw<"AS"> + some(w)> {};
  struct UnicodeKeyword : terminal<lit<"a"> + &!kw<"AS"> + some(any)> {};
  struct KeywordAssert : terminal<lit<"a", &kw<"AS">> + some(any)> {};
  struct UnicodeGuard
      : terminal<lit<"a", &(cr<"αβ"> + !lit<"b">)> + some(any)> {};
  struct GuardedWord : terminal<lit<"a"> + !lit<"b", &lit<"c">> + some(any)> {};
  struct Prefix : terminal<&lit<"a"> + lit<"ab">> {};
  struct SimpleGuard : terminal<lit<"#", !lit<"bc">> + some(cr<"a-z">)> {};
  struct ComposedGuard
      : terminal<lit<"#", !(lit<"b"> + lit<"c">)> + some(cr<"a-z">)> {};
  struct EqualChoice
      : terminal<lit<"a", &((lit<"bx"> | lit<"bc">)+!lit<"d">)> + some(any)> {};
  struct EqualChoiceNot
      : terminal<lit<"a", !((lit<"bx"> | lit<"bc">)+!lit<"d">)> + some(any)> {};
  struct EmptyChoice
      : terminal<lit<"a", &((&lit<"b"> | !lit<"c">)+lit<"b">)> + some(any)> {};
  struct Exact
      : terminal<lit<"a", &(repeat<2, 2>(lit<"bc">) + !lit<"d">)> + some(any)> {
  };
  struct ExactChoice
      : terminal<lit<"a", &(repeat<2, 2>(lit<"b"> | lit<"c">) + !lit<"d">)> +
                 some(any)> {};
  struct ExactAssertions
      : terminal<lit<"a", &repeat<2, 2>(&lit<"b">)> + lit<"b">> {};
  struct Twin : terminal<lit<"a", &lit<"x">> | lit<"a", &lit<"y">>> {};
  struct Captured : terminal<lit<"<"> + mark<Tag>(some(cr<"a-z">)) + lit<"-"> +
                             same<Tag> + lit<">">> {};
  struct CapturedThrough
      : terminal<through(lit<"["> + mark<Tag>(many(cr<"=">)) + lit<"[">,
                         lit<"]"> + same<Tag> + lit<"]">)> {};
  struct EchoGuard : terminal<lit<"a", &ref<Captured>> + some(any)> {};
  struct EchoThroughGuard
      : terminal<lit<"a", &ref<CapturedThrough>> + some(any)> {};
  struct NestedComment
      : terminal<lit<"/*"> +
                 many(ref<NestedComment> | (!lit<"/*"> + !lit<"*/"> + any)) +
                 lit<"*/">> {};
  struct NestedThroughGuard
      : terminal<lit<"a", &ref<NestedComment>> + some(any)> {};
  static constexpr auto LongClose = lit<"bc"> | lit<"b", !lit<"c">>;
  struct LongGroup
      : terminal<lit<"<"> +
                 many(ref<LongGroup> | (!lit<"<"> + !LongClose + any)) +
                 LongClose> {};
  struct LongCloser
      : terminal<lit<"a", &(ref<LongGroup> + lit<"d">)> + some(any)> {};
  struct Through
      : terminal<lit<"a", &(through(lit<"'">, lit<"'">))> + some(any)> {};
  template <class T>
  static constexpr auto Read = assign<&Node::text>(ref<T>);
  struct Root
      : rule<create<Node>(((lit<"p:"> + Read<Positive>) |
                           (lit<"n:"> + Read<Nested> + lit<"c">) |
                           (lit<"g:"> + Read<NestedGuard> + lit<"c">) |
                           (lit<"loose:"> + Read<Loose>) |
                           (lit<"final:"> + Read<FinalGuard> + lit<"x">) |
                           (lit<"pg:"> + Read<PositiveGuard> + lit<"c">) |
                           (lit<"ng:"> + Read<NamedGuard> + lit<"c">) |
                           (lit<"name:"> + Read<Name>) |
                           (lit<"nameguard:"> + Read<NameGuard>) |
                           (lit<"double:"> + Read<Doubled>) |
                           (lit<"negative:"> + Read<Negative>) |
                           (lit<"end:"> + Read<End>) |
                           (lit<"notend:"> + Read<NotEnd>) |
                           (lit<"string:"> + Read<StringGuard>) |
                           (lit<"amb:"> + Read<Ambiguous>) |
                           (lit<"repeat:"> + Read<Repeated> + lit<"c">) |
                           (lit<"nullable:"> + Read<Nullable> + lit<"c">) |
                           (lit<"loop:"> + Read<Loop>) |
                           (lit<"emptyloop:"> + Read<EmptyLoop>) |
                           (lit<"longest:"> + Read<Longest>) |
                           (lit<"keyword:"> + Read<Keyword>) |
                           (lit<"unicodekw:"> + Read<UnicodeKeyword>) |
                           (lit<"kwassert:"> + Read<KeywordAssert>) |
                           (lit<"unicode:"> + Read<UnicodeGuard>) |
                           (lit<"guarded:"> + Read<GuardedWord>) |
                           (lit<"prefix:"> + Read<Prefix>) |
                           (lit<"echo:"> + Read<EchoGuard>) |
                           (lit<"echothrough:"> + Read<EchoThroughGuard>) |
                           (lit<"nestedthrough:"> + Read<NestedThroughGuard>) |
                           (lit<"longcloser:"> + Read<LongCloser>) |
                           (lit<"s:"> + Read<SimpleGuard>) |
                           (lit<"c:"> + Read<ComposedGuard>) |
                           (lit<"equal:"> + Read<EqualChoice>) |
                           (lit<"equal-not:"> + Read<EqualChoiceNot>) |
                           (lit<"empty-choice:"> + Read<EmptyChoice>) |
                           (lit<"exact:"> + Read<Exact>) |
                           (lit<"exact-choice:"> + Read<ExactChoice>) |
                           (lit<"exact-assertions:"> + Read<ExactAssertions>) |
                           (lit<"twin:"> + Read<Twin> + (lit<"x"> | lit<"y">)) |
                           (lit<"through:"> + Read<Through>)) +
                          eof)> {};
  using entry = Root;
  using ignore = WS;
  using hidden = Comment;
};
struct Ll : Base {};
struct Lr : Base {};

}  // namespace tlexicalpredicates
