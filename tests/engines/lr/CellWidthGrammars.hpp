#pragma once

// Cell and graph widths at the runtime boundaries, shared by the engine,
// ceiling and tape audits through one generated language.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "engines/lr/NamedSplitGrammar.hpp"
#include "engines/lr/NestedSplitGrammars.hpp"

// Three arms whose readings immediately meet a forty-arm cell: the pool
// passes its own ceiling before the text has said anything.
namespace tflood {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  cursiv::AstNode* outer = nullptr;
  cursiv::AstNode* inner = nullptr;
};
struct H0 : cursiv::Extends<cursiv::AstNode> {};
struct H1 : cursiv::Extends<cursiv::AstNode> {};
struct H2 : cursiv::Extends<cursiv::AstNode> {};
struct M0 : cursiv::Extends<cursiv::AstNode> {};
struct M1 : cursiv::Extends<cursiv::AstNode> {};
struct M2 : cursiv::Extends<cursiv::AstNode> {};
struct M3 : cursiv::Extends<cursiv::AstNode> {};
struct M4 : cursiv::Extends<cursiv::AstNode> {};
struct M5 : cursiv::Extends<cursiv::AstNode> {};
struct M6 : cursiv::Extends<cursiv::AstNode> {};
struct M7 : cursiv::Extends<cursiv::AstNode> {};
struct M8 : cursiv::Extends<cursiv::AstNode> {};
struct M9 : cursiv::Extends<cursiv::AstNode> {};
struct M10 : cursiv::Extends<cursiv::AstNode> {};
struct M11 : cursiv::Extends<cursiv::AstNode> {};
struct M12 : cursiv::Extends<cursiv::AstNode> {};
struct M13 : cursiv::Extends<cursiv::AstNode> {};
struct M14 : cursiv::Extends<cursiv::AstNode> {};
struct M15 : cursiv::Extends<cursiv::AstNode> {};
struct M16 : cursiv::Extends<cursiv::AstNode> {};
struct M17 : cursiv::Extends<cursiv::AstNode> {};
struct M18 : cursiv::Extends<cursiv::AstNode> {};
struct M19 : cursiv::Extends<cursiv::AstNode> {};
struct M20 : cursiv::Extends<cursiv::AstNode> {};
struct M21 : cursiv::Extends<cursiv::AstNode> {};
struct M22 : cursiv::Extends<cursiv::AstNode> {};
struct M23 : cursiv::Extends<cursiv::AstNode> {};
struct M24 : cursiv::Extends<cursiv::AstNode> {};
struct M25 : cursiv::Extends<cursiv::AstNode> {};
struct M26 : cursiv::Extends<cursiv::AstNode> {};
struct M27 : cursiv::Extends<cursiv::AstNode> {};
struct M28 : cursiv::Extends<cursiv::AstNode> {};
struct M29 : cursiv::Extends<cursiv::AstNode> {};
struct M30 : cursiv::Extends<cursiv::AstNode> {};
struct M31 : cursiv::Extends<cursiv::AstNode> {};
struct M32 : cursiv::Extends<cursiv::AstNode> {};
struct M33 : cursiv::Extends<cursiv::AstNode> {};
struct M34 : cursiv::Extends<cursiv::AstNode> {};
struct M35 : cursiv::Extends<cursiv::AstNode> {};
struct M36 : cursiv::Extends<cursiv::AstNode> {};
struct M37 : cursiv::Extends<cursiv::AstNode> {};
struct M38 : cursiv::Extends<cursiv::AstNode> {};
struct M39 : cursiv::Extends<cursiv::AstNode> {};
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct A0 : rule<create<H0>(lit<"x">)> {};
  struct A1 : rule<create<H1>(lit<"x">)> {};
  struct A2 : rule<create<H2>(lit<"x">)> {};
  struct C0 : rule<create<M0>(lit<"y">)> {};
  struct C1 : rule<create<M1>(lit<"y">)> {};
  struct C2 : rule<create<M2>(lit<"y">)> {};
  struct C3 : rule<create<M3>(lit<"y">)> {};
  struct C4 : rule<create<M4>(lit<"y">)> {};
  struct C5 : rule<create<M5>(lit<"y">)> {};
  struct C6 : rule<create<M6>(lit<"y">)> {};
  struct C7 : rule<create<M7>(lit<"y">)> {};
  struct C8 : rule<create<M8>(lit<"y">)> {};
  struct C9 : rule<create<M9>(lit<"y">)> {};
  struct C10 : rule<create<M10>(lit<"y">)> {};
  struct C11 : rule<create<M11>(lit<"y">)> {};
  struct C12 : rule<create<M12>(lit<"y">)> {};
  struct C13 : rule<create<M13>(lit<"y">)> {};
  struct C14 : rule<create<M14>(lit<"y">)> {};
  struct C15 : rule<create<M15>(lit<"y">)> {};
  struct C16 : rule<create<M16>(lit<"y">)> {};
  struct C17 : rule<create<M17>(lit<"y">)> {};
  struct C18 : rule<create<M18>(lit<"y">)> {};
  struct C19 : rule<create<M19>(lit<"y">)> {};
  struct C20 : rule<create<M20>(lit<"y">)> {};
  struct C21 : rule<create<M21>(lit<"y">)> {};
  struct C22 : rule<create<M22>(lit<"y">)> {};
  struct C23 : rule<create<M23>(lit<"y">)> {};
  struct C24 : rule<create<M24>(lit<"y">)> {};
  struct C25 : rule<create<M25>(lit<"y">)> {};
  struct C26 : rule<create<M26>(lit<"y">)> {};
  struct C27 : rule<create<M27>(lit<"y">)> {};
  struct C28 : rule<create<M28>(lit<"y">)> {};
  struct C29 : rule<create<M29>(lit<"y">)> {};
  struct C30 : rule<create<M30>(lit<"y">)> {};
  struct C31 : rule<create<M31>(lit<"y">)> {};
  struct C32 : rule<create<M32>(lit<"y">)> {};
  struct C33 : rule<create<M33>(lit<"y">)> {};
  struct C34 : rule<create<M34>(lit<"y">)> {};
  struct C35 : rule<create<M35>(lit<"y">)> {};
  struct C36 : rule<create<M36>(lit<"y">)> {};
  struct C37 : rule<create<M37>(lit<"y">)> {};
  struct C38 : rule<create<M38>(lit<"y">)> {};
  struct C39 : rule<create<M39>(lit<"y">)> {};
  struct Item : rule<(ref<C0> + many(lit<"a">) + lit<"e0">) |
                     (ref<C1> + many(lit<"a">) + lit<"e1">) |
                     (ref<C2> + many(lit<"a">) + lit<"e2">) |
                     (ref<C3> + many(lit<"a">) + lit<"e3">) |
                     (ref<C4> + many(lit<"a">) + lit<"e4">) |
                     (ref<C5> + many(lit<"a">) + lit<"e5">) |
                     (ref<C6> + many(lit<"a">) + lit<"e6">) |
                     (ref<C7> + many(lit<"a">) + lit<"e7">) |
                     (ref<C8> + many(lit<"a">) + lit<"e8">) |
                     (ref<C9> + many(lit<"a">) + lit<"e9">) |
                     (ref<C10> + many(lit<"a">) + lit<"e10">) |
                     (ref<C11> + many(lit<"a">) + lit<"e11">) |
                     (ref<C12> + many(lit<"a">) + lit<"e12">) |
                     (ref<C13> + many(lit<"a">) + lit<"e13">) |
                     (ref<C14> + many(lit<"a">) + lit<"e14">) |
                     (ref<C15> + many(lit<"a">) + lit<"e15">) |
                     (ref<C16> + many(lit<"a">) + lit<"e16">) |
                     (ref<C17> + many(lit<"a">) + lit<"e17">) |
                     (ref<C18> + many(lit<"a">) + lit<"e18">) |
                     (ref<C19> + many(lit<"a">) + lit<"e19">) |
                     (ref<C20> + many(lit<"a">) + lit<"e20">) |
                     (ref<C21> + many(lit<"a">) + lit<"e21">) |
                     (ref<C22> + many(lit<"a">) + lit<"e22">) |
                     (ref<C23> + many(lit<"a">) + lit<"e23">) |
                     (ref<C24> + many(lit<"a">) + lit<"e24">) |
                     (ref<C25> + many(lit<"a">) + lit<"e25">) |
                     (ref<C26> + many(lit<"a">) + lit<"e26">) |
                     (ref<C27> + many(lit<"a">) + lit<"e27">) |
                     (ref<C28> + many(lit<"a">) + lit<"e28">) |
                     (ref<C29> + many(lit<"a">) + lit<"e29">) |
                     (ref<C30> + many(lit<"a">) + lit<"e30">) |
                     (ref<C31> + many(lit<"a">) + lit<"e31">) |
                     (ref<C32> + many(lit<"a">) + lit<"e32">) |
                     (ref<C33> + many(lit<"a">) + lit<"e33">) |
                     (ref<C34> + many(lit<"a">) + lit<"e34">) |
                     (ref<C35> + many(lit<"a">) + lit<"e35">) |
                     (ref<C36> + many(lit<"a">) + lit<"e36">) |
                     (ref<C37> + many(lit<"a">) + lit<"e37">) |
                     (ref<C38> + many(lit<"a">) + lit<"e38">) |
                     (ref<C39> + many(lit<"a">) + lit<"e39">)> {};
  struct Root
      : rule<create<Doc>((assign<&Doc::outer>(ref<A0>) +
                          assign<&Doc::inner>(ref<Item>) + lit<"endA">) |
                         (assign<&Doc::outer>(ref<A1>) +
                          assign<&Doc::inner>(ref<Item>) + lit<"endB">) |
                         (assign<&Doc::outer>(ref<A2>) +
                          assign<&Doc::inner>(ref<Item>) + lit<"endC">))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tflood

