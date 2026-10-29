#pragma once

// Two cells no look-ahead can settle, one inside the other: a document needs
// the product of their arms as readings at once. Two shapes, because the
// contract is that a nest of undecidable cells still yields the one tree --
// the narrow one and the widest the lane drives.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

// Five arms outside, five inside: twenty-five readings.
namespace tnsplit {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  cursiv::AstNode* outer = nullptr;
  cursiv::AstNode* inner = nullptr;
};
struct P0 : cursiv::Extends<cursiv::AstNode> {};
struct P1 : cursiv::Extends<cursiv::AstNode> {};
struct P2 : cursiv::Extends<cursiv::AstNode> {};
struct P3 : cursiv::Extends<cursiv::AstNode> {};
struct P4 : cursiv::Extends<cursiv::AstNode> {};
struct Q0 : cursiv::Extends<cursiv::AstNode> {};
struct Q1 : cursiv::Extends<cursiv::AstNode> {};
struct Q2 : cursiv::Extends<cursiv::AstNode> {};
struct Q3 : cursiv::Extends<cursiv::AstNode> {};
struct Q4 : cursiv::Extends<cursiv::AstNode> {};
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct A0 : rule<create<P0>(lit<"p">)> {};
  struct A1 : rule<create<P1>(lit<"p">)> {};
  struct A2 : rule<create<P2>(lit<"p">)> {};
  struct A3 : rule<create<P3>(lit<"p">)> {};
  struct A4 : rule<create<P4>(lit<"p">)> {};
  struct B0 : rule<create<Q0>(lit<"q">)> {};
  struct B1 : rule<create<Q1>(lit<"q">)> {};
  struct B2 : rule<create<Q2>(lit<"q">)> {};
  struct B3 : rule<create<Q3>(lit<"q">)> {};
  struct B4 : rule<create<Q4>(lit<"q">)> {};
  struct Inner : rule<(ref<B0> + many(lit<"a">) + lit<"e0">) |
                      (ref<B1> + many(lit<"a">) + lit<"e1">) |
                      (ref<B2> + many(lit<"a">) + lit<"e2">) |
                      (ref<B3> + many(lit<"a">) + lit<"e3">) |
                      (ref<B4> + many(lit<"a">) + lit<"e4">)> {};
  struct Root
      : rule<create<Doc>((assign<&Doc::outer>(ref<A0>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o0">) |
                         (assign<&Doc::outer>(ref<A1>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o1">) |
                         (assign<&Doc::outer>(ref<A2>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o2">) |
                         (assign<&Doc::outer>(ref<A3>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o3">) |
                         (assign<&Doc::outer>(ref<A4>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o4">))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tnsplit

// Sixteen arms outside, ten inside: a hundred and sixty readings.
namespace tnsplit1610 {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  cursiv::AstNode* outer = nullptr;
  cursiv::AstNode* inner = nullptr;
};
struct P0 : cursiv::Extends<cursiv::AstNode> {};
struct P1 : cursiv::Extends<cursiv::AstNode> {};
struct P2 : cursiv::Extends<cursiv::AstNode> {};
struct P3 : cursiv::Extends<cursiv::AstNode> {};
struct P4 : cursiv::Extends<cursiv::AstNode> {};
struct P5 : cursiv::Extends<cursiv::AstNode> {};
struct P6 : cursiv::Extends<cursiv::AstNode> {};
struct P7 : cursiv::Extends<cursiv::AstNode> {};
struct P8 : cursiv::Extends<cursiv::AstNode> {};
struct P9 : cursiv::Extends<cursiv::AstNode> {};
struct P10 : cursiv::Extends<cursiv::AstNode> {};
struct P11 : cursiv::Extends<cursiv::AstNode> {};
struct P12 : cursiv::Extends<cursiv::AstNode> {};
struct P13 : cursiv::Extends<cursiv::AstNode> {};
struct P14 : cursiv::Extends<cursiv::AstNode> {};
struct P15 : cursiv::Extends<cursiv::AstNode> {};
struct Q0 : cursiv::Extends<cursiv::AstNode> {};
struct Q1 : cursiv::Extends<cursiv::AstNode> {};
struct Q2 : cursiv::Extends<cursiv::AstNode> {};
struct Q3 : cursiv::Extends<cursiv::AstNode> {};
struct Q4 : cursiv::Extends<cursiv::AstNode> {};
struct Q5 : cursiv::Extends<cursiv::AstNode> {};
struct Q6 : cursiv::Extends<cursiv::AstNode> {};
struct Q7 : cursiv::Extends<cursiv::AstNode> {};
struct Q8 : cursiv::Extends<cursiv::AstNode> {};
struct Q9 : cursiv::Extends<cursiv::AstNode> {};
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct A0 : rule<create<P0>(lit<"p">)> {};
  struct A1 : rule<create<P1>(lit<"p">)> {};
  struct A2 : rule<create<P2>(lit<"p">)> {};
  struct A3 : rule<create<P3>(lit<"p">)> {};
  struct A4 : rule<create<P4>(lit<"p">)> {};
  struct A5 : rule<create<P5>(lit<"p">)> {};
  struct A6 : rule<create<P6>(lit<"p">)> {};
  struct A7 : rule<create<P7>(lit<"p">)> {};
  struct A8 : rule<create<P8>(lit<"p">)> {};
  struct A9 : rule<create<P9>(lit<"p">)> {};
  struct A10 : rule<create<P10>(lit<"p">)> {};
  struct A11 : rule<create<P11>(lit<"p">)> {};
  struct A12 : rule<create<P12>(lit<"p">)> {};
  struct A13 : rule<create<P13>(lit<"p">)> {};
  struct A14 : rule<create<P14>(lit<"p">)> {};
  struct A15 : rule<create<P15>(lit<"p">)> {};
  struct B0 : rule<create<Q0>(lit<"q">)> {};
  struct B1 : rule<create<Q1>(lit<"q">)> {};
  struct B2 : rule<create<Q2>(lit<"q">)> {};
  struct B3 : rule<create<Q3>(lit<"q">)> {};
  struct B4 : rule<create<Q4>(lit<"q">)> {};
  struct B5 : rule<create<Q5>(lit<"q">)> {};
  struct B6 : rule<create<Q6>(lit<"q">)> {};
  struct B7 : rule<create<Q7>(lit<"q">)> {};
  struct B8 : rule<create<Q8>(lit<"q">)> {};
  struct B9 : rule<create<Q9>(lit<"q">)> {};
  struct Inner : rule<(ref<B0> + many(lit<"a">) + lit<"e0">) |
                      (ref<B1> + many(lit<"a">) + lit<"e1">) |
                      (ref<B2> + many(lit<"a">) + lit<"e2">) |
                      (ref<B3> + many(lit<"a">) + lit<"e3">) |
                      (ref<B4> + many(lit<"a">) + lit<"e4">) |
                      (ref<B5> + many(lit<"a">) + lit<"e5">) |
                      (ref<B6> + many(lit<"a">) + lit<"e6">) |
                      (ref<B7> + many(lit<"a">) + lit<"e7">) |
                      (ref<B8> + many(lit<"a">) + lit<"e8">) |
                      (ref<B9> + many(lit<"a">) + lit<"e9">)> {};
  struct Root
      : rule<create<Doc>((assign<&Doc::outer>(ref<A0>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o0">) |
                         (assign<&Doc::outer>(ref<A1>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o1">) |
                         (assign<&Doc::outer>(ref<A2>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o2">) |
                         (assign<&Doc::outer>(ref<A3>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o3">) |
                         (assign<&Doc::outer>(ref<A4>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o4">) |
                         (assign<&Doc::outer>(ref<A5>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o5">) |
                         (assign<&Doc::outer>(ref<A6>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o6">) |
                         (assign<&Doc::outer>(ref<A7>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o7">) |
                         (assign<&Doc::outer>(ref<A8>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o8">) |
                         (assign<&Doc::outer>(ref<A9>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o9">) |
                         (assign<&Doc::outer>(ref<A10>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o10">) |
                         (assign<&Doc::outer>(ref<A11>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o11">) |
                         (assign<&Doc::outer>(ref<A12>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o12">) |
                         (assign<&Doc::outer>(ref<A13>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o13">) |
                         (assign<&Doc::outer>(ref<A14>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o14">) |
                         (assign<&Doc::outer>(ref<A15>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o15">))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tnsplit1610

// The narrow nest and the sixteen-by-ten one in one grammar, each behind its
// own opening keyword: no rule is shared, so each nest reads as it does alone.
namespace tnests {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Case : rule<(kw<"five"> + ref<tnsplit::G::Root>) |
                     (kw<"sixteen"> + ref<tnsplit1610::G::Root>)> {};
  using entry = Case;
  using ignored = WS;
};
}  // namespace tnests

// Twenty-four arms outside, twenty-four inside: five hundred and seventy-six
// readings, more than the room holds at once.
namespace tnsplit2424 {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  cursiv::AstNode* outer = nullptr;
  cursiv::AstNode* inner = nullptr;
};
struct P0 : cursiv::Extends<cursiv::AstNode> {};
struct P1 : cursiv::Extends<cursiv::AstNode> {};
struct P2 : cursiv::Extends<cursiv::AstNode> {};
struct P3 : cursiv::Extends<cursiv::AstNode> {};
struct P4 : cursiv::Extends<cursiv::AstNode> {};
struct P5 : cursiv::Extends<cursiv::AstNode> {};
struct P6 : cursiv::Extends<cursiv::AstNode> {};
struct P7 : cursiv::Extends<cursiv::AstNode> {};
struct P8 : cursiv::Extends<cursiv::AstNode> {};
struct P9 : cursiv::Extends<cursiv::AstNode> {};
struct P10 : cursiv::Extends<cursiv::AstNode> {};
struct P11 : cursiv::Extends<cursiv::AstNode> {};
struct P12 : cursiv::Extends<cursiv::AstNode> {};
struct P13 : cursiv::Extends<cursiv::AstNode> {};
struct P14 : cursiv::Extends<cursiv::AstNode> {};
struct P15 : cursiv::Extends<cursiv::AstNode> {};
struct P16 : cursiv::Extends<cursiv::AstNode> {};
struct P17 : cursiv::Extends<cursiv::AstNode> {};
struct P18 : cursiv::Extends<cursiv::AstNode> {};
struct P19 : cursiv::Extends<cursiv::AstNode> {};
struct P20 : cursiv::Extends<cursiv::AstNode> {};
struct P21 : cursiv::Extends<cursiv::AstNode> {};
struct P22 : cursiv::Extends<cursiv::AstNode> {};
struct P23 : cursiv::Extends<cursiv::AstNode> {};
struct Q0 : cursiv::Extends<cursiv::AstNode> {};
struct Q1 : cursiv::Extends<cursiv::AstNode> {};
struct Q2 : cursiv::Extends<cursiv::AstNode> {};
struct Q3 : cursiv::Extends<cursiv::AstNode> {};
struct Q4 : cursiv::Extends<cursiv::AstNode> {};
struct Q5 : cursiv::Extends<cursiv::AstNode> {};
struct Q6 : cursiv::Extends<cursiv::AstNode> {};
struct Q7 : cursiv::Extends<cursiv::AstNode> {};
struct Q8 : cursiv::Extends<cursiv::AstNode> {};
struct Q9 : cursiv::Extends<cursiv::AstNode> {};
struct Q10 : cursiv::Extends<cursiv::AstNode> {};
struct Q11 : cursiv::Extends<cursiv::AstNode> {};
struct Q12 : cursiv::Extends<cursiv::AstNode> {};
struct Q13 : cursiv::Extends<cursiv::AstNode> {};
struct Q14 : cursiv::Extends<cursiv::AstNode> {};
struct Q15 : cursiv::Extends<cursiv::AstNode> {};
struct Q16 : cursiv::Extends<cursiv::AstNode> {};
struct Q17 : cursiv::Extends<cursiv::AstNode> {};
struct Q18 : cursiv::Extends<cursiv::AstNode> {};
struct Q19 : cursiv::Extends<cursiv::AstNode> {};
struct Q20 : cursiv::Extends<cursiv::AstNode> {};
struct Q21 : cursiv::Extends<cursiv::AstNode> {};
struct Q22 : cursiv::Extends<cursiv::AstNode> {};
struct Q23 : cursiv::Extends<cursiv::AstNode> {};
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct A0 : rule<create<P0>(lit<"p">)> {};
  struct A1 : rule<create<P1>(lit<"p">)> {};
  struct A2 : rule<create<P2>(lit<"p">)> {};
  struct A3 : rule<create<P3>(lit<"p">)> {};
  struct A4 : rule<create<P4>(lit<"p">)> {};
  struct A5 : rule<create<P5>(lit<"p">)> {};
  struct A6 : rule<create<P6>(lit<"p">)> {};
  struct A7 : rule<create<P7>(lit<"p">)> {};
  struct A8 : rule<create<P8>(lit<"p">)> {};
  struct A9 : rule<create<P9>(lit<"p">)> {};
  struct A10 : rule<create<P10>(lit<"p">)> {};
  struct A11 : rule<create<P11>(lit<"p">)> {};
  struct A12 : rule<create<P12>(lit<"p">)> {};
  struct A13 : rule<create<P13>(lit<"p">)> {};
  struct A14 : rule<create<P14>(lit<"p">)> {};
  struct A15 : rule<create<P15>(lit<"p">)> {};
  struct A16 : rule<create<P16>(lit<"p">)> {};
  struct A17 : rule<create<P17>(lit<"p">)> {};
  struct A18 : rule<create<P18>(lit<"p">)> {};
  struct A19 : rule<create<P19>(lit<"p">)> {};
  struct A20 : rule<create<P20>(lit<"p">)> {};
  struct A21 : rule<create<P21>(lit<"p">)> {};
  struct A22 : rule<create<P22>(lit<"p">)> {};
  struct A23 : rule<create<P23>(lit<"p">)> {};
  struct B0 : rule<create<Q0>(lit<"q">)> {};
  struct B1 : rule<create<Q1>(lit<"q">)> {};
  struct B2 : rule<create<Q2>(lit<"q">)> {};
  struct B3 : rule<create<Q3>(lit<"q">)> {};
  struct B4 : rule<create<Q4>(lit<"q">)> {};
  struct B5 : rule<create<Q5>(lit<"q">)> {};
  struct B6 : rule<create<Q6>(lit<"q">)> {};
  struct B7 : rule<create<Q7>(lit<"q">)> {};
  struct B8 : rule<create<Q8>(lit<"q">)> {};
  struct B9 : rule<create<Q9>(lit<"q">)> {};
  struct B10 : rule<create<Q10>(lit<"q">)> {};
  struct B11 : rule<create<Q11>(lit<"q">)> {};
  struct B12 : rule<create<Q12>(lit<"q">)> {};
  struct B13 : rule<create<Q13>(lit<"q">)> {};
  struct B14 : rule<create<Q14>(lit<"q">)> {};
  struct B15 : rule<create<Q15>(lit<"q">)> {};
  struct B16 : rule<create<Q16>(lit<"q">)> {};
  struct B17 : rule<create<Q17>(lit<"q">)> {};
  struct B18 : rule<create<Q18>(lit<"q">)> {};
  struct B19 : rule<create<Q19>(lit<"q">)> {};
  struct B20 : rule<create<Q20>(lit<"q">)> {};
  struct B21 : rule<create<Q21>(lit<"q">)> {};
  struct B22 : rule<create<Q22>(lit<"q">)> {};
  struct B23 : rule<create<Q23>(lit<"q">)> {};
  struct Inner : rule<(ref<B0> + many(lit<"a">) + lit<"e0">) |
                      (ref<B1> + many(lit<"a">) + lit<"e1">) |
                      (ref<B2> + many(lit<"a">) + lit<"e2">) |
                      (ref<B3> + many(lit<"a">) + lit<"e3">) |
                      (ref<B4> + many(lit<"a">) + lit<"e4">) |
                      (ref<B5> + many(lit<"a">) + lit<"e5">) |
                      (ref<B6> + many(lit<"a">) + lit<"e6">) |
                      (ref<B7> + many(lit<"a">) + lit<"e7">) |
                      (ref<B8> + many(lit<"a">) + lit<"e8">) |
                      (ref<B9> + many(lit<"a">) + lit<"e9">) |
                      (ref<B10> + many(lit<"a">) + lit<"e10">) |
                      (ref<B11> + many(lit<"a">) + lit<"e11">) |
                      (ref<B12> + many(lit<"a">) + lit<"e12">) |
                      (ref<B13> + many(lit<"a">) + lit<"e13">) |
                      (ref<B14> + many(lit<"a">) + lit<"e14">) |
                      (ref<B15> + many(lit<"a">) + lit<"e15">) |
                      (ref<B16> + many(lit<"a">) + lit<"e16">) |
                      (ref<B17> + many(lit<"a">) + lit<"e17">) |
                      (ref<B18> + many(lit<"a">) + lit<"e18">) |
                      (ref<B19> + many(lit<"a">) + lit<"e19">) |
                      (ref<B20> + many(lit<"a">) + lit<"e20">) |
                      (ref<B21> + many(lit<"a">) + lit<"e21">) |
                      (ref<B22> + many(lit<"a">) + lit<"e22">) |
                      (ref<B23> + many(lit<"a">) + lit<"e23">)> {};
  struct Root
      : rule<create<Doc>((assign<&Doc::outer>(ref<A0>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o0">) |
                         (assign<&Doc::outer>(ref<A1>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o1">) |
                         (assign<&Doc::outer>(ref<A2>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o2">) |
                         (assign<&Doc::outer>(ref<A3>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o3">) |
                         (assign<&Doc::outer>(ref<A4>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o4">) |
                         (assign<&Doc::outer>(ref<A5>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o5">) |
                         (assign<&Doc::outer>(ref<A6>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o6">) |
                         (assign<&Doc::outer>(ref<A7>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o7">) |
                         (assign<&Doc::outer>(ref<A8>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o8">) |
                         (assign<&Doc::outer>(ref<A9>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o9">) |
                         (assign<&Doc::outer>(ref<A10>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o10">) |
                         (assign<&Doc::outer>(ref<A11>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o11">) |
                         (assign<&Doc::outer>(ref<A12>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o12">) |
                         (assign<&Doc::outer>(ref<A13>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o13">) |
                         (assign<&Doc::outer>(ref<A14>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o14">) |
                         (assign<&Doc::outer>(ref<A15>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o15">) |
                         (assign<&Doc::outer>(ref<A16>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o16">) |
                         (assign<&Doc::outer>(ref<A17>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o17">) |
                         (assign<&Doc::outer>(ref<A18>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o18">) |
                         (assign<&Doc::outer>(ref<A19>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o19">) |
                         (assign<&Doc::outer>(ref<A20>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o20">) |
                         (assign<&Doc::outer>(ref<A21>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o21">) |
                         (assign<&Doc::outer>(ref<A22>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o22">) |
                         (assign<&Doc::outer>(ref<A23>) +
                          assign<&Doc::inner>(ref<Inner>) + lit<"o23">))> {};
  using entry = Root;
  using ignored = WS;
};
}  // namespace tnsplit2424
