#pragma once
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
namespace tllswiftwidth {
struct Raw : cursiv::Extends<cursiv::AstNode> {
  string_view opening;
};
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<Raw*> values;
};
struct Base : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Open : terminal<some(lit<"#">) + lit<"\"\"\"">> {};
  struct Close : terminal<lit<"\"\"\""> + some(lit<"#">)> {};
  struct Text : terminal<some(cr<R"(^")">) | (lit<"\""> + option(lit<"\"">))> {};
  struct Value : rule<!lit<"z"> + create<Raw>(assign<&Raw::opening>(ref<Open>) +
                                              many(ref<Text>) + ref<Close>)> {};
  struct GuardedValue
      : rule<create<Raw>(assign<&Raw::opening>(ref<Open>) + many(ref<Text>) +
                         !lit<"z"> + ref<Close>)> {};
  struct Root
      : rule<create<Document>(
            many(append<&Document::values>(ref<Value>) |
                 (lit<"g"> + append<&Document::values>(ref<GuardedValue>))) +
            eof)> {};
  using entry = Root;
  using ignore = WS;
};
struct Ll : Base {};
struct Lr : Base {};
}  // namespace tllswiftwidth
