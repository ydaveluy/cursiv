// A broken document is repaired as the table engine repairs it: the
// diagnostics and the tree agree, down to what they name. Each text is the
// smallest of a kind where the descent used to destroy more of the document
// than the table engine does, or to name another repair for it.

#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "declsll/generated/DeclsLl/Module.hpp"
#include "declslr/generated/DeclsLr/Module.hpp"
#include "parityll/generated/Ll/Module.hpp"
#include "paritylr/generated/Lr/Module.hpp"

namespace {

void agree(const cursiv::Parser& ll, const cursiv::Parser& lr,
           const cursiv::AstReflection& llr, const cursiv::AstReflection& lrr,
           std::string_view text, bool repaired = true) {
  const auto a = ll.parse(text);
  const auto b = lr.parse(text);
  CHECK(a.accepted && b.accepted);
  CHECK(a.cst().recovered() == repaired && b.cst().recovered() == repaired);
  CHECK(a.diagnostics.empty() == !repaired &&
        b.diagnostics.empty() == !repaired);
  CHECK(a.value != nullptr && b.value != nullptr);
  bool alike = a.diagnostics.size() == b.diagnostics.size();
  for (std::size_t i = 0; alike && i < a.diagnostics.size(); ++i) {
    const auto& x = a.diagnostics[i];
    const auto& y = b.diagnostics[i];
    alike = x.kind == y.kind && x.begin == y.begin && x.end == y.end &&
            x.element == y.element && x.also == y.also;
  }
  if (!alike) {
    std::fprintf(stderr, "repair parity [%.*s]\n",
                 static_cast<int>(text.size()), text.data());
    const auto notes = [](const char* engine, const auto& result) {
      for (const auto& d : result.diagnostics)
        std::fprintf(stderr, "  %s: kind=%u span=[%u,%u) element=%u also=%zu\n",
                     engine, static_cast<unsigned>(d.kind), d.begin, d.end,
                     d.element, d.also.size());
    };
    notes("LL", a);
    notes("LR", b);
  }
  CHECK(alike);
  if (a.value == nullptr || b.value == nullptr) return;
  std::string x, y;
  cursiv::test::ast_of(llr, *a.value, x);
  cursiv::test::ast_of(lrr, *b.value, y);
  if (x != y)
    std::fprintf(stderr, "tree parity [%.*s]\n  LL: %s\n  LR: %s\n",
                 static_cast<int>(text.size()), text.data(), x.c_str(),
                 y.c_str());
  CHECK(x == y);
}

// Missing operands and delimiters, a word read again, or text dropped between
// statements all leave the following statement in the same tree. These also
// pin what the repair calls its missing terminal, and any tied alternatives.
void a_repair_keeps_the_following_statement() {
  const auto ll = tpar::generated::Ll::create_parser();
  const auto lr = tpar::generated::Lr::create_parser();
  const auto llr = tpar::generated::Ll::create_ast_reflection();
  const auto lrr = tpar::generated::Lr::create_ast_reflection();
  for (const std::string_view text :
       {"show x as 2;", "show x as ; show y;", "let a = 1 + ; show b;",
        "let a = (1 + 2; show b;", "let a = f(1 2); show b;",
        "let a = f(1,); show b;", "let a = 1 ? + 2; show b;",
        "export lt a = 1; show b;", "show x y; let b = 2;",
        "let a = 1; @ show b;", "let a = 1; /* gap */ @ show b;",
        "let a = f(1, 2", "show x as", "export", "let a = (1, 2);",
        "show (1, 2);", "let a = f(1,)!; show a;",
        "let a = f(1, @2, 3)!; show a;", "let a = (; s~ow a;", "(;s~ow"})
    agree(*ll, *lr, *llr, *lrr, text);
  for (const std::string_view text :
       {"", "let a = 1; show b;", "show let, export;",
        "export let a = f(1, 2); /* gap */ show a as b;"})
    agree(*ll, *lr, *llr, *lrr, text, false);
}

// A prediction can discover a fault far beyond a choice's common prefix.
// Reading a few clean tokens there must not settle the choice before recovery
// reaches the missing semicolon, inventing a postfix operator along the way.
void a_late_fault_does_not_commit_an_optional_postfix() {
  const auto ll = tpar::generated::Ll::create_parser();
  const auto lr = tpar::generated::Lr::create_parser();
  const auto llr = tpar::generated::Ll::create_ast_reflection();
  const auto lrr = tpar::generated::Lr::create_ast_reflection();
  std::string text = "let a = f(";
  for (unsigned i = 0; i < 80; ++i) {
    if (i != 0) text += ',';
    text += '1';
  }
  text += ')';
  agree(*ll, *lr, *llr, *lrr, text);
}

// A token no construct takes at the head of a statement, or in front of what
// closes one: the repair is one insertion or one replacement, and the text
// behind it is read.
void a_stray_token_costs_one_repair() {
  const auto ll = tpar::generated::Ll::create_parser();
  const auto lr = tpar::generated::Lr::create_parser();
  const auto llr = tpar::generated::Ll::create_ast_reflection();
  const auto lrr = tpar::generated::Lr::create_ast_reflection();
  for (const std::string_view text :
       {",2;", "as;", "+2;", "y,", ")2;", "w,", "a,", "a 2", "@2;", "/2;",
        "xport;", "w 2", "le;", "et;", ";2;", "/y;", "\"2;"})
    agree(*ll, *lr, *llr, *lrr, text);
}

// Declarations that open alike and part on their second word: a misspelling of
// it is read again, not made up for by a construct invented in front of it.
void a_misspelt_second_word_is_read_again() {
  const auto ll = tpar::generated::DeclsLl::create_parser();
  const auto lr = tpar::generated::DeclsLr::create_parser();
  const auto llr = tpar::generated::DeclsLl::create_ast_reflection();
  const auto lrr = tpar::generated::DeclsLr::create_ast_reflection();
  for (const std::string_view text :
       {"pub strcut a {}", "pub strut a {}", "pub struct a {} pub strcut b {}"})
    agree(*ll, *lr, *llr, *lrr, text);
}

}  // namespace

int main() {
  a_stray_token_costs_one_repair();
  a_misspelt_second_word_is_read_again();
  a_repair_keeps_the_following_statement();
  a_late_fault_does_not_commit_an_optional_postfix();
  return cursiv::test::report("LlRecoveryParity_test");
}
