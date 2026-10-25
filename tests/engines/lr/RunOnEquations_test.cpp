// A pattern that may run on through the next equation's `=` keeps every
// reading of a document alive to its last token. The tree read is the one the
// election ranks first, however long the document.

#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/lr/RunOnEquationsGrammar.hpp"
#include "runonlr/generated/Lr/Module.hpp"

namespace {

std::string equations(std::string_view form, unsigned n) {
  std::string text(form);
  for (unsigned i = 0; i < n; ++i) text += " a = b";
  return text;
}

// How the equations group: the definitions, and how many operators each
// pattern runs on through.
struct Shape {
  std::vector<unsigned> runs;
  bool operator==(const Shape&) const = default;
};

Shape shape_of(const trunon::Doc& doc) {
  Shape shape;
  for (const trunon::Def* def : doc.defs) {
    unsigned run = 0;
    for (const trunon::Pat* p = def->pat; p != nullptr && p->rest != nullptr;
         p = p->rest)
      ++run;
    shape.runs.push_back(run);
  }
  return shape;
}

// The election: at the first `=` the option present comes before absent, so
// `inner` runs one pattern through every operator of the document; `outer`
// writes the short pattern first, and every `=` is its equation's own.
Shape elected(bool inner, unsigned n) {
  return inner ? Shape{{n - 1}} : Shape{std::vector<unsigned>(n, 0)};
}

// A fixed oracle states how the election groups the equations.
void reads_the_elected_tree(bool inner, unsigned n) {
  const std::string text = equations(inner ? "inner" : "outer", n);
  const auto lr = trunon::generated::Lr::create_parser();
  const auto a = lr->parse(text);
  CHECK(a.accepted && a.diagnostics.empty());
  const auto* x = a.value_as<trunon::Doc>();
  CHECK(x != nullptr);
  if (x == nullptr) return;
  CHECK(shape_of(*x) == elected(inner, n));
}

// Small documents, read on paper: `a = b a = b` is one equation whose pattern
// runs on through its first `=`, or two.
void a_short_document_reads_as_the_election_ranks_it() {
  reads_the_elected_tree(true, 1);
  reads_the_elected_tree(true, 2);
  reads_the_elected_tree(true, 3);
  reads_the_elected_tree(false, 1);
  reads_the_elected_tree(false, 2);
  reads_the_elected_tree(false, 3);
}

void a_long_document_reads_as_the_election_ranks_it() {
  for (const bool inner : {true, false})
    for (const unsigned n : {160u, 400u})
      reads_the_elected_tree(inner, n);
}

// Broken equations finish with diagnostics and keep a document tree.
void a_broken_document_is_repaired() {
  const auto lr = trunon::generated::Lr::create_parser();
  for (const std::string_view form : {"outer", "inner"})
    for (const unsigned n : {0u, 1u, 5u, 50u})
      for (const std::string_view tail : {" =", " a =", " a = = b"}) {
        const std::string text = equations(form, n) + std::string(tail);
        const auto r = lr->parse(text);
        CHECK(r.accepted && !r.diagnostics.empty());
        CHECK(r.value_as<trunon::Doc>() != nullptr);
      }
}

}  // namespace

int main() {
  a_short_document_reads_as_the_election_ranks_it();
  a_long_document_reads_as_the_election_ranks_it();
  a_broken_document_is_repaired();
  return cursiv::test::report("RunOnEquations_test");
}
