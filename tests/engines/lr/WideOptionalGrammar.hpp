#pragma once

// Bodies of more optional positions than the table distributes, each position
// one a `skip()` region's edge or an end-of-input assertion keeps in place:
// both engines read them, whatever the count.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

#define TWIDE_WORD(S) append<&ast::Doc::words>(lit<S>)
#define TWIDE_TIGHT(S) tight(option(TWIDE_WORD(S)))
#define TWIDE_HEAD(S) option(TWIDE_WORD(S))
#define TWIDE_PAIR(A, B) option(tight(TWIDE_WORD(A) + TWIDE_WORD(B)))
#define TWIDE_END(S) (TWIDE_WORD(S) | eof)
#define TWIDE_OVERLAP                                         \
  tight(option(TWIDE_WORD("x"))) +                            \
      tight(option(append<&ast::Doc::extra>(lit<"x"> | lit<"y">)))

namespace twide {

namespace ast {
struct Mark : cursiv::Extends<cursiv::AstNode> {};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> words;
  vector<string_view> extra;
  vector<Mark*> marks;
};
}  // namespace ast

struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  // Twenty optional regions of one token each: both edges are the caller's.
  struct Edges
      : rule<kw<"e"> + TWIDE_TIGHT("p0") + TWIDE_TIGHT("p1") +
             TWIDE_TIGHT("p2") + TWIDE_TIGHT("p3") + TWIDE_TIGHT("p4") +
             TWIDE_TIGHT("p5") + TWIDE_TIGHT("p6") + TWIDE_TIGHT("p7") +
             TWIDE_TIGHT("p8") + TWIDE_TIGHT("p9") + TWIDE_TIGHT("q0") +
             TWIDE_TIGHT("q1") + TWIDE_TIGHT("q2") + TWIDE_TIGHT("q3") +
             TWIDE_TIGHT("q4") + TWIDE_TIGHT("q5") + TWIDE_TIGHT("q6") +
             TWIDE_TIGHT("q7") + TWIDE_TIGHT("q8") + TWIDE_TIGHT("q9") +
             kw<"end">> {};
  // A region opening on eighteen optional parts and closing on a required one.
  struct Heads
      : rule<kw<"h"> +
             tight(TWIDE_HEAD("a") + TWIDE_HEAD("b") + TWIDE_HEAD("c") +
                       TWIDE_HEAD("d") + TWIDE_HEAD("e") + TWIDE_HEAD("f") +
                       TWIDE_HEAD("g") + TWIDE_HEAD("h") + TWIDE_HEAD("i") +
                       TWIDE_HEAD("j") + TWIDE_HEAD("k") + TWIDE_HEAD("l") +
                       TWIDE_HEAD("m") + TWIDE_HEAD("n") + TWIDE_HEAD("o") +
                       TWIDE_HEAD("p") + TWIDE_HEAD("q") + TWIDE_HEAD("r") +
                       TWIDE_WORD("z")) +
             kw<"end">> {};
  // Ten optional regions of two tokens, tight inside.
  struct Pairs
      : rule<kw<"w"> + TWIDE_PAIR("m0", "n0") + TWIDE_PAIR("m1", "n1") +
             TWIDE_PAIR("m2", "n2") + TWIDE_PAIR("m3", "n3") +
             TWIDE_PAIR("m4", "n4") + TWIDE_PAIR("m5", "n5") +
             TWIDE_PAIR("m6", "n6") + TWIDE_PAIR("m7", "n7") +
             TWIDE_PAIR("m8", "n8") + TWIDE_PAIR("m9", "n9") + kw<"end">> {};
  // Ten choices one branch of which asserts the end of the text.
  struct Ends : rule<kw<"d"> + TWIDE_END("s0") + TWIDE_END("s1") +
                     TWIDE_END("s2") + TWIDE_END("s3") + TWIDE_END("s4") +
                     TWIDE_END("s5") + TWIDE_END("s6") + TWIDE_END("s7") +
                     TWIDE_END("s8") + TWIDE_END("s9")> {};
  // Regions that open on the same token, the first of each pair writing a
  // member the second does not.
  struct Overlap
      : rule<kw<"o"> + TWIDE_OVERLAP + TWIDE_OVERLAP + TWIDE_OVERLAP +
             TWIDE_OVERLAP + TWIDE_OVERLAP + kw<"end">> {};
  struct Tail : rule<create<ast::Mark>(option(lit<"k">))> {};
  // An end assertion ahead of nine optional parts and a rule that may match
  // nothing and still build a node.
  struct Mixed
      : rule<kw<"m"> + TWIDE_END("t") + TWIDE_HEAD("c0") + TWIDE_HEAD("c1") +
             TWIDE_HEAD("c2") + TWIDE_HEAD("c3") + TWIDE_HEAD("c4") +
             TWIDE_HEAD("c5") + TWIDE_HEAD("c6") + TWIDE_HEAD("c7") +
             TWIDE_HEAD("c8") + append<&ast::Doc::marks>(ref<Tail>)> {};
  // A token required after a choice that may end the text: the arm that ends
  // it is never read.
  struct Tailed : rule<kw<"r"> + TWIDE_END("u") + TWIDE_WORD("b") +
                       TWIDE_HEAD("c0") + TWIDE_HEAD("c1") + TWIDE_HEAD("c2") +
                       TWIDE_HEAD("c3") + TWIDE_HEAD("c4") + TWIDE_HEAD("c5") +
                       TWIDE_HEAD("c6") + TWIDE_HEAD("c7")> {};
  struct Root : rule<ref<Edges> | ref<Heads> | ref<Pairs> | ref<Ends> |
                     ref<Overlap> | ref<Mixed> | ref<Tailed> |
                     create<ast::Doc>(tight(TWIDE_HEAD("a") + TWIDE_WORD("b") + lit<";">))> {};
  using entry = Root;
  using ignore = WS;
};
struct Ll : Base {};
struct Lr : Base {};

}  // namespace twide

#undef TWIDE_OVERLAP
#undef TWIDE_END
#undef TWIDE_PAIR
#undef TWIDE_HEAD
#undef TWIDE_TIGHT
#undef TWIDE_WORD
