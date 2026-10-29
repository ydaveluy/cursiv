#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "LexicalBackendsAst.hpp"
#include "RecursiveTerminalsGrammar.hpp"
#include "CharactersGrammar.hpp"
namespace tlexbackends {
struct Hashes {};
struct Delimiter {};
struct First {};
struct Second {};
struct Padding {};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  static constexpr auto SwiftRaw = through(
      mark<Hashes>(some(lit<"#">)) + lit<"\"">, lit<"\""> + same<Hashes>);
  struct Swift : terminal<SwiftRaw> {};
  struct Suffix : terminal<SwiftRaw + lit<"tail">> {};
  // A C++ d-char may be a quote: it need not break the R" prefix's final byte.
  struct Cpp
      : terminal<through(
            lit<"R\""> +
                mark<Delimiter>(repeat<0, 16>(cr<R"(^ ()\\\t\v\f\r\n)">)) +
                lit<"(">,
            lit<")"> + same<Delimiter> + lit<"\"">)> {};
  struct Multiple : terminal<mark<First>(some(lit<"#">)) + lit<":"> +
                             mark<Second>(some(cr<"a-z">)) + lit<"|"> +
                             same<First> + lit<"~"> + same<Second> + lit<"!">> {
  };
  struct Overlap : terminal<through(
                       lit<"<<"> + mark<Padding>(some(cr<"A-Z">)) + lit<"\n">,
                       lit<"\n"> + many(cr<" A">) + same<Padding> + lit<";">)> {
  };
  struct Empty
      : terminal<through(lit<"<<"> + mark<Padding>(many(cr<"A-Z">)) + lit<"\n">,
                         lit<"\n"> + many(cr<" ">) + same<Padding>)> {};
  struct After : terminal<through(
                     lit<"<<"> + mark<Padding>(many(cr<"A-Z">)) + lit<"\n">,
                     lit<"\n"> + many(cr<" ;">) + same<Padding> + lit<";">)> {};
  struct Fast
      : terminal<through(lit<"["> + mark<Delimiter>(many(cr<"=">)) + lit<"[">,
                         lit<"]"> + same<Delimiter> + lit<"]">)> {};
  struct Twin : terminal<lit<"<"> + mark<Delimiter>(some(cr<"a-z">)) +
                         lit<"-"> + same<Delimiter> + lit<">">> {};

  struct Root
      : rule<create<Document>(
                 (kw<"swift"> + append<&Document::items>(ref<Swift>)) |
                 (kw<"pair"> + append<&Document::items>(ref<Swift>) + lit<";"> +
                  append<&Document::items>(ref<Swift>)) |
                 (kw<"suffix"> + append<&Document::items>(ref<Suffix>)) |
                 (kw<"cpp"> + append<&Document::items>(ref<Cpp>)) |
                 (kw<"multiple"> + append<&Document::items>(ref<Multiple>)) |
                 (kw<"overlap"> + append<&Document::items>(ref<Overlap>)) |
                 (kw<"empty"> + append<&Document::items>(ref<Empty>)) |
                 (kw<"after"> + append<&Document::items>(ref<After>)) |
                 (kw<"fast"> + append<&Document::items>(ref<Fast>)) |
                 (kw<"twin"> + append<&Document::items>(ref<Twin>))) +
             eof> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tlexbackends

namespace tcaptures {

struct A {};
struct B {};
struct Hashes {};

struct Base : cursiv::GrammarSpec {
  struct Branch : terminal<((mark<A>(some(lit<"#">)) + lit<":">) |
                            (mark<A>(some(lit<"=">)) + lit<"~">)) +
                           same<A> + lit<"!">> {};
  struct Guard : terminal<mark<A>(some(lit<"#">)) + lit<"|"> + &same<A> +
                          same<A> + lit<"!">> {};
  struct Negative
      : terminal<mark<A>(some(lit<"#">)) + lit<"|"> +
                 !(same<A> + lit<"x">)+same<A> + option(lit<"x">) + lit<"!">> {
  };
  struct OwnGuard : terminal<&(mark<B>(some(lit<"=">)) + lit<":"> +
                               same<B>)+some(lit<"=">) +
                             lit<":"> + some(lit<"=">) + lit<"!">> {};
  struct GuardLocal
      : terminal<mark<A>(some(lit<"#">)) + lit<"|"> +
                 &(mark<B>(some(lit<"=">)) + lit<":"> + &same<B> + same<B> +
                   lit<":"> + &same<A> + same<A>)+some(lit<"=">) +
                 lit<":"> + some(lit<"=">) + lit<":"> + same<A> + lit<"!">> {};
  struct Shadow : terminal<mark<A>(some(lit<"#">)) + lit<"|"> +
                           &(mark<A>(some(lit<"=">)) + lit<":"> +
                             same<A>)+some(lit<"=">) +
                           lit<":"> + some(lit<"=">) + same<A> + lit<"!">> {};
  struct Redefined
      : terminal<mark<A>(repeat<1>(lit<"#">)) + mark<B>(repeat<1>(lit<"=">)) +
                 mark<A>(repeat<1>(lit<"@">)) + same<B> + same<A> + lit<"!">> {
  };

