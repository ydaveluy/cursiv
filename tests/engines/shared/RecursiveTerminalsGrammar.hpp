#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
namespace trecursive {

struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view text;
  vector<string_view> parts;
  Node* inner = nullptr;
};

struct Capture {};

struct InnerLanguage : cursiv::GrammarSpec {
  struct Group : terminal<lit<"("> + option(ref<Group>) + lit<")">> {};
  struct Root : rule<create<Node>(assign<&Node::text>(ref<Group>))> {};
  using entry = Root;
};

struct Base : cursiv::GrammarSpec {
  struct Comment
      : terminal<lit<"/*"> +
                 many(ref<Comment> | (!lit<"/*"> + !lit<"*/"> + any)) +
                 lit<"*/">> {};
  struct OpenComment
      : terminal<lit<"<*"> +
                 many(ref<OpenComment> | (!lit<"<*"> + !lit<"*>"> + any)) +
                 (lit<"*>"> | eof)> {};

  struct Square;
  static constexpr auto BracketText =
      !lit<"("> + !lit<")"> + !lit<"["> + !lit<"]"> + any;
  struct Round
      : terminal<lit<"("> + many(ref<Square> | BracketText) + lit<")">> {};
  struct Square
      : terminal<lit<"["> + many(ref<Round> | BracketText) + lit<"]">> {};

  struct Quoted : terminal<lit<"\""> + many((lit<"\\"> + any) | cr<R"(^"\\)">) +
                           lit<"\"">> {};
  struct Code : terminal<lit<"{"> +
                         many(ref<Code> | ref<Quoted> |
                              (!lit<"{"> + !lit<"}"> + !lit<"\""> + any)) +
                         lit<"}">> {};

  struct Atom : terminal<lit<"a"> + option(ref<Atom>) + option(lit<"b">)> {};
  struct Continued : terminal<ref<Atom> + lit<"b">> {};
  struct Run : terminal<lit<"n"> + option(ref<Run>)> {};
  struct Filled : terminal<option(ref<Run>) + lit<"q">> {};
  struct Positive : terminal<lit<"@", &ref<Comment>> + some(any)> {};
  struct Negative : terminal<lit<"~", !ref<Comment>> + some(any)> {};
  struct Asserted : terminal<&ref<Code> + ref<Code>> {};
  struct Refused : terminal<!ref<Code> + lit<"b">> {};
  struct Repeated : terminal<many(ref<Code>) + lit<"z">> {};
  struct Some : terminal<some(ref<Code>) + lit<"z">> {};
  struct Exact : terminal<repeat<2>(ref<Code>) + lit<"z">> {};
  struct Through : terminal<through(ref<Code>, lit<"!">)> {};
  struct Delimited
      : terminal<through(option(lit<"a">), lit<";">) + option(ref<Delimited>)> {
  };
  struct Marked : terminal<lit<"<"> + mark<Capture>(some(cr<"=">)) + lit<">"> +
                           ref<Code> + same<Capture> + lit<";">> {};
  struct Echo
      : terminal<through(lit<"<"> + mark<Capture>(some(cr<"-">)) + lit<">">,
                         lit<"]"> + same<Capture> + lit<";">) +
                 option(lit<"x"> + ref<Echo>)> {};
  struct PaddedEcho
      : terminal<through(lit<"<"> + mark<Capture>(some(cr<"-">)) + lit<">">,
                         lit<"]"> + many(cr<"-">) + same<Capture> + lit<";">) +
                 option(lit<"x"> + ref<PaddedEcho>)> {};
  struct EmptyPaddedEcho
      : terminal<through(lit<"<"> + mark<Capture>(many(cr<"=">)) + lit<">">,
                         lit<"]"> + many(cr<" ">) + same<Capture> + lit<" ">) +
                 option(lit<"x"> + ref<EmptyPaddedEcho>)> {};
  struct AlternateEcho
      : terminal<(through(lit<"<"> + mark<Capture>(some(cr<"a">)) + lit<":">,
                          lit<"]"> + many(cr<"b">) + same<Capture> + lit<";">) |
                  (through(
                      lit<"("> + mark<Capture>(some(cr<"b">)) + lit<":">,
                      lit<"]"> + many(cr<"a">) + same<Capture> + lit<";">))) +
                 option(lit<"x"> + ref<AlternateEcho>)> {};
  struct Unicode : terminal<lit<"λ"> + option(ref<Unicode>) + lit<"ω">> {};
  struct OnlyGuard : terminal<lit<"("> + option(ref<OnlyGuard>) + lit<")">> {};
  struct GuardOnly : terminal<lit<"#", &ref<OnlyGuard>> + some(any)> {};
  struct DataHead : terminal<lit<"b">> {};
  struct OnlyData : terminal<lit<"("> + option(ref<OnlyData>) + lit<")">> {};
  struct DataProbe : rule<ref<DataHead> + ref<OnlyData>> {};
  struct Bytes : rule<some(any)> {};