// Forty arms no look-ahead can settle: a repetition stands between the
// terminal that opens an alternative and the literal that closes it.
namespace twideun {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {};
}  // namespace ast

struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct T0 : terminal<lit<"x"> + option(lit<"m0">)> {};
  struct T1 : terminal<lit<"x"> + option(lit<"m1">)> {};
  struct T2 : terminal<lit<"x"> + option(lit<"m2">)> {};
  struct T3 : terminal<lit<"x"> + option(lit<"m3">)> {};
  struct T4 : terminal<lit<"x"> + option(lit<"m4">)> {};
  struct T5 : terminal<lit<"x"> + option(lit<"m5">)> {};
  struct T6 : terminal<lit<"x"> + option(lit<"m6">)> {};
  struct T7 : terminal<lit<"x"> + option(lit<"m7">)> {};
  struct T8 : terminal<lit<"x"> + option(lit<"m8">)> {};
  struct T9 : terminal<lit<"x"> + option(lit<"m9">)> {};
  struct T10 : terminal<lit<"x"> + option(lit<"m10">)> {};
  struct T11 : terminal<lit<"x"> + option(lit<"m11">)> {};
  struct T12 : terminal<lit<"x"> + option(lit<"m12">)> {};
  struct T13 : terminal<lit<"x"> + option(lit<"m13">)> {};
  struct T14 : terminal<lit<"x"> + option(lit<"m14">)> {};
  struct T15 : terminal<lit<"x"> + option(lit<"m15">)> {};
  struct T16 : terminal<lit<"x"> + option(lit<"m16">)> {};
  struct T17 : terminal<lit<"x"> + option(lit<"m17">)> {};
  struct T18 : terminal<lit<"x"> + option(lit<"m18">)> {};
  struct T19 : terminal<lit<"x"> + option(lit<"m19">)> {};
  struct T20 : terminal<lit<"x"> + option(lit<"m20">)> {};
  struct T21 : terminal<lit<"x"> + option(lit<"m21">)> {};
  struct T22 : terminal<lit<"x"> + option(lit<"m22">)> {};
  struct T23 : terminal<lit<"x"> + option(lit<"m23">)> {};
  struct T24 : terminal<lit<"x"> + option(lit<"m24">)> {};
  struct T25 : terminal<lit<"x"> + option(lit<"m25">)> {};
  struct T26 : terminal<lit<"x"> + option(lit<"m26">)> {};
  struct T27 : terminal<lit<"x"> + option(lit<"m27">)> {};
  struct T28 : terminal<lit<"x"> + option(lit<"m28">)> {};
  struct T29 : terminal<lit<"x"> + option(lit<"m29">)> {};
  struct T30 : terminal<lit<"x"> + option(lit<"m30">)> {};
  struct T31 : terminal<lit<"x"> + option(lit<"m31">)> {};
  struct T32 : terminal<lit<"x"> + option(lit<"m32">)> {};
  struct T33 : terminal<lit<"x"> + option(lit<"m33">)> {};
  struct T34 : terminal<lit<"x"> + option(lit<"m34">)> {};
  struct T35 : terminal<lit<"x"> + option(lit<"m35">)> {};
  struct T36 : terminal<lit<"x"> + option(lit<"m36">)> {};
  struct T37 : terminal<lit<"x"> + option(lit<"m37">)> {};
  struct T38 : terminal<lit<"x"> + option(lit<"m38">)> {};
  struct T39 : terminal<lit<"x"> + option(lit<"m39">)> {};
  struct Root
      : rule<create<ast::Doc>((ref<T0> + many(lit<"a">) + lit<"end0">) |
                              (ref<T1> + many(lit<"a">) + lit<"end1">) |
                              (ref<T2> + many(lit<"a">) + lit<"end2">) |
                              (ref<T3> + many(lit<"a">) + lit<"end3">) |
                              (ref<T4> + many(lit<"a">) + lit<"end4">) |
                              (ref<T5> + many(lit<"a">) + lit<"end5">) |
                              (ref<T6> + many(lit<"a">) + lit<"end6">) |
                              (ref<T7> + many(lit<"a">) + lit<"end7">) |
                              (ref<T8> + many(lit<"a">) + lit<"end8">) |
                              (ref<T9> + many(lit<"a">) + lit<"end9">) |
                              (ref<T10> + many(lit<"a">) + lit<"end10">) |
                              (ref<T11> + many(lit<"a">) + lit<"end11">) |
                              (ref<T12> + many(lit<"a">) + lit<"end12">) |
                              (ref<T13> + many(lit<"a">) + lit<"end13">) |
                              (ref<T14> + many(lit<"a">) + lit<"end14">) |
                              (ref<T15> + many(lit<"a">) + lit<"end15">) |
                              (ref<T16> + many(lit<"a">) + lit<"end16">) |
                              (ref<T17> + many(lit<"a">) + lit<"end17">) |
                              (ref<T18> + many(lit<"a">) + lit<"end18">) |
                              (ref<T19> + many(lit<"a">) + lit<"end19">) |
                              (ref<T20> + many(lit<"a">) + lit<"end20">) |
                              (ref<T21> + many(lit<"a">) + lit<"end21">) |
                              (ref<T22> + many(lit<"a">) + lit<"end22">) |
                              (ref<T23> + many(lit<"a">) + lit<"end23">) |
                              (ref<T24> + many(lit<"a">) + lit<"end24">) |
                              (ref<T25> + many(lit<"a">) + lit<"end25">) |
                              (ref<T26> + many(lit<"a">) + lit<"end26">) |
                              (ref<T27> + many(lit<"a">) + lit<"end27">) |
                              (ref<T28> + many(lit<"a">) + lit<"end28">) |
                              (ref<T29> + many(lit<"a">) + lit<"end29">) |
                              (ref<T30> + many(lit<"a">) + lit<"end30">) |
                              (ref<T31> + many(lit<"a">) + lit<"end31">) |
                              (ref<T32> + many(lit<"a">) + lit<"end32">) |
                              (ref<T33> + many(lit<"a">) + lit<"end33">) |
                              (ref<T34> + many(lit<"a">) + lit<"end34">) |
                              (ref<T35> + many(lit<"a">) + lit<"end35">) |
                              (ref<T36> + many(lit<"a">) + lit<"end36">) |
                              (ref<T37> + many(lit<"a">) + lit<"end37">) |
                              (ref<T38> + many(lit<"a">) + lit<"end38">) |
                              (ref<T39> + many(lit<"a">) + lit<"end39">))> {};
  using entry = Root;
  using ignored = WS;
};

}  // namespace twideun

