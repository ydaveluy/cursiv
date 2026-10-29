// An ambiguous left recursion reads as the loop it describes: a base, then
// one turn at a time, whatever order its arms are written in.
#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"

#include "semanticll/generated/Ll/Module.hpp"

namespace ll_fold_order_cases {

namespace {

struct Case {
  std::string_view text, tree;
};

constexpr Case kCases[] = {
    {"s1 a x", "Doc{value=Pair{} }"},
    {"s2 a x", "Doc{value=Pair{} }"},
    {"s3 a x", "Doc{value=Pair{} }"},
    {"p1 - a !", "Doc{value=Neg{inner=Post{inner=Leaf{} } } }"},
    {"p2 - a !", "Doc{value=Neg{inner=Post{inner=Leaf{} } } }"},
    {"o1 a x x", "Doc{value=Turn{inner=Leaf{} tail=Tail{} } }"},
    {"o2 a x x", "Doc{value=Turn{inner=Leaf{} tail=Tail{} } }"},
    {"m1 a x x x",
     "Doc{value=Turn{inner=Leaf{} tails0=Tail{} tails1=Tail{} } }"},
    {"m2 a x x x",
     "Doc{value=Turn{inner=Leaf{} tails0=Tail{} tails1=Tail{} } }"},
    {"t1 a , a , a , a", "Doc{value=Short{inner=Long{inner=Leaf{} } } }"},
    {"t2 a , a , a , a",
     "Doc{value=Short{inner=Short{inner=Short{inner=Leaf{} } } } }"},
    {"i1 a x", "Doc{value=Pair{} }"},
    {"i2 a x", "Doc{value=Pair{} }"},
};

std::string read(const cursiv::Parser& parser, const cursiv::AstReflection& r,
                 std::string_view text) {
  const std::string source = "fold:" + std::string(text);
  const auto result = parser.parse(source);
  if (!result.accepted || !result.diagnostics.empty() || !result.value)
    return "refused";
  std::string out;
  cursiv::test::ast_of(r, *result.value, out);
  return out;
}

}  // namespace

void run() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  const auto llr = tllsemantic::generated::Ll::create_ast_reflection();
  for (const Case& c : kCases) CHECK(read(*ll, *llr, c.text) == c.tree);
}

}  // namespace ll_fold_order_cases