  static constexpr auto HelperBody = lit<"("> + mark<A>(some(lit<"=">)) +
                                     lit<":", &same<A>> + same<A> + lit<")">;
  struct Helper : terminal<HelperBody> {};
  struct RecursiveHelper
      : terminal<HelperBody + option(lit<"/"> + ref<RecursiveHelper>)> {};
  struct ViaHelper : terminal<mark<A>(some(lit<"#">)) + lit<"|"> + ref<Helper> +
                              lit<"|"> + same<A> + lit<"!">> {};
  struct ViaRecursiveHelper
      : terminal<mark<A>(some(lit<"#">)) + lit<"|"> + ref<RecursiveHelper> +
                 lit<"|"> + same<A> + lit<"!">> {};
  struct InlineHelper : terminal<mark<A>(some(lit<"#">)) + lit<"|"> +
                                 HelperBody + lit<"|"> + same<A> + lit<"!">> {};
  struct Nested
      : terminal<lit<"("> + mark<A>(some(lit<"=">)) + lit<":"> +
                 option(ref<Nested>) + lit<":"> + same<A> + lit<")">> {};
  struct ViaNested : terminal<mark<A>(some(lit<"#">)) + lit<"|"> + ref<Nested> +
                              lit<"|"> + same<A> + lit<"!">> {};

  struct Raw;
  static constexpr auto Close = lit<"\""> + same<Hashes>;
  static constexpr auto Escape =
      lit<"\\"> + same<Hashes> +
      ((lit<"("> + many(ref<Raw> | (!lit<"\""> + !lit<")"> + any)) + lit<")">) |
       (!lit<"("> + any));
  struct Raw
      : terminal<mark<Hashes>(many(lit<"#">)) + lit<"\""> +
                 many(Escape | (!Close + !(lit<"\\"> + same<Hashes>)+any)) +
                 Close> {};

  // Named only by a condition, inline, and kept a call by its captures.
  using Probe = terminal<lit<"<"> + mark<A>(some(lit<"=">)) + lit<":"> +
                         same<A> + lit<">">>;
  struct Angle : terminal<lit<"<"> + some(lit<"="> | lit<":">) + lit<">">> {};

  struct Root
      : rule<create<Node>(
            (lit<"branch:"> + assign<&Node::text>(ref<Branch>)) |
            (lit<"guard:"> + assign<&Node::text>(ref<Guard>)) |
            (lit<"negative:"> + assign<&Node::text>(ref<Negative>)) |
            (lit<"own:"> + assign<&Node::text>(ref<OwnGuard>)) |
            (lit<"local:"> + assign<&Node::text>(ref<GuardLocal>)) |
            (lit<"shadow:"> + assign<&Node::text>(ref<Shadow>)) |
            (lit<"redefined:"> + assign<&Node::text>(ref<Redefined>)) |
            (lit<"helper:"> + assign<&Node::text>(ref<ViaHelper>)) |
            (lit<"recursive:"> + assign<&Node::text>(ref<ViaRecursiveHelper>)) |
            (lit<"inline:"> + assign<&Node::text>(ref<InlineHelper>)) |
            (lit<"nested:"> + assign<&Node::text>(ref<ViaNested>)) |
            (lit<"probe:"> + &ref<Probe> + assign<&Node::text>(ref<Angle>)) |
            (lit<"raw:"> + assign<&Node::text>(ref<Raw>)))> {};
  using entry = Root;
};

}  // namespace tcaptures

namespace tthrough {
struct Label {};
struct Equals {};
struct Base : cursiv::GrammarSpec {
  struct Guarded : terminal<through(lit<"/*">, kw<"END">)> {};
  struct ManualComment
      : terminal<lit<"/*"> + many(!lit<"*/"> + any) + lit<"*/">> {};
  struct Folded : terminal<through(lit<"[">, ikw<"end">)> {};
  struct Choices : terminal<through(lit<"[">, lit<"END"> | lit<"STOP">)> {};
  struct EndChoices
      : terminal<through(lit<"[">, lit<"END"> | lit<"STOP"> | eof)> {};
  struct Nullable : terminal<through(lit<"[">, option(lit<"]">))> {};
  struct EndOnly : terminal<through(lit<"[">, eof)> {};