  struct SameDelimiter
      : terminal<lit<"<"> +
                 many(!lit<"<"> + (ref<SameDelimiter> | (!lit<"<"> + any))) +
                 lit<"<">> {};
  static constexpr auto LongClose = lit<"yy"> | lit<"y", !lit<"y">>;
  struct PrefixClose
      : terminal<lit<"x"> +
                 many(!LongClose + (ref<PrefixClose> | (!lit<"x"> + any))) +
                 LongClose> {};
  static constexpr auto RawOpen = lit<"a"> | lit<"ab">;
  static constexpr auto LongOpen = lit<"ab"> | lit<"a", !lit<"b">>;
  struct InnerGroup;
  static constexpr auto GroupItem =
      !lit<"b"> + (ref<InnerGroup> | (!RawOpen + any));
  struct InnerGroup : terminal<LongOpen + many(GroupItem) + lit<"b">> {};
  struct OuterGroup : terminal<RawOpen + many(GroupItem) + lit<"b">> {};

  static constexpr auto MultiOpen = lit<"{|"> | lit<"(|">;
  static constexpr auto MultiClose = lit<"|}"> | lit<"|)">;
  struct Multi : terminal<MultiOpen +
                          many(ref<Multi> | (!MultiOpen + !MultiClose + any)) +
                          MultiClose> {};

  template <class T>
  static constexpr auto Read = assign<&Node::text>(ref<T>);
  struct Root
      : rule<create<Node>(
            ((lit<"comment:"> + Read<Comment> + lit<"!">) |
             (lit<"pair:"> + append<&Node::parts>(ref<Comment>) + lit<"x"> +
              append<&Node::parts>(ref<Comment>)) |
             (lit<"open:"> + Read<OpenComment>) |
             (lit<"mutual:"> + Read<Round> + lit<"!">) |
             (lit<"code:"> + Read<Code> + lit<"!">) |
             (lit<"continuation:"> + Read<Continued> + lit<"!">) |
             (lit<"optional:"> + Read<Filled> + lit<"!">) |
             (lit<"asserted:"> + Read<Asserted> + lit<"!">) |
             (lit<"refused:"> + Read<Refused> + lit<"!">) |
             (lit<"many:"> + Read<Repeated> + lit<"!">) |
             (lit<"some:"> + Read<Some> + lit<"!">) |
             (lit<"exact:"> + Read<Exact> + lit<"!">) |
             (lit<"through:"> + Read<Through>) |
             (lit<"emptyopen:"> + Read<Delimited>) |
             (lit<"marked:"> + Read<Marked>) | (lit<"echo:"> + Read<Echo>) |
             (lit<"paddecho:"> + Read<PaddedEcho>) |
             (lit<"emptyecho:"> + Read<EmptyPaddedEcho>) |
             (lit<"altecho:"> + Read<AlternateEcho>) |
             (lit<"unicode:"> + Read<Unicode> + lit<"!">) |
             (lit<"guardonly:"> + Read<GuardOnly>) |
             (lit<"dataonly:"> + &ref<DataProbe> + Read<Bytes>) |
             (lit<"section:"> + assign<&Node::inner>(section<InnerLanguage>(
                                    lit<"<">, lit<"END">))) |
             (lit<"same:"> + Read<SameDelimiter> + lit<"!">) |
             (lit<"longclose:"> + Read<PrefixClose> + lit<"y!">) |
             (lit<"shortclose:"> + Read<PrefixClose> + lit<"yy!">) |
             (lit<"opener:"> + Read<OuterGroup> + lit<"!">) |
             (lit<"multi:"> + Read<Multi> + lit<"!">) |
             (lit<"positive:"> + Read<Positive>) |
             (lit<"negative:"> + Read<Negative>)) +
            eof)> {};
  using entry = Root;
};

}  // namespace trecursive

