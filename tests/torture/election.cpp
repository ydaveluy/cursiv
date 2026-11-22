// How the engine elects among parses: the arm written first, between rules
// and between the alternatives of one, a condition against a shift, the
// dangling else at depth, and a run split two ways. A reduce/reduce conflict
// keeps its fallback.

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/engine/parser/Explorer.hpp"
#include "cursiv/engine/parser/Parse.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/Check.hpp"
#include "election/generated/Election/Module.hpp"
#include "election/generated/Election/Table.hpp"
#include "torture/DeepIfGrammar.hpp"
#include "torture/ElectionGrammar.hpp"

namespace {

namespace ast = telect::ast;
namespace e = telect::generated::Election;
using Stmt = tdang::ast::Stmt;

const cursiv::Parser& parser() {
  static const std::unique_ptr<cursiv::Parser> instance = e::createParser();
  return *instance;
}

bool clean(const cursiv::ParseResult& r) {
  return r.cst().accepted() && !r.cst().recovered();
}

// One case of the election table, read clean. The tree reads the document in
// place: the caller keeps it alive.
cursiv::ParseResult elect(std::string_view doc) {
  cursiv::ParseResult r = parser().parse(doc);
  CHECK(clean(r));
  return r;
}

const ast::Case* case_of(const cursiv::ParseResult& r) {
  return r.valueAs<ast::Case>();
}

// One cell, both reductions kept: the earlier complete parse is elected.
void rank_elects(std::string_view opener, bool wantX) {
  const std::string doc = std::string(opener) + " a b x";
  const cursiv::ParseResult r = elect(doc);
  const ast::Case* c = case_of(r);
  const ast::Top* top = c != nullptr ? c->top : nullptr;
  CHECK(top != nullptr);
  if (top == nullptr) return;
  CHECK((top->x != nullptr) == wantX);
  CHECK((top->y != nullptr) == !wantX);
  if ((top->x != nullptr) != wantX)
    std::printf("    %.*s elected %s\n", static_cast<int>(opener.size()),
                opener.data(), top->x ? "X" : "Y");
}

// "c a x" completes as Hold(RX("c a")) or "c" Hold(RY("a")): the reduces are
// R's own alternatives, so the conflict survives to the table and the parse
// forks over it.
void runtime_elects(std::string_view opener, bool wantX) {
  const std::string doc = std::string(opener) + " c a x";
  const cursiv::ParseResult r = elect(doc);
  const ast::Case* c = case_of(r);
  const ast::Hold* top = c != nullptr ? c->hold : nullptr;
  CHECK(top != nullptr && top->r != nullptr);
  if (top == nullptr || top->r == nullptr) return;
  const bool gotX = top->r->type == &cursiv::kAstTypeTag<ast::RX>;
  CHECK(gotX == wantX);
  if (gotX != wantX)
    std::printf("    %.*s elected %s\n", static_cast<int>(opener.size()),
                opener.data(), gotX ? "RX" : "RY");
}

// "a b x" reads as X, whose name a condition guards, or as Y: the grammar
// without the condition elects. "x b x" is Y's alone, as the condition
// refuses the name.
void guard_elects(std::string_view opener, bool wantX) {
  for (const auto& [text, x] :
       {std::pair{"a b x", wantX}, std::pair{"x b x", false}}) {
    const std::string doc = std::string(opener) + " " + text;
    const cursiv::ParseResult r = elect(doc);
    const ast::Case* c = case_of(r);
    const ast::Top* top = c != nullptr ? c->top : nullptr;
    CHECK(top != nullptr);
    if (top == nullptr) continue;
    CHECK((top->x != nullptr) == x);
    if ((top->x != nullptr) != x)
      std::printf("    %.*s elected %s on '%s'\n",
                  static_cast<int>(opener.size()), opener.data(),
                  top->x ? "X" : "Y", text);
  }
}

// The dangling else, its arms declared in both orders: `DeepIf`'s, and the
// election's `deepifrev` case.
struct Dangling {
  const char* label;
  std::string_view opener;
  const Stmt* (*root)(const cursiv::ParseResult&);
  // The if a lone else binds: the nearest, or the outermost where the arm
  // with an else is written first.
  bool nearest;
};

const Stmt* deep_if_root(const cursiv::ParseResult& r) {
  return r.valueAs<Stmt>();
}
const Stmt* deep_if_rev_root(const cursiv::ParseResult& r) {
  const ast::Case* c = case_of(r);
  return c != nullptr ? c->stmt : nullptr;
}

constexpr Dangling kDeepIf{"DeepIf   ", "", &deep_if_root, true};
constexpr Dangling kDeepIfRev{"DeepIfRev", "deepifrev", &deep_if_rev_root,
                              false};

// N nested ifs, one condition per level ("c0".."c{N-1}", innermost last),
// then M trailing elses ("e0".."e{M-1}") -- unique labels so a hand walk of
// the tree can name exactly which if a given else bound to.
std::string nested(std::string_view opener, unsigned depth, unsigned elses) {
  std::string s(opener);
  if (!s.empty()) s += ' ';
  for (unsigned k = 0; k < depth; ++k)
    s += "if c" + std::to_string(k) + " then ";
  s += "x";
  for (unsigned k = 0; k < elses; ++k) s += " else e" + std::to_string(k);
  return s;
}

void common_tokens_distinguish_siblings_in_their_stack_context() {
  constexpr std::string_view siblings = "tags <r><a>#</a><b>#</b></r>";
  static unsigned predicted;
  auto tables = e::kTables;
  CHECK(tables.predict != nullptr);
  if (!tables.predict) return;
  tables.predict = [](cursiv::engine::Machine& m) {
    const auto choice = e::kTables.predict(m);
    if (choice.arm != cursiv::engine::kNoStack && m.src[m.tok.begin(m)] == '#') ++predicted;
    return choice;
  };
  cursiv::Cst tree;
  predicted = 0;
  CHECK(parse(tables, siblings, tree) && tree.diagnostics().empty());
  CHECK(predicted != 0);
  const auto both = elect(siblings);
  const auto* root = both.valueAs<ast::Tag>();
  CHECK(root && root->children.size() == 2 && root->children[0]->close == "a" &&
        root->children[1]->close == "b" && root->close == "r");
  const auto one = elect("tags <r><a>#</r>");
  root = one.valueAs<ast::Tag>();
  CHECK(root && root->children.size() == 1 && root->children[0]->close.empty() && root->close == "r");
}

void small_ast_checks() {
  // A lone if: no conflict, no ambiguity to elect.
  {
    const cursiv::ParseResult r = elect("if a then x");
    const Stmt* root = r.valueAs<Stmt>();
    CHECK(root != nullptr);
    if (root == nullptr) return;
    CHECK(root->cond == "a");
    CHECK(root->then_ != nullptr && root->then_->cond == "x");
    CHECK(root->else_ == nullptr);
  }

  // "if a then if b then x else e" reads two ways, parting first at the
  // outer if, which keeps its first arm written: the nearest takes the else.
  const std::string_view text = "if a then if b then x else e";
  {
    const cursiv::ParseResult r = elect(text);
    const Stmt* root = r.valueAs<Stmt>();
    CHECK(root != nullptr && root->cond == "a");
    CHECK(root != nullptr && root->else_ == nullptr);
    const Stmt* inner = root != nullptr ? root->then_ : nullptr;
    CHECK(inner != nullptr && inner->cond == "b");
    CHECK(inner != nullptr && inner->else_ != nullptr &&
          inner->else_->cond == "e");
    CHECK(inner != nullptr && inner->then_ != nullptr &&
          inner->then_->cond == "x");
  }

  // The entry's own extent covers the whole document -- a span check, not
  // just a boolean.
  {
    const cursiv::ParseResult r = elect(text);
    CHECK(r.cst()[r.cst().entryRoot()].begin == 0);
    CHECK(r.cst()[r.cst().entryRoot()].end == text.size());
  }

  // The same text, arms declared in the opposite order: the outer if keeps
  // its first arm, the one with an else.
  {
    const std::string doc =
        std::string(kDeepIfRev.opener) + " " + std::string(text);
    const cursiv::ParseResult r = elect(doc);
    const Stmt* root = kDeepIfRev.root(r);
    CHECK(root != nullptr && root->else_ != nullptr &&
          root->else_->cond == "e");
    CHECK(root != nullptr && root->then_ != nullptr &&
          root->then_->else_ == nullptr);
  }
}

// A valid document nested deep parses to its one tree.
void depth_probe(const Dangling& g, unsigned maxDepth) {
  for (unsigned n = 1; n <= maxDepth; ++n) {
    const std::string text =
        nested(g.opener, n, n);  // N ifs, N elses: forces a bind at every level
    const cursiv::ParseResult r = parser().parse(text);

    // A fully-nested N-if/N-else document has exactly one completion (every
    // if must take its own else, since there are exactly as many of each) --
    // it must read clean.
    CHECK(clean(r));
    if (!clean(r)) {
      std::printf("    %s: depth=%u NOT clean\n", g.label, n);
      continue;
    }

    // The tree must be the one valid nesting: elses were typed
    // innermost-first, so walking outer to inner (c0, c1, ...) reaches them in
    // reverse -- c_k binds e_{n-1-k}.
    const Stmt* cur = g.root(r);
    bool shapeOk = cur != nullptr;
    for (unsigned k = 0; shapeOk && k < n; ++k) {
      const std::string wantCond = "c" + std::to_string(k);
      const std::string wantElse = "e" + std::to_string(n - 1 - k);
      shapeOk = cur->cond == wantCond && cur->else_ != nullptr &&
                cur->else_->cond == wantElse && cur->then_ != nullptr;
      cur = shapeOk ? cur->then_ : nullptr;
    }
    shapeOk = shapeOk && cur != nullptr && cur->cond == "x";
    CHECK(shapeOk);
    if (!shapeOk)
      std::printf("    %s: depth=%u tree MISBOUND despite a clean parse\n",
                  g.label, n);
  }
}

// Stability: N nested ifs, one trailing else, bound to the same if at every
// depth: the election does not move with the depth.
void election_stable_in_depth(const Dangling& g, unsigned maxDepth) {
  for (unsigned depth = 2; depth <= maxDepth; ++depth) {
    const std::string text = nested(g.opener, depth, 1);
    const cursiv::ParseResult r = parser().parse(text);
    CHECK(clean(r));
    if (!clean(r)) continue;
    const Stmt* cur = g.root(r);
    unsigned level = 0;
    while (cur && cur->else_ == nullptr) {
      cur = cur->then_;
      ++level;
    }
    const unsigned want = g.nearest ? depth - 1 : 0;
    CHECK(level == want);
    if (level != want)
      std::printf(
          "    %s: depth=%u else bound at level %u (want %u)\n", g.label,
          depth, level, want);
  }
}

const ast::List* groups_of(const cursiv::ParseResult& r) {
  const ast::Case* c = case_of(r);
  return c != nullptr ? c->list : nullptr;
}

// Overlapping lists: a run of "a"s groupable into pairs or triples -- width
// from partitioning the same run different ways, unlike the dangling-else
// shift/reduce chain.
void overlap() {
  // "aa": only a pair fits -- no ambiguity to elect.
  {
    const cursiv::ParseResult r = elect("overlap aa");
    const ast::List* list = groups_of(r);
    CHECK(list != nullptr && list->groups.size() == 1);
    CHECK(list != nullptr && !list->groups.empty() &&
          list->groups[0]->type == &cursiv::kAstTypeTag<ast::Pair>);
  }

  // "aaa": only a triple fits.
  {
    const cursiv::ParseResult r = elect("overlap aaa");
    const ast::List* list = groups_of(r);
    CHECK(list != nullptr && list->groups.size() == 1);
    CHECK(list != nullptr && !list->groups.empty() &&
          list->groups[0]->type == &cursiv::kAstTypeTag<ast::Triple>);
  }

  // "a" alone: neither group shape fits -- refused, not silently accepted
  // as a partial group.
  CHECK(!clean(parser().parse("overlap a")));

  // "aaaaaa" (6): two genuinely different partitions exist -- three pairs,
  // or two triples. The pair is the arm written first: three pairs.
  {
    const cursiv::ParseResult r = elect("overlap aaaaaa");
    const ast::List* list = groups_of(r);
    CHECK(list != nullptr);
    if (list != nullptr) {
      unsigned consumed = 0;
      bool allPairs = true;
      for (const ast::Group* grp : list->groups) {
        if (grp->type == &cursiv::kAstTypeTag<ast::Pair>) {
          consumed += 2;
        } else if (grp->type == &cursiv::kAstTypeTag<ast::Triple>) {
          consumed += 3;
          allPairs = false;
        }
      }
      CHECK(consumed == 6);  // no "a" lost, none invented
      CHECK(allPairs);
    }
  }

  // "aaaaaaa" (7) admits three distinct group orderings, a reordering
  // ambiguity rather than just a partition count, and the engine reads it
  // clean.
  (void)elect("overlap aaaaaaa");
}

}  // namespace

void torture_election() {
  common_tokens_distinguish_siblings_in_their_stack_context();
  // X declared first: X wins; Y declared first: Y wins.
  rank_elects("rankxy", true);
  rank_elects("rankyx", false);
  // The parses part first at Hold's arms, not R's: Hold's first arm, which
  // reads "c a" as R, wins whatever the order of R's arms.
  runtime_elects("runxy", true);
  runtime_elects("runyx", true);
  // Y's shift goes ahead of Name's reduction in either order.
  guard_elects("guardxy", true);
  guard_elects("guardyx", false);
  // Name and Y reduce side by side: the declared order elects.
  guard_elects("mixedxy", true);
  guard_elects("mixedyx", false);

  small_ast_checks();
  election_stable_in_depth(kDeepIf, 24);
  election_stable_in_depth(kDeepIfRev, 24);
  // 30-deep nesting, in both declaration orders: here (elses == depth, no
  // surplus to elect over) the tree is unambiguous, so a clean parse is
  // provable.
  depth_probe(kDeepIf, 30);
  depth_probe(kDeepIfRev, 30);

  overlap();
}