  static constexpr auto Open =
      lit<"<<<"> + mark<Label>(some(cr<"A-Z">)) + &lit<"\n">;
  static constexpr auto Close = lit<"\n"> + many(cr<" \t">) + same<Label> + !w;
  struct Heredoc : terminal<through(Open, Close)> {};
  struct ExplicitHeredoc : terminal<Open + many(!Close + any) + Close> {};
  struct Padding : terminal<through(Open, lit<"\n"> + repeat<0, 4>(cr<" \t">) +
                                              same<Label>)> {};
  struct SameOnly : terminal<through(mark<Equals>(some(cr<"=">)) + lit<":">,
                                     same<Equals>)> {};
  struct EmptySame
      : terminal<through(mark<Label>(many(cr<"#">)) + lit<":">, same<Label>)> {
  };

  static constexpr auto WritingOpen = mark<Label>(some(cr<"#">)) + lit<"[">;
  static constexpr auto WritingClose =
      (lit<"]"> + mark<Label>(some(cr<"=">)) + lit<":">) |
      (lit<"}"> + same<Label> + lit<":">);
  struct Writing : terminal<through(WritingOpen, WritingClose) + same<Label>> {
  };

  template <class T>
  static constexpr auto Read = assign<&Node::text>(ref<T>);
  struct Root
      : rule<create<Node>(
            (lit<"guarded:"> + Read<Guarded>) |
            (lit<"manual:"> + Read<ManualComment>) |
            (lit<"folded:"> + Read<Folded>) | (lit<"choice:"> + Read<Choices>) |
            (lit<"endchoice:"> + Read<EndChoices>) |
            (lit<"nullable:"> + Read<Nullable>) |
            (lit<"end:"> + Read<EndOnly>) | (lit<"heredoc:"> + Read<Heredoc>) |
            (lit<"explicit:"> + Read<ExplicitHeredoc>) |
            (lit<"padding:"> + Read<Padding>) |
            (lit<"same:"> + Read<SameOnly>) |
            (lit<"empty:"> + Read<EmptySame>) |
            (lit<"writing:"> + Read<Writing>) |
            (lit<"first:"> + Read<Choices> + lit<"tail">))> {};
  using entry = Root;
};
}  // namespace tthrough

// `through(open, close)` standing in a rule is one anonymous token: trivia
// stand between tokens and never inside it.

namespace trulethrough {

struct Equals {};

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<" \n">)> {};
  struct ID : terminal<cr<"a-z"> + many(cr<"a-z0-9">)> {};

  static constexpr auto Quote = through(lit<"'">, lit<"'">);
  static constexpr auto Angle = through(lit<"<">, lit<">">);

  struct Root : rule<create<Node>(
                    ((kw<"block"> +
                      assign<&Node::text>(through(lit<"/*">, lit<"*/">))) |
                     (kw<"open"> + assign<&Node::text>(
                                       through(lit<"/*">, lit<"*/"> | eof))) |
                     (kw<"first"> + assign<&Node::text>(Angle) + lit<"x">) |
                     (kw<"bracket"> +
                      assign<&Node::text>(through(
                          lit<"["> + mark<Equals>(many(lit<"=">)) + lit<"[">,
                          lit<"]"> + same<Equals> + lit<"]">))) |
                     (kw<"either"> + assign<&Node::text>(Quote | ref<ID>)) |
                     (kw<"many"> + many(append<&Node::parts>(Quote))) |
                     (kw<"peek"> + &Angle + assign<&Node::text>(Angle)) |
                     (kw<"unless"> + !through(lit<"a">, lit<"b">) +
                      assign<&Node::text>(ref<ID>))) +
                    eof)> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace trulethrough

namespace tlexbackends {
// Literal modes, captures and rule tokens share a dispatch; trivia stays local.
struct Suite : cursiv::GrammarSpec {
  struct Entry : rule<skip(ignore<Base::WS>, hide<>, ref<Base::Root> + eof) |
                      (lit<"capture:"> +
                       (ref<tcaptures::Base::Root> | ref<tthrough::Base::Root>)) |
                      (lit<"rule:"> + skip(ignore<trulethrough::Base::WS>,
                          hide<>, ref<trulethrough::Base::Root> + eof)) |
                      ref<trecursive::Suite::Entry>> {};
  using entry = Entry;
};
struct Ll : Suite {
  // These entries use no keywords, so they need no global Unicode boundary.
  struct Entry : rule<ref<Suite::Entry> |
                      (lit<"chars:"> + ref<tchars::Integration::Entry>)> {};
  using entry = Entry;
};
struct Lr : Suite {};
}  // namespace tlexbackends
