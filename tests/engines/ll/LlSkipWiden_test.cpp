#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "skipwidenll/generated/Ll/Module.hpp"

namespace {

struct Case {
  std::string_view text;
  std::string ast;
  std::string hidden = {};
};

// The tree and the texts the CST keeps as hidden trivia, each followed by `|`.
template <class Parser, class Reflection>
std::string read(Parser& p, Reflection& r, std::string_view text) {
  const auto result = p->parse(text);
  CHECK(result.accepted && result.diagnostics.empty());
  std::string out;
  if (result.value) cursiv::test::ast_of(*r, *result.value, out);
  out += " hidden:";
  for (cursiv::NodeId i = 0; i < result.cst().size(); ++i) {
    const auto& n = result.cst()[i];
    if (n.hidden)
      out += std::string(text.substr(n.begin, n.end - n.begin)) + '|';
  }
  return out;
}

// LL reads every text as the case says.
template <class Ll, class LlAst>
void expect_cases(Ll ll, LlAst llr,
           std::initializer_list<Case> cases) {
  for (const Case& k : cases) {
    const std::string want = k.ast + " hidden:" + k.hidden;
    const auto a = read(ll, llr, k.text);
    if (a != want)
      std::cerr << k.text << "\nwant: " << want << "\nLL: " << a
                << '\n';
    CHECK(a == want);
  }
}

std::string numbers(unsigned n) {
  std::string out = "Array{";
  for (unsigned i = 0; i < n; ++i)
    out += "items" + std::to_string(i) + "=Number{} ";
  return out + "} ";
}

std::string arrays(std::initializer_list<unsigned> sizes) {
  std::string out = "Doc{";
  unsigned i = 0;
  for (const unsigned n : sizes)
    out += "arrays" + std::to_string(i++) + "=" + numbers(n);
  return out + "}";
}

std::string scalars(unsigned n) {
  std::string out;
  for (unsigned i = 0; i < n; ++i)
    out += "items" + std::to_string(i) + "=Scalar{} ";
  return out;
}

}  // namespace

int main() {
  using namespace tllwiden::generated;
  expect_cases(Ll::create_parser(), Ll::create_ast_reflection(),
        {{"[1, 2]", arrays({2})},
         {"[1,\n 2]", arrays({2})},
         {"[\n1\n]", arrays({1})},
         {"[1, # note\n 2]", arrays({2}), "# note|"},
         {"[1]\n[2]", arrays({1, 1})},
         {"[1]\n\n[2]", arrays({1, 1})},
         {"[1,\n2]\n[3, # c\n4]", arrays({2, 2}), "# c|"}});
  const std::string flat = "Entry{value=Scalar{} } ";
  const std::string list2 = "Entry{value=List{" + scalars(2) + "} } ";
  const auto file = [](const std::string& a, const std::string& b = {}) {
    return "File{entries0=" + a + (b.empty() ? "" : "entries1=" + b) + "}";
  };
  expect_cases(Ll::create_parser(), Ll::create_ast_reflection(),
        {{"a=1", file(flat)},
         {"a=[1,\n2]\nb=3", file(list2, flat)},
         {"a=[1, # c\n2,\n]\nb=3", file(list2, flat), "# c|"},
         {"a=[[1],\n[2]] # tail\nb=3",
          file("Entry{value=List{items0=List{" + scalars(1) + "} items1=List{" +
                   scalars(1) + "} } } ",
               flat)}});
  return cursiv::test::report("LlSkipWiden_test");
}
