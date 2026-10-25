// Dozens of words refused at the head of a name that is also read where
// every one of them is a keyword: the names around the words keep their
// extent.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace trefusedheads {

struct Item : cursiv::Extends<cursiv::AstNode> {
  bool keyword = false;
  bool bound = false;
  string_view name;
};
struct Doc : cursiv::Extends<cursiv::AstNode> {
  vector<Item*> items;
};

struct G : cursiv::GrammarSpec {
  static constexpr auto Words =
      kw<"a"> | kw<"an"> | kw<"and"> | kw<"any"> |
      kw<"array"> | kw<"as"> | kw<"ask"> | kw<"assert"> |
      kw<"begin"> | kw<"bind"> | kw<"bit"> | kw<"block"> |
      kw<"body"> | kw<"bool"> | kw<"break"> | kw<"by"> |
      kw<"case"> | kw<"cast"> | kw<"catch"> | kw<"char"> |
      kw<"check"> | kw<"class"> | kw<"const"> | kw<"continue"> |
      kw<"copy"> | kw<"count"> | kw<"create"> | kw<"declare"> |
      kw<"default"> | kw<"define"> | kw<"delete"> | kw<"do"> |
      kw<"else"> | kw<"elseif"> | kw<"end"> | kw<"enum"> |
      kw<"exit"> | kw<"extern"> | kw<"false"> | kw<"fetch"> |
      kw<"for"> | kw<"from"> | kw<"func"> | kw<"goto"> |
      kw<"if"> | kw<"import"> | kw<"in"> | kw<"index"> |
      kw<"inline"> | kw<"int"> | kw<"is"> | kw<"let"> |
      kw<"like"> | kw<"loop"> | kw<"match"> | kw<"mod"> |
      kw<"new"> | kw<"nil"> | kw<"not"> | kw<"null"> |
      kw<"of"> | kw<"or"> | kw<"out"> | kw<"pass"> |
      kw<"print"> | kw<"raise"> | kw<"read"> | kw<"ref"> |
      kw<"repeat"> | kw<"return"> | kw<"select"> | kw<"set"> |
      kw<"sort"> | kw<"static"> | kw<"step"> | kw<"struct"> |
      kw<"switch"> | kw<"then"> | kw<"to"> | kw<"true"> |
      kw<"try"> | kw<"type"> | kw<"union"> | kw<"unless"> |
      kw<"until"> | kw<"use"> | kw<"var"> | kw<"while"> |
      kw<"with"> | kw<"write"> | kw<"yield"> | kw<"alpha"> |
      kw<"bravo"> | kw<"charlie"> | kw<"delta"> | kw<"echo"> |
      kw<"foxtrot"> | kw<"golf"> | kw<"hotel"> | kw<"india"> |
      kw<"juliet"> | kw<"kilo"> | kw<"lima"> | kw<"mike"> |
      kw<"november"> | kw<"oscar"> | kw<"papa"> | kw<"quebec"> |
      kw<"romeo"> | kw<"sierra"> | kw<"tango"> | kw<"uniform"> |
      kw<"victor"> | kw<"whiskey"> | kw<"xray"> | kw<"yankee"> |
      kw<"zulu"> | kw<"apple"> | kw<"banana"> | kw<"cherry"> |
      kw<"damson"> | kw<"elder"> | kw<"fig"> | kw<"grape"> |
      kw<"hazel"> | kw<"iris"> | kw<"jade"> | kw<"kiwi"> |
      kw<"lemon"> | kw<"mango"> | kw<"nectar"> | kw<"olive"> |
      kw<"peach"> | kw<"quince"> | kw<"rowan"> | kw<"sorrel"> |
      kw<"thyme"> | kw<"ugli"> | kw<"vanilla"> | kw<"walnut">;
  static constexpr auto Others =
      kw<"zephyr"> | kw<"zodiac"> | kw<"zenith"> | kw<"zigzag"> |
      kw<"zealot"> | kw<"zebra"> | kw<"zinc"> | kw<"zonal"> |
      kw<"zippy"> | kw<"zircon"> | kw<"zydeco"> | kw<"zero"> |
      kw<"zone"> | kw<"zeta"> | kw<"zing"> | kw<"zest"> |
      kw<"zap"> | kw<"zoo"> | kw<"zany"> | kw<"zeal"> |
      kw<"zilch"> | kw<"zither"> | kw<"zombie"> | kw<"zoetic"> |
      kw<"zonule"> | kw<"zorro"> | kw<"zucchini"> | kw<"zwieback"> |
      kw<"zygote"> | kw<"zoology"> | kw<"zoning"> | kw<"zipper"> |
      kw<"zestful"> | kw<"zealous"> | kw<"zooid"> | kw<"zymase"> |
      kw<"zibeline"> | kw<"zanzibar"> | kw<"zymurgy"> | kw<"zirconia">;
  struct WS : terminal<some(s)> {};
  struct NAME : terminal<!Words + cr<"a-zA-Z_"> + many(w)> {};
  struct Stmt
      : rule<create<Item>(((assign<&Item::name>(Words) +
                            flag<&Item::keyword>(lit<"!">)) |
                           (assign<&Item::name>(Others) + lit<"?">) |
                           assign<&Item::name>(ref<NAME>) |
                           (flag<&Item::bound>(lit<"=">) +
                            assign<&Item::name>(ref<NAME>))) +
                          lit<";">)> {};
  struct Root : rule<create<Doc>(many(append<&Doc::items>(ref<Stmt>)))> {};
  using entry = Root;
  using ignore = WS;
};

}  // namespace trefusedheads