// More terminals match one position than a word has bits.


// Two readings of the first token, each opening a rule that offers forty
// of the next: more readings than the oracle carries at once.
namespace tnested {
struct Doc : cursiv::Extends<cursiv::AstNode> {};
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct X0 : terminal<lit<"x"> + option(lit<"m0">)> {};
  struct X1 : terminal<lit<"x"> + option(lit<"m1">)> {};
  struct Y0 : terminal<lit<"y"> + option(lit<"m0">)> {};
  struct Y1 : terminal<lit<"y"> + option(lit<"m1">)> {};
  struct Y2 : terminal<lit<"y"> + option(lit<"m2">)> {};
  struct Y3 : terminal<lit<"y"> + option(lit<"m3">)> {};
  struct Y4 : terminal<lit<"y"> + option(lit<"m4">)> {};
  struct Y5 : terminal<lit<"y"> + option(lit<"m5">)> {};
  struct Y6 : terminal<lit<"y"> + option(lit<"m6">)> {};
  struct Y7 : terminal<lit<"y"> + option(lit<"m7">)> {};
  struct Y8 : terminal<lit<"y"> + option(lit<"m8">)> {};
  struct Y9 : terminal<lit<"y"> + option(lit<"m9">)> {};
  struct Y10 : terminal<lit<"y"> + option(lit<"m10">)> {};
  struct Y11 : terminal<lit<"y"> + option(lit<"m11">)> {};
  struct Y12 : terminal<lit<"y"> + option(lit<"m12">)> {};
  struct Y13 : terminal<lit<"y"> + option(lit<"m13">)> {};
  struct Y14 : terminal<lit<"y"> + option(lit<"m14">)> {};
  struct Y15 : terminal<lit<"y"> + option(lit<"m15">)> {};
  struct Y16 : terminal<lit<"y"> + option(lit<"m16">)> {};
  struct Y17 : terminal<lit<"y"> + option(lit<"m17">)> {};
  struct Y18 : terminal<lit<"y"> + option(lit<"m18">)> {};
  struct Y19 : terminal<lit<"y"> + option(lit<"m19">)> {};
  struct Y20 : terminal<lit<"y"> + option(lit<"m20">)> {};
  struct Y21 : terminal<lit<"y"> + option(lit<"m21">)> {};
  struct Y22 : terminal<lit<"y"> + option(lit<"m22">)> {};
  struct Y23 : terminal<lit<"y"> + option(lit<"m23">)> {};
  struct Y24 : terminal<lit<"y"> + option(lit<"m24">)> {};
  struct Y25 : terminal<lit<"y"> + option(lit<"m25">)> {};
  struct Y26 : terminal<lit<"y"> + option(lit<"m26">)> {};
  struct Y27 : terminal<lit<"y"> + option(lit<"m27">)> {};
  struct Y28 : terminal<lit<"y"> + option(lit<"m28">)> {};
  struct Y29 : terminal<lit<"y"> + option(lit<"m29">)> {};
  struct Y30 : terminal<lit<"y"> + option(lit<"m30">)> {};
  struct Y31 : terminal<lit<"y"> + option(lit<"m31">)> {};
  struct Y32 : terminal<lit<"y"> + option(lit<"m32">)> {};
  struct Y33 : terminal<lit<"y"> + option(lit<"m33">)> {};
  struct Y34 : terminal<lit<"y"> + option(lit<"m34">)> {};
  struct Y35 : terminal<lit<"y"> + option(lit<"m35">)> {};
  struct Y36 : terminal<lit<"y"> + option(lit<"m36">)> {};
  struct Y37 : terminal<lit<"y"> + option(lit<"m37">)> {};
  struct Y38 : terminal<lit<"y"> + option(lit<"m38">)> {};
  struct Y39 : terminal<lit<"y"> + option(lit<"m39">)> {};
  struct Child : rule<(ref<Y0> + lit<"end0">) | (ref<Y1> + lit<"end1">) |
                      (ref<Y2> + lit<"end2">) | (ref<Y3> + lit<"end3">) |
                      (ref<Y4> + lit<"end4">) | (ref<Y5> + lit<"end5">) |
                      (ref<Y6> + lit<"end6">) | (ref<Y7> + lit<"end7">) |
                      (ref<Y8> + lit<"end8">) | (ref<Y9> + lit<"end9">) |
                      (ref<Y10> + lit<"end10">) | (ref<Y11> + lit<"end11">) |
                      (ref<Y12> + lit<"end12">) | (ref<Y13> + lit<"end13">) |
                      (ref<Y14> + lit<"end14">) | (ref<Y15> + lit<"end15">) |
                      (ref<Y16> + lit<"end16">) | (ref<Y17> + lit<"end17">) |
                      (ref<Y18> + lit<"end18">) | (ref<Y19> + lit<"end19">) |
                      (ref<Y20> + lit<"end20">) | (ref<Y21> + lit<"end21">) |
                      (ref<Y22> + lit<"end22">) | (ref<Y23> + lit<"end23">) |
                      (ref<Y24> + lit<"end24">) | (ref<Y25> + lit<"end25">) |
                      (ref<Y26> + lit<"end26">) | (ref<Y27> + lit<"end27">) |
                      (ref<Y28> + lit<"end28">) | (ref<Y29> + lit<"end29">) |
                      (ref<Y30> + lit<"end30">) | (ref<Y31> + lit<"end31">) |
                      (ref<Y32> + lit<"end32">) | (ref<Y33> + lit<"end33">) |
                      (ref<Y34> + lit<"end34">) | (ref<Y35> + lit<"end35">) |
                      (ref<Y36> + lit<"end36">) | (ref<Y37> + lit<"end37">) |
                      (ref<Y38> + lit<"end38">) | (ref<Y39> + lit<"end39">)> {};
  struct Root : rule<create<Doc>((ref<X0> + ref<Child> + lit<"b">) |
                                 (ref<X1> + ref<Child> + lit<"c">))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tnested

// The arm-count boundaries share one lexicon and one generated module.
namespace tcells {
struct G : tnamedsplit::G {
  struct Case : rule<ref<tnamedsplit::G::Root> |
                     (kw<"wide"> + ref<twideun::G::Root>) |
                     (kw<"nested"> + ref<tnested::G::Root>) |
                     (kw<"flood"> + ref<tflood::G::Root>) |
                     (kw<"nests"> + ref<tnests::G::Case>) |
                     (kw<"huge"> + ref<tnsplit2424::G::Root>)> {};
  using entry = Case;
};
}  // namespace tcells
