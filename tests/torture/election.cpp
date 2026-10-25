// Where the engine elects among readings: the arm written first, between
// rules and between the alternatives of one, a condition against a shift, the
// dangling else at depth, and a run partitioned two ways. A
// reduce/reduce retains its fallback; lookahead can settle the cell without a
// branch.

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/engines/lr/runtime/LrParseStats.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/engines/shared/runtime/AstReducer.hpp"
#include "cursiv/test/Check.hpp"
#include "dangling/generated/DeepIf/AstReflection.hpp"
#include "dangling/generated/DeepIf/Parser.hpp"
#include "dangling/generated/DeepIf/Table.hpp"
#include "election/generated/Election/AstReflection.hpp"
#include "election/generated/Election/Parser.hpp"
#include "election/generated/Election/Table.hpp"
#include "torture/DeepIfGrammar.hpp"
#include "torture/ElectionGrammar.hpp"

namespace {

namespace ast = telect::ast;
namespace e = telect::generated::Election;
namespace d = tdang::generated::DeepIf;
using Stmt = tdang::ast::Stmt;

// The table, not the grammar: the counters are the table's, which the erased
// door does not carry, so the lane drives the generated table the module's own
// parser drives.
template <const auto& A>
bool accepts(std::string_view text, const cursiv::LrDrive* drive,
             cursiv::Cst& cst, cursiv::LrParseStats* stats = nullptr) {
  return cursiv::lr_parser<A>.parse(text, cst, stats, drive) &&
         cst.accepted() && !cst.recovered();
}

// One case of the election table, read clean, its tree built. The tree reads
// the document in place: the caller keeps it alive.
cursiv::ParseResult elect(std::string_view doc,
                          cursiv::LrParseStats* stats = nullptr) {
  cursiv::Cst cst;
  CHECK(accepts<e::kTable>(doc, e::kDrivePtr, cst, stats));
  return cursiv::AstReducer::build<e::kTable.stamp>(e::kAstActions,
                                                    std::move(cst));
}

const ast::Case* case_of(const cursiv::ParseResult& r) {
  return r.value_as<ast::Case>();
}

// One cell, both reductions kept: the look-ahead elects the earlier complete
// reading without forking on this input.
void settles_without_fork(std::string_view opener, bool wantX) {
  cursiv::LrParseStats stats;
  const std::string doc = std::string(opener) + " a b x";
  const cursiv::ParseResult r = elect(doc, &stats);
  CHECK(stats.splits == 0);
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
// R's own alternatives, so the conflict survives to the table and the episode
// forks over it.
void runtime_elects(std::string_view opener, bool wantX) {
  // Whether the cell forked or the look-ahead settled it is the engine's
  // own business; what is pinned is the arm elected.
  const std::string doc = std::string(opener) + " c a x";
  const cursiv::ParseResult r = elect(doc);
  const ast::Case* c = case_of(r);
  const ast::Hold* top = c != nullptr ? c->hold : nullptr;
  CHECK(top != nullptr && top->r != nullptr);
  if (top == nullptr || top->r == nullptr) return;
  const bool gotX = top->r->type == &cursiv::ast_type_tag<ast::RX>;
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
  return r.value_as<Stmt>();
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

void small_ast_checks() {
  cursiv::Cst cst;

  // A lone if: no conflict, no ambiguity to elect.
  CHECK(accepts<d::kTable>("if a then x", d::kDrivePtr, cst));
  {
    cursiv::ParseResult r = cursiv::AstReducer::build<d::kTable.stamp>(
        d::kAstActions, std::move(cst));
    const Stmt* root = r.value_as<Stmt>();
    CHECK(root != nullptr);
    CHECK(root->cond == "a");
    CHECK(root->then_ != nullptr && root->then_->cond == "x");
    CHECK(root->else_ == nullptr);
  }

  // "if a then if b then x else e" reads two ways, parting first at the
  // outer if, which keeps its first arm written: the nearest takes the else.
  cursiv::Cst cst2;
  CHECK(accepts<d::kTable>("if a then if b then x else e", d::kDrivePtr, cst2));
  {
    cursiv::ParseResult r = cursiv::AstReducer::build<d::kTable.stamp>(
        d::kAstActions, std::move(cst2));
    const Stmt* root = r.value_as<Stmt>();
    CHECK(root != nullptr && root->cond == "a");
    CHECK(root->else_ == nullptr);
    const Stmt* inner = root->then_;
    CHECK(inner != nullptr && inner->cond == "b");
    CHECK(inner->else_ != nullptr && inner->else_->cond == "e");
    CHECK(inner->then_ != nullptr && inner->then_->cond == "x");
  }

  // The entry's own extent covers the whole document -- a span check, not
  // just a boolean.
  cursiv::Cst cst3;
  const std::string_view text = "if a then if b then x else e";
  CHECK(accepts<d::kTable>(text, d::kDrivePtr, cst3));
  CHECK(cst3[cst3.entry_root()].begin == 0);
  CHECK(cst3[cst3.entry_root()].end == text.size());

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

  // Two ifs, two elses: only one complete reading exists, and it is also
  // what nearest-binds gives -- e0 -> inner if, e1 -> outer if.
  cursiv::Cst cst5;
  cursiv::LrParseStats stats5;
  CHECK(accepts<d::kTable>("if a then if b then x else e0 else e1",
                           d::kDrivePtr, cst5, &stats5));
  {
    cursiv::ParseResult r = cursiv::AstReducer::build<d::kTable.stamp>(
        d::kAstActions, std::move(cst5));
    const Stmt* root = r.value_as<Stmt>();
    CHECK(root != nullptr && root->cond == "a");
    CHECK(root->else_ != nullptr && root->else_->cond == "e1");
    const Stmt* inner = root->then_;
    CHECK(inner != nullptr && inner->cond == "b");
    CHECK(inner->else_ != nullptr && inner->else_->cond == "e0");
  }
}

// Does a valid document ever get misparsed at depth?
template <const auto& A>
void depth_probe(const Dangling& g, const cursiv::LrDrive* drive,
                 const cursiv::AstActions& actions, unsigned maxDepth) {
  for (unsigned n = 1; n <= maxDepth; ++n) {
    const std::string text =
        nested(g.opener, n, n);  // N ifs, N elses: forces a bind at every level
    cursiv::Cst cst;
    cursiv::LrParseStats stats;
    const bool clean = accepts<A>(text, drive, cst, &stats);

    // A fully-nested N-if/N-else document has exactly one completion (every
    // if must take its own else, since there are exactly as many of each) --
    // it must read clean.
    CHECK(clean);
    if (!clean) {
      std::printf("    %s: depth=%u NOT clean\n", g.label, n);
      continue;
    }

    // The tree must be the one valid nesting: elses were typed
    // innermost-first, so walking outer to inner (c0, c1, ...) meets them in
    // reverse -- c_k binds e_{n-1-k}.
    const cursiv::ParseResult r =
        cursiv::AstReducer::build<A.stamp>(actions, std::move(cst));
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
template <const auto& A>
void election_stable_in_depth(const Dangling& g, const cursiv::LrDrive* drive,
                              const cursiv::AstActions& actions,
                              unsigned maxDepth) {
  for (unsigned depth = 2; depth <= maxDepth; ++depth) {
    const std::string text = nested(g.opener, depth, 1);
    cursiv::Cst cst;
    cursiv::LrParseStats stats;
    const bool clean = accepts<A>(text, drive, cst, &stats);
    CHECK(clean);
    if (!clean) continue;
    const cursiv::ParseResult r =
        cursiv::AstReducer::build<A.stamp>(actions, std::move(cst));
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
          list->groups[0]->type == &cursiv::ast_type_tag<ast::Pair>);
  }

  // "aaa": only a triple fits.
  {
    const cursiv::ParseResult r = elect("overlap aaa");
    const ast::List* list = groups_of(r);
    CHECK(list != nullptr && list->groups.size() == 1);
    CHECK(list != nullptr && !list->groups.empty() &&
          list->groups[0]->type == &cursiv::ast_type_tag<ast::Triple>);
  }

  // "a" alone: neither group shape fits -- refused, not silently accepted
  // as a partial group.
  {
    cursiv::Cst cst;
    CHECK(!accepts<e::kTable>("overlap a", e::kDrivePtr, cst));
  }

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
        if (grp->type == &cursiv::ast_type_tag<ast::Pair>) {
          consumed += 2;
        } else if (grp->type == &cursiv::ast_type_tag<ast::Triple>) {
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
  // Every case keeps its one conflict to the table: none is decided at
  // construction.
  CHECK(e::kTable.nconflicts == 10);
  CHECK(d::kTable.nconflicts > 0);  // built to conflict

  // X declared first: X wins; Y declared first: Y wins.
  settles_without_fork("rankxy", true);
  settles_without_fork("rankyx", false);
  // The readings part first at Hold's arms, not R's: Hold's first arm, which
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
  election_stable_in_depth<d::kTable>(kDeepIf, d::kDrivePtr, d::kAstActions,
                                      24);
  election_stable_in_depth<e::kTable>(kDeepIfRev, e::kDrivePtr, e::kAstActions,
                                      24);
  // 30-deep nesting, in both declaration orders: here (elses == depth, no
  // surplus to elect over) the tree is unambiguous, so a clean parse is
  // provable.
  depth_probe<d::kTable>(kDeepIf, d::kDrivePtr, d::kAstActions, 30);
  depth_probe<e::kTable>(kDeepIfRev, e::kDrivePtr, e::kAstActions, 30);

  overlap();
}