namespace trightlinear {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
struct Capture {};
struct Outer {};

struct Base : cursiv::GrammarSpec {
  struct Run : terminal<lit<"a"> + option(ref<Run>)> {};
  struct Sequence : terminal<lit<"a"> + lit<"b"> + option(ref<Sequence>)> {};
  struct Ambiguous : terminal<(lit<"a"> | lit<"ab">)+option(ref<Ambiguous>)> {};
  struct Alternative : terminal<lit<"c"> | (lit<"c"> + ref<Alternative>)> {};
  struct Reversed : terminal<(lit<"d"> + ref<Reversed>) | lit<"d">> {};
  struct SequenceAlternative
      : terminal<(lit<"a"> + lit<"b">) |
                 (lit<"a"> + lit<"b"> + ref<SequenceAlternative>)> {};
  struct Unicode : terminal<(lit<"λ"> | lit<"😀">)+option(ref<Unicode>)> {};
  struct Counted : terminal<repeat<1, 3>(lit<"e">) + option(ref<Counted>)> {};
  struct Variable
      : terminal<some(lit<"x">) + lit<"y"> + option(ref<Variable>)> {};
  struct BigCount : terminal<repeat<100000>(lit<"z">) + option(ref<BigCount>)> {
  };

  // A shorter endpoint of the callee is needed for the caller's suffix.
  struct Suffix : terminal<ref<Run> + lit<"a"> + lit<"!">> {};
  struct AmbiguousSuffix : terminal<ref<Ambiguous> + lit<"b"> + lit<"!">> {};
  struct Wrapped
      : terminal<mark<Outer>(repeat<1>(lit<"#">)) + ref<Run> + same<Outer>> {};

  // These bodies must keep their call semantics. Each invocation marks its
  // own run, and the call below it starts with none.
  struct Marked
      : terminal<mark<Capture>(repeat<1>(lit<"m">)) +
                 option(lit<"s"> + same<Capture>) + option(ref<Marked>)> {};
  struct Guarded : terminal<lit<"g", !lit<"x">> + option(ref<Guarded>)> {};
  struct EndGuarded : terminal<lit<"q", eof> + option(ref<EndGuarded>)> {};
  struct Nested : terminal<lit<"("> + option(ref<Nested>) + lit<")">> {};

  template <class T>
  static constexpr auto Read = assign<&Node::text>(ref<T>);
  struct Root : rule<create<Node>((lit<"run:"> + Read<Run>) |
                                  (lit<"sequence:"> + Read<Sequence>) |
                                  (lit<"ambiguous:"> + Read<Ambiguous>) |
                                  (lit<"alternative:"> + Read<Alternative>) |
                                  (lit<"reversed:"> + Read<Reversed>) |
                                  (lit<"seqalt:"> + Read<SequenceAlternative>) |
                                  (lit<"unicode:"> + Read<Unicode>) |
                                  (lit<"counted:"> + Read<Counted>) |
                                  (lit<"variable:"> + Read<Variable>) |
                                  (lit<"bigcount:"> + Read<BigCount>) |
                                  (lit<"suffix:"> + Read<Suffix>) |
                                  (lit<"ambsuffix:"> + Read<AmbiguousSuffix>) |
                                  (lit<"wrapped:"> + Read<Wrapped>) |
                                  (lit<"marked:"> + Read<Marked>) |
                                  (lit<"guarded:"> + Read<Guarded>) |
                                  (lit<"end:"> + Read<EndGuarded>) |
                                  (lit<"nested:"> + Read<Nested>))> {};
  using entry = Root;
};
struct Ll : Base {};
struct Lr : Base {};
}  // namespace trightlinear

namespace trecursive {
struct Suite : Base {
  struct Entry : rule<ref<Base::Root> | ref<InnerLanguage::Root> |
                      (lit<"rightlinear:"> + ref<trightlinear::Base::Root>)> {};
  using entry = Entry;
};
}  // namespace trecursive
