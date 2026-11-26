#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "LexicalBackendsAst.hpp"
#include "RecursiveTerminalsGrammar.hpp"
#include <charconv>
#include <limits>
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
  struct CountedChoice
      : terminal<lit<"a", &(repeat<2, 4>(lit<"bc"> | lit<"bd">) + lit<"bc!">)> +
                 some(any)> {};
  struct StoppedChoice
      : terminal<lit<"a", &(repeat<0, 4>(lit<"bc"> | lit<"bd">) + lit<"!">)> +
                 some(any)> {};
  struct Digits
      : terminal<lit<"n", &(some(d | lit<"_">) + lit<"!">)> + some(any)> {};
  struct ChainedDigits
      : terminal<!some(cr<"a-z">) + &(some(d) | lit<"Z">) +
                 !some(cr<"A-Z">) + some(d)> {};
  struct NullableGuard
      : terminal<lit<"n"> + !(some(d) + lit<"x">) + option(lit<"a">)> {};
  struct EmptyGuard : terminal<lit<"e"> + !some(d)> {};
  struct CyclicGuard
      : terminal<lit<"c"> + many(&lit<"a">) + lit<"a">> {};
  struct GuardedRaw : terminal<&some(lit<"#">) + SwiftRaw> {};
  // Words the automaton and the matcher read alike: the token after tells.
  struct TiedName : terminal<some(cr<"a-z">)> {};
  struct TiedTag : terminal<!(lit<"q"> + many(d) + !cr<"a-z">) + some(cr<"a-z">)> {};

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
                 (kw<"counted"> + append<&Document::items>(ref<CountedChoice>)) |
                 (kw<"stopped"> + append<&Document::items>(ref<StoppedChoice>)) |
                 (kw<"digits"> + append<&Document::items>(ref<Digits>)) |
                 (kw<"chained"> + append<&Document::items>(ref<ChainedDigits>)) |
                 (kw<"nullable-guard"> + append<&Document::items>(ref<NullableGuard>) + lit<"b">) |
                 (kw<"empty-guard"> + append<&Document::items>(ref<EmptyGuard>)) |
                 (kw<"cyclic-guard"> + append<&Document::items>(ref<CyclicGuard>)) |
                 (kw<"guarded-raw"> + append<&Document::items>(ref<GuardedRaw>)) |
                 (kw<"tied"> + many((append<&Document::items>(ref<TiedName>) + lit<"(">) |
                                    (append<&Document::items>(ref<TiedTag>) + lit<"=">))) |
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
namespace one {
struct Tag {};
}
namespace two {
struct Tag {};
}
template <int>
struct Slot {};

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
  // The mark may begin on a byte that is not yet a whole character.
  struct Echo : terminal<mark<A>(any) + same<A>> {};
  struct Line : terminal<through(mark<A>(some(cr<"^\n">)), lit<"\n"> + same<A>)> {};
  // Tags that share a short name are still two tags.
  struct Namesakes : terminal<mark<one::Tag>(any) + mark<two::Tag>(any) + same<one::Tag> +
                              same<two::Tag>> {};
  struct Slots : terminal<mark<Slot<1>>(any) + mark<Slot<2>>(any) + same<Slot<1>> +
                          same<Slot<2>>> {};

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
            (lit<"echo:"> + assign<&Node::text>(ref<Echo>)) |
            (lit<"line:"> + assign<&Node::text>(ref<Line>)) |
            (lit<"namesakes:"> + assign<&Node::text>(ref<Namesakes>)) |
            (lit<"slots:"> + assign<&Node::text>(ref<Slots>)) |
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
  struct Choices : terminal<through(lit<"[">, &lit<"END"> | lit<"END"> |
                                     lit<"STOP"> | &lit<"STOP"> | &lit<"tail">)> {};
  struct EndChoices
      : terminal<through(lit<"[">, lit<"END"> | lit<"STOP"> | eof)> {};
  struct Nullable : terminal<through(lit<"[">, option(lit<"]">))> {};
  struct EndOnly : terminal<through(lit<"[">, eof)> {};
  // Where a closer ends inside a longer one, the longest closes.
  struct Inside : terminal<through(lit<"[">, lit<"aab"> | lit<"a">)> {};

  static constexpr auto Open =
      lit<"<<<"> + mark<Label>(some(cr<"A-Z">)) + &lit<"\n">;
  static constexpr auto Close = lit<"\n"> + many(cr<" \t">) + same<Label> + !w;
  struct Heredoc : terminal<through(Open, Close)> {};
  struct ExplicitHeredoc : terminal<Open + many(!Close + any) + Close> {};
  struct Padding : terminal<through(Open, lit<"\n"> + repeat<0, 4>(cr<" \t">) +
                                              same<Label>)> {};
  struct SameOnly : terminal<through(mark<Equals>(some(cr<"=x">)) + lit<":">,
                                     same<Equals>)> {};
  struct EmptySame
      : terminal<through(mark<Label>(many(cr<"#">)) + lit<":">, same<Label>)> {
  };

  static constexpr auto ScalarOpen =
      lit<"<"> + mark<Equals>(some(lit<"=">)) + lit<">">;
  static constexpr auto ScalarClose = lit<"\xA9"> + same<Equals>;
  static constexpr auto ScalarPadClose =
      lit<"\xA9"> + many(cr<" ">) + same<Equals>;
  struct ScalarFast : terminal<through(ScalarOpen, ScalarClose)> {};
  struct ScalarExplicit
      : terminal<ScalarOpen + many(!ScalarClose + any) + ScalarClose> {};
  // A mark at the start of a token uses the general recognizer rather than
  // recovering its run from the opener's fence.
  struct ScalarRecognized
      : terminal<through(mark<Equals>(some(lit<"=">)) + lit<">">,
                         ScalarClose)> {};
  struct ScalarPadded : terminal<through(ScalarOpen, ScalarPadClose)> {};
  struct ScalarPaddedExplicit
      : terminal<ScalarOpen + many(!ScalarPadClose + any) + ScalarPadClose> {};
  struct ScalarPaddedRecognized
      : terminal<through(ScalarOpen,
                         lit<"\xA9"> + many(cr<" =">) + same<Equals>)> {};
  struct ScalarMark
      : terminal<through(lit<"<"> + mark<Label>(some(lit<"\xA9">)) + lit<">">,
                         same<Label> + lit<"!">)> {};
  // The scan begins after the opener, even when an exact byte literal ends
  // with a UTF-8 lead byte. Its following continuation byte starts a character.
  struct ScalarSplit
      : terminal<through(lit<"<"> + mark<Equals>(some(lit<"=">)) + lit<"\xC3">,
                         ScalarClose)> {};

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
            (lit<"end:"> + Read<EndOnly>) | (lit<"inside:"> + Read<Inside>) |
            (lit<"heredoc:"> + Read<Heredoc>) |
            (lit<"explicit:"> + Read<ExplicitHeredoc>) |
            (lit<"padding:"> + Read<Padding>) |
            (lit<"same:"> + Read<SameOnly>) |
            (lit<"empty:"> + Read<EmptySame>) |
            (lit<"char-fast:"> + Read<ScalarFast>) |
            (lit<"char-explicit:"> + Read<ScalarExplicit>) |
            (lit<"char-rec:"> + Read<ScalarRecognized>) |
            (lit<"char-pad:"> + Read<ScalarPadded>) |
            (lit<"char-pad-explicit:"> + Read<ScalarPaddedExplicit>) |
            (lit<"char-pad-rec:"> + Read<ScalarPaddedRecognized>) |
            (lit<"char-mark:"> + Read<ScalarMark>) |
            (lit<"char-split:"> + Read<ScalarSplit>) |
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
inline std::size_t read_payload(std::string_view input) {
  if (input.empty()) return 0;
  const char* end = input.data() + input.size();
  std::size_t count;
  const auto [payload, error] = std::from_chars(input.data(), end, count);
  if (error != std::errc{} || count > static_cast<std::size_t>(end - payload))
    return 0;
  return static_cast<std::size_t>(payload - input.data()) + count;
}

