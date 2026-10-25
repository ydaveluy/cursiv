// How the election reads a left recursion that runs through other rules: as
// the loop of the rule a call starts with, whatever else calls them. The table
// reads every cycle; the descent reads the ones with an arm that only calls on.
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/ll/gen/ll_build.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/IndirectLoopGrammar.hpp"
#include "indirectloopcycle/generated/CycleLr/Module.hpp"
#include "semanticll/generated/Ll/Module.hpp"

namespace {

struct Case {
  std::string_view text, tree;
};

constexpr Case kCycle[] = {
    {"mutual/a a x", "Doc(Tx(Ba))"},
    {"mutual/a a x y x", "Doc(Tx(Ty(Tx(Ba))))"},
    {"mutual/b a x y", "Doc(Ty(Bax))"},
    {"mutual/b a x y x y", "Doc(Ty(Tx(Ty(Bax))))"},
    {"mutual/ab:a a x", "Doc(Tx(Ba))"},
    {"mutual/ab:a a x y x", "Doc(Tx(Ty(Tx(Ba))))"},
    {"mutual/ab:b a x y", "Doc(Ty(Bax))"},
    {"mutual/ab:b a x y x y", "Doc(Ty(Tx(Ty(Bax))))"},
    {"mutual/ba:b a x y", "Doc(Ty(Bax))"},
    {"mutual/ba:b a x y x y", "Doc(Ty(Tx(Ty(Bax))))"},
    {"mutual/ba:a a x", "Doc(Tx(Ba))"},
    {"mutual/ba:a a x y x", "Doc(Tx(Ty(Tx(Ba))))"},

    {"mutual-a/a a x", "Doc(Bax)"},
    {"mutual-a/a a x y x", "Doc(Tx(Ty(Bax)))"},
    {"mutual-a/b a x y", "Doc(Ty(Bax))"},
    {"mutual-a/b a x y x y", "Doc(Ty(Tx(Ty(Bax))))"},
    {"mutual-b/a a x", "Doc(Tx(Ba))"},
    {"mutual-b/a a x y x", "Doc(Tx(Ty(Tx(Ba))))"},
    {"mutual-b/b a x y", "Doc(Ty(Tx(Ba)))"},
    {"mutual-b/b a x y x y", "Doc(Ty(Tx(Ty(Tx(Ba)))))"},
    {"mutual-ab/a a x", "Doc(Bax)"},
    {"mutual-ab/a a x y x", "Doc(Tx(Ty(Bax)))"},
    {"mutual-ab/b a x y", "Doc(Ty(Tx(Ba)))"},
    {"mutual-ab/b a x y x y", "Doc(Ty(Tx(Ty(Tx(Ba)))))"},

    {"ring/a a y x", "Doc(Tx(Ty(Bc)))"},
    {"ring/b a y", "Doc(Ty(Bc))"},
    {"ring/c a y x z", "Doc(Tz(Tx(Bay)))"},
    {"ring-b/a a y x", "Doc(Tx(Bay))"},
    {"ring-b/b a y", "Doc(Bay)"},
    {"ring-b/c a y x z", "Doc(Tz(Tx(Bay)))"},

    {"rungs/a a w x", "Doc(Tx(Tw(Bc)))"},
    {"rungs-b/a a w x", "Doc(Tx(Tv(Bd)))"},
    {"share/a a e x", "Doc(Tp(Be))"},
    {"diamond/a a x y x", "Doc(Tx(Ty(Bax)))"},
    {"diamond-s/a a x y x", "Doc(Tz(Tr(Bax)))"},

    {"hop:a a x", "Doc(Tx(Ba))"},
    {"hop:a a x y x", "Doc(Tx(Ty(Tx(Ba))))"},
    {"hop:b a x y", "Doc(Ty(Bax))"},
    {"hop:b a x y x y", "Doc(Ty(Tx(Ty(Bax))))"},

    {"paren/a ( a x y )", "Doc(Tp(Ty(Bax)))"},
    {"paren/a ( a x y ) y x", "Doc(Tx(Ty(Tp(Ty(Bax)))))"},
    {"paren/a ( a x y x y )", "Doc(Tp(Ty(Tx(Ty(Bax)))))"},
    {"paren/a a x", "Doc(Tx(Ba))"},
    {"paren/b a x y", "Doc(Ty(Bax))"},
    {"paren/b ( a x y ) y", "Doc(Ty(Tp(Ty(Bax))))"},

    {"sub/a b y x", "Doc(Tx(Ty(Bb)))"},
    {"sub/a a z x", "Doc(Tx(Tz(Ba)))"},
    {"sub/a b y y x", "Doc(Tx(Ty(Ty(Bb))))"},
    {"sub/a a z y x", "Doc(Tx(Ty(Tz(Ba))))"},
    {"sub/a a", "Doc(Ba)"},
    {"sub/b b y", "Doc(Ty(Bb))"},
    {"sub/b a z", "Doc(Tz(Ba))"},
    {"sub/b a z y", "Doc(Ty(Tz(Ba)))"},
    {"sub/b b y y", "Doc(Ty(Ty(Bb)))"},
    {"sub-amb/a b x x", "Doc(Tx(Ty(Bb)))"},
    {"sub-amb/a b x x x", "Doc(Tx(Ty(Ty(Bb))))"},
    {"sub-amb/a a x x x", "Doc(Tx(Ty(Tz(Ba))))"},
    {"sub-amb/a b x x x x", "Doc(Tx(Ty(Ty(Ty(Bb)))))"},
    {"sub-amb/b b x x", "Doc(Ty(Ty(Bb)))"},
    {"sub-amb/b b x x x", "Doc(Ty(Ty(Ty(Bb))))"},
    {"sub-amb/b a x x", "Doc(Ty(Tz(Ba)))"},

    {"valued/a a x", "Doc(Val[A(B(a)x)])"},
    {"valued/a a x y x", "Doc(Val[A(B(A(B(a)x)y)x)])"},
    {"valued/b a x y", "Doc(Val[B(A(ax)y)])"},
    {"valued/b a x y x y", "Doc(Val[B(A(B(A(ax)y)x)y)])"},
    {"valued/ab:a a x", "Doc(Val[A(B(a)x)])"},
    {"valued/ab:a a x y x", "Doc(Val[A(B(A(B(a)x)y)x)])"},
    {"valued/ab:b a x y", "Doc(Val[B(A(ax)y)])"},
    {"valued/ab:b a x y x y", "Doc(Val[B(A(B(A(ax)y)x)y)])"},
};

constexpr Case kTwin[] = {
    {"twin/a b y y", "Doc(Ty(Ty(Bb)))"},
    {"twin/a a y y", "Doc(Ty(Tz(Ba)))"},
    {"twin/a b y y y", "Doc(Ty(Ty(Ty(Bb))))"},
    {"twin/b b y y", "Doc(Ty(Ty(Bb)))"},
    {"twin/b a y y", "Doc(Ty(Tz(Ba)))"},
    {"twin/b a y y y", "Doc(Ty(Ty(Tz(Ba))))"},
};

constexpr Case kShared[] = {
    {"via/a a x", "Doc(Tx(Ba))"},
    {"via/a a x x", "Doc(Tx(Tx(Ba)))"},
    {"via/w a x", "Doc(Tx(Ba))"},
    {"via/aw:a a x", "Doc(Tx(Ba))"},
    {"via/aw:a a x x", "Doc(Tx(Tx(Ba)))"},
    {"via/aw:w a x", "Doc(Tx(Ba))"},
    {"via/aw:w a x x", "Doc(Tx(Bax))"},
    {"via/wa:w a x", "Doc(Tx(Ba))"},
    {"via-a/a a x", "Doc(Bax)"},
    {"via-a/a a x x", "Doc(Tx(Bax))"},
    {"via-a/w a x", "Doc(Tx(Ba))"},
    {"via-a/w a x x", "Doc(Tx(Bax))"},
    {"via-w/a a x", "Doc(Tx(Ba))"},
    {"via-w/a a x x", "Doc(Tx(Tx(Ba)))"},
    {"via-w/w a x", "Doc(Tx(Ba))"},
    {"via-w/w a x x", "Doc(Tx(Tx(Ba)))"},
    {"via-aw/a a x", "Doc(Bax)"},
    {"via-aw/a a x x", "Doc(Tx(Bax))"},
    {"via-aw/w a x", "Doc(Tx(Ba))"},

    {"turn-first/a a x", "Doc(Bax)"},
    {"turn-first/a a x x", "Doc(Tx(Bax))"},
    {"turn-first/w a x", "Doc(Tx(Ba))"},
    {"turn-first/w a x x", "Doc(Tx(Bax))"},
    {"turn-last/a a x", "Doc(Bax)"},
    {"turn-last/a a x x", "Doc(Tx(Bax))"},
    {"turn-last/w a x", "Doc(Tx(Ba))"},
    {"turn-last/w a x x", "Doc(Tx(Bax))"},

    {"rung/a a x", "Doc(Tx(Ba))"},
    {"rung/b a x", "Doc(Bax)"},
    {"rung/b a x x", "Doc(Tx(Bax))"},
    {"rung/ab:a a x", "Doc(Tx(Ba))"},
    {"rung/ba:b a x", "Doc(Bax)"},
    {"rung/ba:b a x x", "Doc(Tx(Bax))"},
    {"rung/ba:a a x x", "Doc(Tx(Tx(Ba)))"},
    {"rung-b/a a x", "Doc(Tx(Ba))"},
    {"rung-b/a a x x", "Doc(Tx(Tx(Ba)))"},
    {"rung-b/b a x", "Doc(Tx(Ba))"},
    {"rung-b/b a x x", "Doc(Tx(Tx(Ba)))"},

    {"chain/a a x", "Doc(Tx(Ba))"},
    {"chain/a a x x", "Doc(Tx(Tx(Ba)))"},
    {"chain/a b x", "Doc(Tx(Bb))"},
    {"chain/b a x", "Doc(Tx(Ba))"},
    {"chain/b b x", "Doc(Tx(Bb))"},
    {"chain/c a x", "Doc(Tx(Ba))"},
    {"chain/c b x", "Doc(Tx(Bb))"},

    {"ladder/e - a !", "Doc(Post(Neg(Ba)))"},
    {"ladder/n - a !", "Doc(Neg(Post(Ba)))"},
    {"ladder/en:n - a !", "Doc(Neg(Post(Ba)))"},
    {"ladder/ne:e - a !", "Doc(Post(Neg(Ba)))"},
    {"ladder-b/e - a ! !", "Doc(Post(Neg(Post(Ba))))"},
    {"ladder-b/n - a ! !", "Doc(Neg(Post(Post(Ba))))"},

    {"via/w a x x", "Doc(Tx(Bax))"},
    {"via/wa:w a x x", "Doc(Tx(Bax))"},
    {"via/wa:a a x", "Doc(Tx(Ba))"},
    {"via/wa:a a x x", "Doc(Tx(Tx(Ba)))"},
    {"via-aw/w a x x", "Doc(Tx(Tx(Ba)))"},
    {"rung/a a x x", "Doc(Tx(Tx(Ba)))"},
    {"rung/ab:a a x x", "Doc(Tx(Tx(Ba)))"},
    {"rung/ab:b a x", "Doc(Bax)"},
    {"rung/ab:b a x x", "Doc(Tx(Bax))"},
    {"rung/ba:a a x", "Doc(Tx(Ba))"},
    {"chain/b a x x", "Doc(Tx(Bax))"},
    {"chain/c a x x", "Doc(Tx(Bax))"},

    {"fparen/a ( a x x )", "Doc(Tp(Tx(Bax)))"},
    {"fparen/a ( a x x ) x", "Doc(Tx(Tp(Tx(Bax))))"},
    {"fparen/a a x x", "Doc(Tx(Tx(Ba)))"},
    {"fparen/w a x x", "Doc(Tx(Bax))"},
    {"fparen/w ( a x x ) x", "Doc(Tx(Tp(Tx(Bax))))"},
    {"fparen/w a x", "Doc(Tx(Ba))"},

    {"fsub/a b", "Doc(Bb)"},
    {"fsub/a a", "Doc(Ba)"},
    {"fsub/a b y", "Doc(Ty(Bb))"},
    {"fsub/a a z", "Doc(Tz(Ba))"},
    {"fsub/a b z", "Doc(Tz(Bb))"},
    {"fsub/a b y y", "Doc(Ty(Ty(Bb)))"},
    {"fsub/a a z y", "Doc(Ty(Tz(Ba)))"},
    {"fsub/b b y", "Doc(Ty(Bb))"},
    {"fsub/b a z", "Doc(Tz(Ba))"},
    {"fsub/b a z y", "Doc(Ty(Tz(Ba)))"},
    {"fsub/ab:a a z y", "Doc(Ty(Tz(Ba)))"},
    {"fsub/ab:b a z y", "Doc(Ty(Tz(Ba)))"},

    {"fsub-base/a b y", "Doc(Bby)"},
    {"fsub-base/a b y y", "Doc(Ty(Bby))"},
    {"fsub-base/a b y z", "Doc(Tz(Bby))"},
    {"fsub-base/b b y", "Doc(Bby)"},
    {"fsub-base/b b y y", "Doc(Ty(Bby))"},
    {"fsub-base/b b y z", "Doc(Tz(Bby))"},

    {"twin-bases/a a", "Doc(Ba)"},
    {"twin-bases/a a y", "Doc(Ty(Ba))"},

    {"fvalued/a a x", "Doc(Val[A(B(A(B(a))x))])"},
    {"fvalued/aw:a a x", "Doc(Val[A(B(A(B(a))x))])"},
    {"fvalued/w a x", "Doc(Val[B(A(B(a))x)])"},
    {"fvalued/aw:w a x", "Doc(Val[B(A(B(a))x)])"},
    {"fvalued/w a x x", "Doc(Val[B(A(ax)x)])"},
    {"fvalued/aw:w a x x", "Doc(Val[B(A(ax)x)])"},
};

struct Alike {
  std::span<const std::string_view> heads, texts;
};
constexpr std::string_view kMutualA[] = {"mutual/a", "mutual/ab:a",
                                         "mutual/ba:a", "hop:a"};
constexpr std::string_view kMutualB[] = {"mutual/b", "mutual/ab:b",
                                         "mutual/ba:b", "hop:b"};
constexpr std::string_view kMutualATexts[] = {"a x", "a x y x"};
constexpr std::string_view kMutualBTexts[] = {"a x y", "a x y x y"};
constexpr Alike kAlikeCycle[] = {{kMutualA, kMutualATexts},
                                 {kMutualB, kMutualBTexts}};
constexpr std::string_view kViaW[] = {"via/w", "via/aw:w", "via/wa:w"};
constexpr std::string_view kViaA[] = {"via/a", "via/aw:a", "via/wa:a"};
constexpr std::string_view kViaTexts[] = {"a x", "a x x", "a x x x"};
constexpr std::string_view kRungA[] = {"rung/a", "rung/ab:a", "rung/ba:a"};
constexpr std::string_view kRungB[] = {"rung/b", "rung/ab:b", "rung/ba:b"};
constexpr std::string_view kRungTexts[] = {"a x", "a x x", "a x x x"};
constexpr std::string_view kFSubA[] = {"fsub/a", "fsub/ab:a"};
constexpr std::string_view kFSubB[] = {"fsub/b", "fsub/ab:b"};
constexpr std::string_view kFSubTexts[] = {"b y", "a z y", "a z y y"};
constexpr Alike kAlikeShared[] = {{kViaW, kViaTexts},   {kViaA, kViaTexts},
                                  {kRungA, kRungTexts}, {kRungB, kRungTexts},
                                  {kFSubA, kFSubTexts}, {kFSubB, kFSubTexts}};

std::string dump(const cursiv::AstReflection& r, const cursiv::AstNode& n) {
  std::string kids;
  r.visitMembers(n, [&](std::string_view, std::uint32_t, cursiv::AstNode& kid) {
    kids += dump(r, kid);
  });
  const std::string name(r.getTypeMetaData(n.type).name);
  if (name == "Val")
    return name + '[' +
           std::string(static_cast<const tindirectloop::Val&>(n).text) + ']';
  return kids.empty() ? name : name + '(' + kids + ')';
}

std::string read(const cursiv::Parser& parser, const cursiv::AstReflection& r,
                 std::string_view text, std::string_view prefix = {}) {
  const std::string source = std::string(prefix) + std::string(text);
  const auto result = parser.parse(source);
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

template <class Read>
void alike(const char* engine, std::span<const Alike> groups, Read&& read_one) {
  for (const Alike& g : groups)
    for (const std::string_view text : g.texts) {
      const std::string first =
          read_one(std::string(g.heads.front()) + ' ' + std::string(text));
      CHECK(first != "refused");
      for (const std::string_view head : g.heads) {
        const std::string at = std::string(head) + ' ' + std::string(text);
        const std::string got = read_one(at);
        if (got != first)
          std::printf("%s: '%s' reads %s, not %s as '%.*s %.*s' does\n", engine,
                      at.c_str(), got.c_str(), first.c_str(),
                      static_cast<int>(g.heads.front().size()),
                      g.heads.front().data(), static_cast<int>(text.size()),
                      text.data());
        CHECK(got == first);
      }
    }
}

}  // namespace

int main() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  const auto llr = tllsemantic::generated::Ll::create_ast_reflection();
  const auto table = tindirectloop::generated::CycleLr::create_parser();
  const auto tabler =
      tindirectloop::generated::CycleLr::create_ast_reflection();
  for (const Case& c : kCycle)
    expect("table", c, read(*table, *tabler, c.text));
  for (const Case& c : kShared) {
    expect("descent", c, read(*ll, *llr, c.text, "loop:"));
  }
  alike("table", kAlikeCycle,
        [&](const std::string& text) { return read(*table, *tabler, text); });
  alike("descent", kAlikeShared,
        [&](const std::string& text) { return read(*ll, *llr, text, "loop:"); });
  for (const Case& c : kTwin) {
    expect("descent", c, read(*ll, *llr, c.text, "loop:"));
  }
  const auto refused = cursiv::ll_build_program(
      cursiv::LlLowering<tindirectloop::Cycle>::lower());
  CHECK(refused.fault.code == cursiv::GrammarFaultCode::leftRecursion);
  return cursiv::test::report("LlIndirectLoop_test");
}
