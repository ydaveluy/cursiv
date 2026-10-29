#pragma once

// A comment that nests: the closer that ends it is the one balancing every
// opener inside, and a string outside carries no opener at all.

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tnestedlex {

namespace ast {
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
};
}  // namespace ast

struct Core : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct COMMENT
      : terminal<lit<"/*"> +
                 many(ref<COMMENT> | (!lit<"/*"> + !lit<"*/"> + any)) +
                 lit<"*/">> {};
  // Two openers one closer balances, as a preprocessor's conditionals; two
  // of each, no first byte shared among the openers; and a run the end of
  // the text closes too.
  static constexpr auto RegionOpen = lit<"`ifdef"> | lit<"`ifndef">;
  struct REGION
      : terminal<RegionOpen +
                 many(ref<REGION> | (!RegionOpen + !lit<"`endif"> + any)) +
                 lit<"`endif">> {};
  static constexpr auto BlockOpen = lit<"{|"> | lit<"(|">;
  static constexpr auto BlockClose = lit<"|}"> | lit<"|)">;
  struct BLOCK : terminal<BlockOpen +
                          many(ref<BLOCK> | (!BlockOpen + !BlockClose + any)) +
                          BlockClose> {};
  struct OPEN
      : terminal<lit<"(*"> + many(ref<OPEN> | (!lit<"(*"> + !lit<"*)"> + any)) +
                 (lit<"*)"> | eof)> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct STRING : terminal<through(lit<"\"">, lit<"\"">)> {};
  struct Doc : rule<create<ast::Doc>(
                   many(append<&ast::Doc::items>(ref<ID> | ref<STRING>)))> {};
  using entry = Doc;
  using ignored = list<WS, COMMENT, REGION, BLOCK, OPEN>;
};

namespace overlap {
struct G : cursiv::GrammarSpec {
  struct Comment
      : terminal<lit<"/*"> +
                 many(ref<Comment> | (!lit<"/*"> + !lit<"*/"> + any)) +
                 lit<"*/">> {};
  struct Run : terminal<ref<Comment> | lit<"/*x">> {};
  struct Twin : terminal<cr<"/"> + cr<"*"> + cr<"x">> {};
  struct Pure
      : terminal<lit<"<*"> + many(ref<Pure> | (!lit<"<*"> + !lit<"*>"> + any)) +
                 lit<"*>">> {};
  struct NestedWord
      : terminal<lit<"i"> +
                 many(ref<NestedWord> | (!lit<"i"> + !lit<"f"> + any)) +
                 lit<"f">> {};
  struct Word : terminal<ref<NestedWord> | some(cr<"a-z">)> {};
  struct Doc : rule<create<ast::Doc>(
      many(append<&ast::Doc::items>(ref<Run> | ref<Twin> | lit<"/*x*/"> |
                                    ref<Pure> | lit<"<*x*>">) |
           (ref<Word> + lit<"?">) | (kw<"if"> + lit<"!">)))> {};
  using entry = Doc;
};
}  // namespace overlap

// Two refusals that end on the same byte: it is read once, and the search
// starts behind it just the same.
namespace pragmas {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NestedComment
      : terminal<lit<"{-"> +
                 many(ref<NestedComment> | (!lit<"{-"> + !lit<"-}"> + any)) +
                 lit<"-}">> {};
  struct COMMENT : terminal<!lit<"{-#"> + !lit<"{-$"> + ref<NestedComment>> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Doc : rule<create<ast::Doc>(many(append<&ast::Doc::items>(
                   ref<ID> | lit<"{-#"> | lit<"{-$"> | lit<"#-}">)))> {};
  using entry = Doc;
  using ignored = list<WS, COMMENT>;
};
}  // namespace pragmas

// Delimiters as long as the author writes them: forty bytes each.
namespace wide {
struct G : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  static constexpr auto Open = lit<"<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<">;
  static constexpr auto Close = lit<">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>">;
  struct COMMENT
      : terminal<Open + many(ref<COMMENT> | (!Open + !Close + any)) + Close> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Doc : rule<create<ast::Doc>(many(append<&ast::Doc::items>(ref<ID>)))> {
  };
  using entry = Doc;
  using ignored = list<WS, COMMENT>;
};
}  // namespace wide

// The ignored nested runs share one lexicon; pragma markers remain tokens.
struct G : Core {
  struct Doc : rule<create<ast::Doc>(many(append<&ast::Doc::items>(
                   ref<ID> | ref<STRING> | lit<"{-#"> | lit<"{-$"> |
                   lit<"#-}">)))> {};
  using entry = Doc;
  using ignored = list<WS, COMMENT, REGION, BLOCK, OPEN,
                      pragmas::G::COMMENT, wide::G::COMMENT>;
};

}  // namespace tnestedlex
