// A left recursion through an infix reads as the loop of the rule a call starts
// with, the infix standing as its levels: both engines read what the walk of
// the spec elects, whichever rule the call enters at.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/InfixLoopGrammar.hpp"
#include "indirectfoldll/generated/Ll/Module.hpp"

namespace infix_loop_cases {
namespace {

struct Case {
  std::string_view text, tree;
};

constexpr Case kSum[] = {
    {"e/ a", "Doc(Ba)"},
    {"e/ a + b", "Doc(Bin(BaBb))"},
    {"e/ a !", "Doc(Post(Ba))"},
    {"e/ a + b !", "Doc(Bin(BaPost(Bb)))"},
    {"e/ a ! + b", "Doc(Bin(Post(Ba)Bb))"},
    {"e/ a + b + a", "Doc(Bin(Bin(BaBb)Ba))"},
    {"e/ ( a + b ) !", "Doc(Post(Bin(BaBb)))"},
    {"e/ a + b ! !", "Doc(Bin(BaPost(Post(Bb))))"},
    {"e/ a ! !", "Doc(Post(Post(Ba)))"},
    {"e/ a + b ! + a", "Doc(Bin(Bin(BaPost(Bb))Ba))"},
    {"e/ a + ( b ! )", "Doc(Bin(BaPost(Bb)))"},
    {"p/ a", "Doc(Ba)"},
    {"p/ a !", "Doc(Post(Ba))"},
    {"p/ a + b !", "Doc(Post(Bin(BaBb)))"},
    {"p/ ( a + b ) !", "Doc(Post(Bin(BaBb)))"},
    {"p/ a + b ! !", "Doc(Post(Bin(BaPost(Bb))))"},
    {"p/ a ! !", "Doc(Post(Post(Ba)))"},
};
constexpr Case kLadder[] = {
    {"e/ a", "Doc(Ba)"},
    {"e/ - a + b", "Doc(Neg(Bin(BaBb)))"},
    {"e/ - a * b", "Doc(Neg(Bin(BaBb)))"},
    {"e/ a * b !", "Doc(Bin(BaPost(Bb)))"},
    {"e/ a + b * a !", "Doc(Bin(BaBin(BbPost(Ba))))"},
    {"e/ - a !", "Doc(Neg(Post(Ba)))"},
    {"e/ - a + b !", "Doc(Neg(Bin(BaPost(Bb))))"},
    {"e/ a * - b", "Doc(Bin(BaNeg(Bb)))"},
    {"e/ - a * b + a", "Doc(Neg(Bin(Bin(BaBb)Ba)))"},
    {"e/ ( a + b ) * a !", "Doc(Bin(Bin(BaBb)Post(Ba)))"},
    {"e/ a * b + a !", "Doc(Bin(BaPost(Bin(BbBa))))"},
    {"e/ a + b * a", "Doc(Bin(BaBin(BbBa)))"},
    {"p/ a", "Doc(Ba)"},
    {"p/ - a + b", "Doc(Neg(Bin(BaBb)))"},
    {"p/ - a * b", "Doc(Neg(Bin(BaBb)))"},
    {"p/ a * b !", "Doc(Post(Bin(BaBb)))"},
    {"p/ a + b * a !", "Doc(Post(Bin(BaBin(BbBa))))"},
    {"p/ - a !", "Doc(Neg(Post(Ba)))"},
    {"p/ - a + b !", "Doc(Neg(Bin(BaPost(Bb))))"},
    {"p/ - a * b + a", "Doc(Neg(Bin(Bin(BaBb)Ba)))"},
    {"p/ ( a + b ) * a !", "Doc(Post(Bin(Bin(BaBb)Ba)))"},
    {"p/ a * b + a !", "Doc(Post(Bin(Bin(BaBb)Ba)))"},
};
constexpr Case kVia[] = {
    {"e/ a ?", "Doc(Qst(Ba))"},
    {"e/ a + b ?", "Doc(Bin(BaQst(Bb)))"},
    {"e/ a * b ?", "Doc(Bin(BaQst(Bb)))"},
    {"e/ a ? + b", "Doc(Bin(Qst(Ba)Bb))"},
    {"e/ a + b ? ?", "Doc(Bin(BaQst(Qst(Bb))))"},
    {"e/ a * b + a ?", "Doc(Bin(BaQst(Bin(BbBa))))"},
    {"e/ ( a + b ) ?", "Doc(Qst(Bin(BaBb)))"},
    {"e/ a + b * a ?", "Doc(Bin(BaBin(BbQst(Ba))))"},
    {"p/ a ?", "Doc(Qst(Ba))"},
    {"p/ a + b ?", "Doc(Qst(Bin(BaBb)))"},
    {"p/ a * b ?", "Doc(Qst(Bin(BaBb)))"},
    {"p/ a + b ? ?", "Doc(Qst(Bin(BaQst(Bb))))"},
    {"p/ a * b + a ?", "Doc(Qst(Bin(Bin(BaBb)Ba)))"},
    {"p/ ( a + b ) ?", "Doc(Qst(Bin(BaBb)))"},
    {"p/ a + b * a ?", "Doc(Qst(Bin(BaBin(BbBa))))"},
    {"w/ a ?", "Doc(Qst(Ba))"},
    {"w/ a + b ?", "Doc(Qst(Bin(BaBb)))"},
    {"w/ a * b ?", "Doc(Qst(Bin(BaBb)))"},
    {"w/ a + b ? ?", "Doc(Qst(Bin(BaQst(Bb))))"},
    {"w/ a * b + a ?", "Doc(Qst(Bin(Bin(BaBb)Ba)))"},
    {"w/ ( a + b ) ?", "Doc(Qst(Bin(BaBb)))"},
    {"w/ a + b * a ?", "Doc(Qst(Bin(BaBin(BbBa))))"},
};
constexpr Case kMixed[] = {
    {"e/ a ^ b ^ a", "Doc(Bin(BaBin(BbBa)))"},
    {"e/ a ^ b !", "Doc(Bin(BaPost(Bb)))"},
    {"e/ - a ^ b", "Doc(Bin(Neg(Ba)Bb))"},
    {"e/ - a + b", "Doc(Neg(Bin(BaBb)))"},
    {"e/ a ^ b + a !", "Doc(Bin(BaPost(Bin(BbBa))))"},
    {"e/ a + b ^ a !", "Doc(Bin(BaBin(BbPost(Ba))))"},
    {"e/ a ^ b + a", "Doc(Bin(Bin(BaBb)Ba))"},
    {"e/ ( a ^ b ) !", "Doc(Post(Bin(BaBb)))"},
    {"e/ a + b ^ a", "Doc(Bin(BaBin(BbBa)))"},
    {"p/ a ^ b !", "Doc(Post(Bin(BaBb)))"},
    {"p/ - a ^ b", "Doc(Neg(Bin(BaBb)))"},
    {"p/ - a + b", "Doc(Neg(Bin(BaBb)))"},
    {"p/ a ^ b + a !", "Doc(Post(Bin(Bin(BaBb)Ba)))"},
    {"p/ a + b ^ a !", "Doc(Post(Bin(BaBin(BbBa))))"},
    {"p/ ( a ^ b ) !", "Doc(Post(Bin(BaBb)))"},
};

std::string dump(const cursiv::AstReflection& r, const cursiv::AstNode& n) {
  std::string kids;
  r.visitMembers(n, [&](std::string_view, std::uint32_t, cursiv::AstNode& kid) {
    kids += dump(r, kid);
  });
  const std::string name(r.getTypeMetaData(n.type).name);
  return kids.empty() ? name : name + '(' + kids + ')';
}

std::string read(const cursiv::Parser& parser, const cursiv::AstReflection& r,
                 std::string_view text) {
  const auto result = parser.parse(text);
  if (!result.accepted || !result.diagnostics.empty() || !result.value)
    return "refused";
  return dump(r, *result.value);
}

void expect(const char* engine, const Case& c, const std::string& got) {
  if (got != c.tree)
    std::printf("%s: '%.*s' reads %s, not %.*s\n", engine,
                static_cast<int>(c.text.size()), c.text.data(), got.c_str(),
                static_cast<int>(c.tree.size()), c.tree.data());
  CHECK(got == c.tree);
}

template <std::size_t N, class MakeLl, class MakeLlReflection>
void expect_cases(const Case (&cases)[N], MakeLl makeLl,
                  MakeLlReflection makeLlr, std::string_view prefix = {}) {
  const auto ll = makeLl();
  const auto llr = makeLlr();
  for (const Case& c : cases) {
    const std::string text = std::string(prefix) + std::string(c.text);
    expect("descent", c, read(*ll, *llr, text));
  }
}

}  // namespace

void run() {
  namespace g = tllexpressions::generated;
  expect_cases(kSum, g::Ll::create_parser, g::Ll::create_ast_reflection,
               "loop:sum:");
  expect_cases(kLadder, g::Ll::create_parser, g::Ll::create_ast_reflection,
               "loop:ladder:");
  expect_cases(kVia, g::Ll::create_parser, g::Ll::create_ast_reflection,
               "loop:via:");
  expect_cases(kMixed, g::Ll::create_parser, g::Ll::create_ast_reflection,
               "loop:mixed:");
}

}  // namespace infix_loop_cases