template<char Prefix>
inline std::size_t read_counted(std::string_view input) {
  if (!input.starts_with(Prefix)) return 0;
  const auto bytes = read_payload(input.substr(1));
  return bytes ? 1 + bytes : 0;
}

struct Scanners : cursiv::GrammarSpec {
  template<char C> struct Data : terminal<scan<read_counted<C>>> {};
  template<cursiv::str Prefix> struct Prefixed : terminal<lit<Prefix> + scan<read_payload>> {};
  struct Suffix : terminal<ref<Data<'D'>> + lit<";">> {};
  struct ID : terminal<some(cr<"A-Za-z0-9">)> {};
  struct Delimited : terminal<through(lit<"[">, scan<read_counted<'D'>>)> {};
  struct Run : terminal<some(scan<read_counted<'D'>>) + lit<";">> {};
  struct PrefixedRun : terminal<some(lit<"D0">) + scan<read_counted<'A'>>> {};
  struct OptionalRun : terminal<many(scan<read_counted<'D'>>) + lit<";">> {};
  struct Guarded : terminal<lit<"@", &scan<read_counted<'D'>>> + ref<ID>> {};
  struct Negative : terminal<!scan<read_counted<'D'>> + ref<ID>> {};
  static constexpr auto colon = [](std::string_view text) -> std::size_t { return text.starts_with(':'); };
  struct Tag : terminal<lit<"tag"> + &scan<colon>> {};
  struct OtherTag : terminal<lit<"tag"> + !scan<colon>> {};
  struct Mixed : terminal<ref<Tag> | (some(cr<"a-z">) + !scan<colon>)> {};
  struct Key : terminal<some(cr<"A-Za-z0-9">)> {};
  static constexpr auto oversize = [](std::string_view) { return std::numeric_limits<std::size_t>::max(); };
  struct Invalid : terminal<scan<oversize>> {};
  template<class T> static constexpr auto item = append<&Document::items>(ref<T>);
  template<class T, class U> static constexpr auto tagOrder =
      (ref<ID> | item<terminal<ref<T> | ref<U>>>) + (lit<":"> | lit<"!">);
  struct Plain : rule<lit<":">> {};
  struct Longer : rule<lit<":"> + &(lit<"ab"> + lit<"?">)> {};
  struct PlainComma : rule<lit<":"> + lit<",">> {};
  struct GuardedComma : rule<lit<":"> + &(lit<","> + lit<"ab"> + lit<"?">) + lit<",">> {};
  template<class Short, class Long> static constexpr auto choice = skip(ignore<Base::WS>, hide<>,
      (ref<Short> + lit<"a"> + lit<"b"> + lit<"!">) | (ref<Long> + lit<"ab"> + lit<"?">));
  struct Root : rule<create<Document>(
      (lit<"one:"> + some(item<Data<'D'>>)) |
      (lit<"bare:"> + some(append<&Document::items>(scan<read_counted<'D'>>))) |
      (lit<"adjacent:"> + skip(ignore<Base::WS>, hide<>, ref<ID> + item<Prefixed<"D">>)) |
      (lit<"alternate:"> + skip(ignore<Base::WS>, hide<>,
          ref<ID> + item<terminal<(lit<"D"> | lit<"~">) + scan<read_payload>>>)) |
      (lit<"choose:"> + choice<Plain, Longer>) | (lit<"comma:"> + choice<PlainComma, GuardedComma>) |
      (lit<"four:"> + some(item<Data<'D'>> | item<Data<'A'>> | item<Data<'B'>> | item<Data<'C'>>)) |
      (lit<"prefix:"> + some(item<Prefixed<"D">> | item<Prefixed<"A">> |
                             item<Prefixed<"B">> | item<Prefixed<"C">>)) |
      (lit<"probe:"> + &ref<terminal<scan<read_counted<'Z'>>>> + item<ID>) |
      (lit<"suffix:"> + item<Suffix>) |
      (lit<"retry:"> + some(append<&Document::items>(ref<Run> | ref<Data<'D'>>))) |
      (lit<"either:"> + some(item<terminal<ref<Run> | ref<Data<'D'>>>>)) |
      (lit<"loop:"> + some(item<terminal<ref<PrefixedRun> | ref<Data<'D'>>>>)) |
      (lit<"run:"> + item<Run>) | (lit<"optional:"> + item<OptionalRun>) |
      (lit<"positive:"> + item<Guarded>) |
      (lit<"negative:"> + item<Negative>) |
      (lit<"rank:"> + (ref<ID> | item<Tag>) + lit<":">) |
      (lit<"pair:"> + tagOrder<Tag, OtherTag>) | (lit<"reverse:"> + tagOrder<OtherTag, Tag>) |
      (lit<"mixed:"> + ((ref<ID> + (lit<":"> | lit<"!i">)) |
                        (item<Mixed> + (lit<":"> | lit<"!m">)))) |
      (lit<"mixedtie:"> + ((ref<Tag> + lit<":i">) | (item<Mixed> + lit<":m">))) |
      (lit<"keys:"> + ((ref<ID> + lit<";">) | (item<Key> + lit<":">))) |
      (lit<"invalid:"> + item<Invalid>) | (lit<"binary:"> + item<Data<'\0'>>) |
      (lit<"delimited:"> + item<Delimited>) |
      (lit<"tie:"> + ((item<Data<'D'>> + lit<"!">) | (item<ID> + lit<"?">))) |
      (lit<"held:"> + ((item<terminal<ref<Data<'D'>> + &lit<"!">>> + lit<"!!">) |
                        (item<terminal<ref<Data<'D'>> + !lit<"!">>> + lit<";">))) |
      (lit<"literal:"> + ((item<Data<'D'>> + lit<"!">) | (lit<"D0"> + lit<"?">))))> {};
};
// Literal modes, captures and rule tokens share a dispatch; trivia stays local.
struct Suite : cursiv::GrammarSpec {
  struct Entry : rule<skip(ignore<Base::WS>, hide<>, ref<Base::Root> + eof) |
                      (lit<"capture:"> +
                       (ref<tcaptures::Base::Root> | ref<tthrough::Base::Root>)) |
                      (lit<"rule:"> + skip(ignore<trulethrough::Base::WS>,
                          hide<>, ref<trulethrough::Base::Root> + eof)) |
                      (lit<"scan:"> + ref<Scanners::Root>) | ref<trecursive::Suite::Entry>> {};
  using entry = Entry;
};
}  // namespace tlexbackends
