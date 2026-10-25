#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/GuardedClimbGrammar.hpp"
#include "guardedclimbll/generated/Ll/Module.hpp"
#include "guardedclimblr/generated/Lr/Module.hpp"
int main() {
  auto ll = tguardedclimb::generated::Ll::create_parser();
  auto lr = tguardedclimb::generated::Lr::create_parser();
  for (const cursiv::Parser* parser : {ll.get(), lr.get()})
    for (std::string_view text :
         {"f(g(0))", "x op", "x op y", "x match {case y => y}",
          "x match {case s\"$a${b}\" => a}", "for (x <- ys) yield x",
          "for (x ← ys) yield x"}) {
      const auto r = parser->parse(text);
      bool ok = r.accepted && !r.cst().recovered() && r.diagnostics.empty() &&
                r.value;
      if (text == "x op") {
        auto* p = cursiv::ast_cast<tguardedclimb::Postfix>(r.value);
        auto* operand =
            p ? cursiv::ast_cast<tguardedclimb::Ident>(p->operand) : nullptr;
        ok &= p && p->name == "op" && operand && operand->name == "x";
      }
      if (text == "x op y") {
        auto* p = cursiv::ast_cast<tguardedclimb::Binary>(r.value);
        auto* left =
            p ? cursiv::ast_cast<tguardedclimb::Ident>(p->left) : nullptr;
        auto* right =
            p ? cursiv::ast_cast<tguardedclimb::Ident>(p->right) : nullptr;
        ok &= p && p->op == "op" && left && left->name == "x" && right &&
              right->name == "y";
      }
      if (text.starts_with("f(")) {
        auto* p = cursiv::ast_cast<tguardedclimb::Call>(r.value);
        ok &= p && p->args.size() == 1 &&
              cursiv::ast_cast<tguardedclimb::Call>(p->args[0]);
      }
      if (text.starts_with("x match")) {
        auto* p = cursiv::ast_cast<tguardedclimb::Match>(r.value);
        auto* operand =
            p ? cursiv::ast_cast<tguardedclimb::Ident>(p->operand) : nullptr;
        auto* body =
            p ? cursiv::ast_cast<tguardedclimb::Ident>(p->body) : nullptr;
        ok &= p && operand && operand->name == "x" && body && p->pattern &&
              p->operand->parent == p && p->pattern->parent == p &&
              p->body->parent == p;
        if (text == "x match {case y => y}") {
          auto* pattern =
              p ? cursiv::ast_cast<tguardedclimb::Bind>(p->pattern) : nullptr;
          ok &= pattern && pattern->name == "y" && body && body->name == "y";
        }
        if (text.find("s\"") != text.npos)
          ok &= p && cursiv::ast_cast<tguardedclimb::Pattern>(p->pattern) &&
                cursiv::ast_cast<tguardedclimb::Pattern>(p->pattern)
                        ->parts.size() == 2;
      }
      if (text.starts_with("for")) {
        auto* p = cursiv::ast_cast<tguardedclimb::For>(r.value);
        ok &= p && p->generators.size() == 1 && p->generators[0]->source &&
              p->body && p->generators[0]->pattern;
      }
      CHECK(ok);
    }
  // A blank line behind a word operator ends the expression; one break does
  // not.
  for (const cursiv::Parser* parser : {ll.get(), lr.get()}) {
    CHECK(parser->parse("x op\ny").diagnostics.empty());
    CHECK(!parser->parse("x op\n\ny").diagnostics.empty());
  }
  return cursiv::test::report("GuardedClimb_test");
}
