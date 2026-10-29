#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/syntax-tree/ConvertGrammars.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/utils/TypeName.hpp"
#include "cursiv/engines/lr/gen/LrFinalTable.hpp"
#include "cursiv/engines/lr/gen/LrGotoRows.hpp"
#include "cursiv/engines/lr/gen/LrIdentity.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/engines/lr/gen/lr_build.hpp"
#include "cursiv/engines/lr/gen/lr_build_derived.hpp"
#include "cursiv/engines/lr/gen/lr_build_guards.hpp"
#include "cursiv/engines/lr/gen/lr_build_passes.hpp"
#include "cursiv/engines/lr/gen/lr_completion_sites.hpp"
#include "cursiv/engines/lr/gen/lr_emit_parser.hpp"
#include "cursiv/engines/lr/gen/lr_entry_rows.hpp"
#include "cursiv/engines/lr/gen/lr_term_run.hpp"
#include "cursiv/engines/lr/table/LrActionKind.hpp"
#include "cursiv/engines/shared/gen/AstRegistry.hpp"
#include "cursiv/engines/shared/gen/AstRowOf.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "deferrednested/generated/NestedFirst/Module.hpp"
#include "engines/lr/AsiGrammar.hpp"
#include "engines/lr/AuditGrammar.hpp"
#include "engines/lr/BuildBoundsGrammars.hpp"
#include "engines/lr/ChainInsertGrammar.hpp"
#include "engines/lr/ConditionRefsGrammar.hpp"
#include "engines/lr/CornerKeyGrammar.hpp"
#include "engines/lr/DeferredChoiceGrammars.hpp"
#include "engines/lr/EdgesGrammar.hpp"
#include "engines/lr/GrowthGrammars.hpp"
#include "engines/lr/GuardedChoiceGrammar.hpp"
#include "engines/lr/IdentityCaseGrammars.hpp"
#include "engines/lr/IdentityGrammar.hpp"
#include "engines/lr/InsertOrderGrammar.hpp"
#include "engines/lr/LexUnicodeGrammars.hpp"
#include "engines/lr/ManyAlternativesGrammar.hpp"
#include "engines/lr/TableOrderGrammar.hpp"
#include "engines/lr/VirtualStateGrammar.hpp"
#include "engines/lr/WrittenOrderGrammar.hpp"
#include "astfusedlr/generated/Lr/Module.hpp"
#include "passes/generated/G/Module.hpp"
#include "passes/generated/G/Table.hpp"
#include "torture/ElectionGrammar.hpp"

namespace lr_identity_cases {

// The construction run natively: the identity it picks and the completion
// sites it agrees on.

// The build must pick the wider identity on its own: a user never hears the
// word. The choice is made here the way the generator makes it, over a
// construction run natively.
namespace identity {

namespace {

namespace ast = tident::ast;
using G = tident::G;
using Built = cursiv::LrBuildTable;

const cursiv::Parser& engine() {
  static const auto p = tfused::generated::Lr::create_parser();
  return *p;
}

template <class Kid>
void reads(std::string_view src) {
  const cursiv::ParseResult r = engine().parse(src);
  CHECK(r.accepted);
  CHECK(static_cast<bool>(r) && r.diagnostics.empty());
  auto* doc = r.value_as<ast::Doc>();
  CHECK(doc != nullptr && cursiv::ast_cast<Kid>(doc->kid) != nullptr);
}

// A tag reads as held unless known false. Two tags whose words differ only in
// where known ends and truth begins stay apart; a condition known to hold and
// one nothing is known of fuse.
void minimization_reads_condition_tags_as_held() {
  using cursiv::DynSet;
  // Two states alike but for their tags, minimised: how many stand, and where
  // the first one's goto lands.
  const auto minimized = [](std::array<DynSet, 2> known,
                            std::array<DynSet, 2> truth) {
    Built a;
    a.nstates = a.nactions = 2;
    a.cfg.nnts = a.cfg.nterms = a.cfg.nprods = 1;
    a.cfg.prods[0] = {};
    a.cfg.prodBuild[0] = {};
    for (unsigned s = 0; s < 2; ++s) {
      a.actionBase[s] = s;
      a.actionCount[s] = 1;
      a.actions[s] = {0, cursiv::LrActionKind::accept, 0, 0};
      a.gotoRows.edges.push_back({0, 1, 0});
      a.gotoRows.close_row();
      a.mode[s] = 0;
      a.closeRow[s] = {};
      a.follow[s].set(0);
      a.tagKnown[s] = known[s];
      a.tagTruth[s] = truth[s];
      CHECK(known[s].covers(truth[s]));
    }
    cursiv::lr_minimize_states(a);
    return std::pair{unsigned{a.nstates}, unsigned{a.gotoRows[0][0].to}};
  };
  CHECK(minimized(
            {DynSet{{~0ull, 1, 0x000100000001ffffull}}, DynSet{{~0ull, 1}}},
            {DynSet{}, DynSet{{0xffff000100000001ull}}}) == std::pair{2u, 1u});
  CHECK(minimized({DynSet{}, DynSet{{1}}}, {DynSet{}, DynSet{{1}}}).first == 1);
}

}  // namespace

void run() {
  minimization_reads_condition_tags_as_held();
  // The control: the core graph really is the broken one -- its merge made
  // a cell the grammar does not order, and the detector named it.
  const cursiv::LrLowered& low = cursiv::LrLowering<G>::table();
  std::unique_ptr<Built> picked =
      std::make_unique<Built>(cursiv::lr_build_automaton(low));
  CHECK(picked->nOrdered != 0);
  CHECK(picked->nMergeInduced != 0);

  if (picked->nMergeInduced != 0) {
    std::unique_ptr<Built> wide =
        std::make_unique<Built>(cursiv::lr_build_wide(low));
    if (cursiv::lr_wider_stands(*picked, *wide)) picked = std::move(wide);
  }

  // The picked table is deterministic and whole: the wider identity stood.
  CHECK(picked->identity == cursiv::LrIdentity::core_plus_lookaheads);
  CHECK(picked->nconflicts == 0 && picked->nOwed == 0);
  CHECK(picked->nOrdered == 0);
  CHECK(picked->nMergeInduced == 0);

  // A conflict one context holds whole is no merge's: the wider graph is
  // not asked about it.
  {
    const std::unique_ptr<Built> lr2 = std::make_unique<Built>(
        cursiv::lr_build_automaton(
            cursiv::LrLowering<tidcase::lr2::G>::table()));
    CHECK(lr2->nOrdered != 0);
    CHECK(lr2->nMergeInduced == 0);
  }

  // Two canonical states that hold the same conflict cell are one state.
  {
    const cursiv::LrLowered& sum = cursiv::LrLowering<tidcase::sum::G>::table();
    const std::unique_ptr<Built> core =
        std::make_unique<Built>(cursiv::lr_build_automaton(sum));
    const std::unique_ptr<Built> wide =
        std::make_unique<Built>(cursiv::lr_build_wide(sum));
    CHECK(core->nMergeInduced != 0);
    CHECK(cursiv::lr_wider_stands(*core, *wide));
    CHECK(wide->nconflicts == 1);
  }

  // The wider graph is judged on the ambiguities it owes, never on cells:
  // the passes copy a cell into every state they mint.
  {
    const std::unique_ptr<Built> core = std::make_unique<Built>();
    const std::unique_ptr<Built> wide = std::make_unique<Built>();
    core->nOwed = wide->nOwed = 3;
    core->nconflicts = 12;
    wide->nconflicts = 4;
    CHECK(!cursiv::lr_wider_stands(*core, *wide));
    wide->nOwed = 2;
    CHECK(cursiv::lr_wider_stands(*core, *wide));
    wide->overflow = true;
    CHECK(!cursiv::lr_wider_stands(*core, *wide));
  }

  reads<ast::AKid>("a c d");
  reads<ast::BKid>("b c d");
  reads<ast::BKid>("a c e");
  reads<ast::AKid>("b c e");
}

}  // namespace identity

// Completion agrees on semantic sites, including a genuinely bare caller.
namespace completion_sites {

namespace {
struct Target : cursiv::Extends<cursiv::AstNode> {};
struct Narrow : cursiv::Extends<Target> {};
// The root's class: the common base of the `Doc` and `Other` the sites build.
struct Site : cursiv::Extends<cursiv::AstNode> {};
template <auto Kind>
struct Owner : cursiv::Extends<Site> {
  cursiv::reference<Target> to, other;
};
// GCC spells both owners as Owner<1>; their completion sites stay distinct.
using Doc = Owner<1>;
using Other = Owner<1L>;
enum class Last { same, other, owner, narrow, bare };

template <Last LastSite, bool Direct = false>
struct Sites : cursiv::GrammarSpec {
  struct AT : terminal<lit<"@">> {};
  struct ID : terminal<some(cr<"a-z">)> {};
  struct Name : rule<ref<AT> + ref<ID>> {};
  template <unsigned I>
  static constexpr auto tag = [] {
    auto s = cursiv::str{"0"};
    s.data[0] += I;
    return s;
  }();
  template <unsigned I>
  struct Prefix : rule<lit<tag<I>> + lit<":">> {};

  template <unsigned I>
  static consteval auto site() {
    constexpr auto payload = [] {
      if constexpr (Direct) return ref<AT>;
      else return ref<Name>;
    }();
    if constexpr (I == 8 && LastSite == Last::bare)
      return create<Site>(payload);
    else if constexpr (I == 8 && LastSite == Last::other)
      return assign<&Doc::other>(payload);
    else if constexpr (I == 8 && LastSite == Last::owner)
      return create<Other>(assign<&Other::to>(payload));
    else if constexpr (I == 8 && LastSite == Last::narrow)
      return assign<cursiv::xref<&Doc::to, Narrow>>(payload);
    else
      return assign<&Doc::to>(payload);
  }
  template <unsigned I>
  struct WriteAt : rule<site<I>()> {};

  template <std::size_t... I>
  static consteval auto choices(std::index_sequence<I...>) {
    if constexpr (Direct)
      return ((ref<Prefix<I>> + ref<WriteAt<I>>) | ...);
    else
      return ((ref<Prefix<I>> + site<I>()) | ...);
  }
  struct Root : rule<choices(std::make_index_sequence<9>{})> {};
  using entry = Root;
};

template <class R>
std::uint16_t symbol_named(const cursiv::LrBuildCfg& c) {
  const auto name = cursiv::detail::type_name<cursiv::declared_of_t<R>>();
  for (std::uint16_t t = 0; t < c.nterms; ++t)
    if (c.term_name(t) == name) return t;
  for (std::uint16_t n = 0; n < c.nnts; ++n)
    if (c.nt_name(n) == name) return cursiv::nt_sym(n);
  return cursiv::kNoRow;
}

template <Last LastSite, bool Direct = false>
void completion_sites_agree() {
  using G = Sites<LastSite, Direct>;
  const auto& low = cursiv::LrLowering<G>::table();
  // Keep Prefix's reduction: its expectation has no local shift site and
  // must use the proved agreement of AT's own sites or Name's callers.
  auto a = cursiv::lr_build_automaton(low, {.passes = false});
  CHECK(!a.overflow);
  const auto registry = cursiv::ast_registry<G, cursiv::LrLowering<G>>();
  const auto facts =
      cursiv::lr_record_gens(cursiv::lr_entry_table(a), registry.rows);
  const auto canonical = cursiv::lr_completion_sites(facts);
  const auto symbol = symbol_named<
      std::conditional_t<Direct, typename G::AT, typename G::Name>>(a.cfg);
  std::set<cursiv::ElementId> raw;
  for (unsigned p = 0; p < a.cfg.nprods; ++p) {
    const auto& q = a.cfg.prods[p];
    for (unsigned i = 0; i < q.len; ++i)
      if (a.cfg.sym_at(q, i) == symbol && a.cfg.row_at(q, i) != cursiv::kNoElement)
        raw.insert(a.cfg.row_at(q, i));
  }
  CHECK(raw.size() ==
        (LastSite == Last::same || LastSite == Last::bare ? 1u : 2u));
  if constexpr (LastSite == Last::same || LastSite == Last::owner) {
    std::set<cursiv::ElementId> normalized;
    for (const auto row : raw) normalized.insert(canonical[row]);
    CHECK(normalized.size() == (LastSite == Last::same ? 1u : 2u));
  }
  CHECK(!raw.empty());
  const auto owner = facts[*raw.begin()].typeSlot;

  cursiv::lr_derive_tables(a, canonical);
  const auto terminal = symbol_named<typename G::AT>(a.cfg);
  // The shifts naming the terminal: where every call fills one member the
  // site is that member, and one call filling another is a site of its own.
  unsigned shifts = 0;
  bool other = false;
  for (const auto& e : a.expectPool) {
    if (e.feature != terminal) continue;
    ++shifts;
    const bool to = e.site != cursiv::kNoElement &&
                    facts[e.site].feature == "to" &&
                    facts[e.site].typeSlot == owner &&
                    facts[e.site].narrow == cursiv::kNoNarrow;
    if constexpr (LastSite == Last::same) CHECK(to);
    other = other || !to;
  }
  CHECK(shifts != 0);
  if constexpr (LastSite != Last::same) CHECK(other);
}
}  // namespace

void run() {
  completion_sites_agree<Last::same>();
  completion_sites_agree<Last::same, true>();
  completion_sites_agree<Last::other>();
  completion_sites_agree<Last::owner>();
  completion_sites_agree<Last::narrow>();
  completion_sites_agree<Last::bare>();
}

}  // namespace completion_sites

namespace case_lrmanyalternatives {
// The items of a state are all read, however many alternatives stand before
// the one that reads another terminal.

void run() {
  const cursiv::LrBuildTable A =
      cursiv::lr_build_automaton(cursiv::LrLowering<tmany::G>::table());
  CHECK(!A.overflow && !A.cfg.lowerFail());
  // After `a` the first alternatives read `t` and one reads `u`: `t` is
  // written first, though `u` has the lesser id.
  unsigned lists = 0;
  for (unsigned s = 0; s < A.nstates; ++s) {
    const std::uint32_t k = A.insertOf[s];
    if (k == 0 || A.insertAt[k] - A.insertAt[k - 1] < 2) continue;
    const std::string_view first =
        A.cfg.term_name(A.insertWord[A.insertAt[k - 1]]);
    const std::string_view second =
        A.cfg.term_name(A.insertWord[A.insertAt[k - 1] + 1]);
    lists += first == "t" && second == "u";
  }
  CHECK(lists == 1);
}
}  // namespace case_lrmanyalternatives

namespace case_lrchaininsert {
// The order a repair inserts terminals in follows the reductions two tokens
// share however long their chain goes.

namespace {

// The terminal a state's list inserts first, empty where its ids order them.
std::string_view first_inserted(const cursiv::LrBuildTable& A, unsigned s) {
  const std::uint32_t k = A.insertOf[s];
  if (k == 0) return {};
  return A.cfg.term_name(A.insertWord[A.insertAt[k - 1]]);
}

}  // namespace

void run() {
  const cursiv::LrBuildTable A =
      cursiv::lr_build_automaton(cursiv::LrLowering<tchaininsert::G>::table());
  CHECK(!A.overflow && !A.cfg.lowerFail());
  unsigned lists = 0;
  for (unsigned s = 0; s < A.nstates; ++s)
    lists += first_inserted(A, s).ends_with("b");
  CHECK(lists >= 27);
}
}  // namespace case_lrchaininsert

namespace case_lrinsertorder {
// A repair tries the terminals of a state in the order a reading of the
// grammar meets them: alternatives as written, a loop that goes on before the
// rule that closes it, and, in a state that only reduces, what the rules it
// returns to read.

namespace {

// The terminals state `s` inserts in, in the order of its list.
std::string listed(const cursiv::LrBuildTable& A, unsigned s) {
  const std::uint32_t k = A.insertOf[s];
  std::string out;
  if (k == 0) return out;
  for (std::uint32_t i = A.insertAt[k - 1]; i < A.insertAt[k]; ++i) {
    if (!out.empty()) out += ' ';
    out += A.cfg.term_name(A.insertWord[i]);
  }
  return out;
}

bool shifts(const cursiv::LrBuildTable& A, unsigned s) {
  for (unsigned z = 0; z < A.actionCount[s]; ++z)
    if (A.actions[A.actionBase[s] + z].kind == cursiv::LrActionKind::shift)
      return true;
  return false;
}

}  // namespace

void run() {
  const cursiv::LrBuildTable A =
      cursiv::lr_build_automaton(cursiv::LrLowering<tinsertorder::G>::table());
  CHECK(!A.overflow && !A.cfg.lowerFail());
  unsigned picks = 0, goes = 0, returns = 0;
  for (unsigned s = 0; s < A.nstates; ++s) {
    const std::string list = listed(A, s);
    picks += list == "q x y";
    if (list != "m }") continue;
    (shifts(A, s) ? goes : returns) += 1;
  }
  CHECK(picks == 1);
  CHECK(goes >= 1);
  CHECK(returns >= 1);
}
}  // namespace case_lrinsertorder

namespace case_lrguards {
// A condition takes readings away and adds none: a table owes no ambiguity
// the same grammar without its conditions does not owe. Every grammar of the
// suite that writes a condition in a rule is held to it.

namespace {

using Built = cursiv::LrBuildTable;
template <class Rule>
struct ElectionCase : telect::Election { using entry = Rule; };

// The grammar with its conditions gone: the positions they guarded stand
// bare, and no reduction a rule is called for waits on one.
std::unique_ptr<cursiv::LrLowered> without_conditions(
    const cursiv::LrLowered& low) {
  auto plain = std::make_unique<cursiv::LrLowered>(low);
  auto& g = plain->cfg;
  cursiv::lr_guard_positions(g, plain->writes);
  std::vector<char> called(g.nnts, 0);
  for (std::uint32_t i = 0; i < g.nslots; ++i)
    if (!cursiv::is_term(g.slotSym[i])) called[cursiv::nt_index(g.slotSym[i])] = 1;
  for (unsigned p = 0; p < g.nprods; ++p)
    if (called[g.prods[p].lhs]) g.prods[p].gap = 0;
  return plain;
}

template <class G>
void adds_no_ambiguity(const char* label) {
  const cursiv::LrLowered& low = cursiv::LrLowering<G>::table();
  const auto guarded = std::make_unique<Built>(cursiv::lr_build_automaton(low));
  const auto bare =
      std::make_unique<Built>(cursiv::lr_build_automaton(*without_conditions(low)));
  CHECK(!guarded->overflow && !bare->overflow);
  CHECK(guarded->nOwed <= bare->nOwed);
  if (guarded->nOwed > bare->nOwed)
    std::printf("    %s owes %u, %u without its conditions\n", label,
                guarded->nOwed, bare->nOwed);
}

// Reductions that wait on a condition: a condition standing before a position
// guards that position, and gates nothing a rule is reduced by.
template <class G>
unsigned gated_reductions() {
  const auto built = std::make_unique<Built>(
      cursiv::lr_build_automaton(cursiv::LrLowering<G>::table()));
  unsigned n = 0;
  for (unsigned p = 0; p < built->cfg.nprods; ++p)
    n += built->cfg.prods[p].gap != 0;
  return n;
}

// Options before a condition: the graph lays a program per way in, and the
// states they land on fuse.
struct Folded : cursiv::GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct Root : rule<option(lit<"c">) + option(lit<"a">) + lit<"b"> +
                     !gap::none + lit<"b">> {};
  using entry = Root;
  using ignored = WS;
};

// The table keeps the programs its states name, each once.
template <class G>
void names_each_program_once() {
  const auto built = std::make_unique<Built>(
      cursiv::lr_build_automaton(cursiv::LrLowering<G>::table()));
  const std::vector<cursiv::LrGuard>& guards = built->guards;
  std::vector<std::uint8_t> named(guards.size(), 0);
  for (unsigned s = 0; s < built->nstates; ++s) {
    for (const cursiv::LrAction& x : built->action_row(s))
      if (x.kind == cursiv::LrActionKind::guard) named[x.arg] = 1;
    for (const auto& e : built->gotoRows[s])
      if (e.guard != 0) named[e.guard - 1u] = 1;
  }
  CHECK(!guards.empty());
  CHECK(std::ranges::count(named, 0) == 0);
  for (auto it = guards.begin(); it != guards.end(); ++it)
    CHECK(std::find(guards.begin(), it, *it) == it);
}

}  // namespace

void run() {
  adds_no_ambiguity<ElectionCase<telect::Election::GuardXY>>("GuardXY");
  adds_no_ambiguity<ElectionCase<telect::Election::GuardYX>>("GuardYX");
  adds_no_ambiguity<ElectionCase<telect::Election::MixedXY>>("GuardMixedXY");
  adds_no_ambiguity<ElectionCase<telect::Election::MixedYX>>("GuardMixedYX");
  adds_no_ambiguity<tregion::G>("RegionEdges");
  adds_no_ambiguity<taudit::G>("Audit");
  adds_no_ambiguity<tasi::G>("Asi");
  adds_no_ambiguity<tasi::more::G>("AsiMore");
  adds_no_ambiguity<tasi::ops::G>("AsiOps");
  adds_no_ambiguity<tasi::prec::G>("AsiPrec");
  adds_no_ambiguity<tasi::gaps::G>("GapShapes");
  adds_no_ambiguity<tvirtual::G>("VirtualState");
  adds_no_ambiguity<tconv::inv::G>("ConvertInv");
  adds_no_ambiguity<tunicode::intersect::G>("UnicodeIntersect");
  CHECK(gated_reductions<tcondref::G>() == 0);
  CHECK(gated_reductions<tasi::tail::G>() != 0);
  names_each_program_once<Folded>();
  names_each_program_once<tasi::G>();
  names_each_program_once<taudit::G>();
}
}  // namespace case_lrguards

namespace case_lrcornerkey {
// What orders two readings is read off the words of the whole left corner
// they climb through, not of its first sixteen.

namespace {

// The rule whose reduction the table lays first where `Whole_` and `Tail`
// both reduce on a token.
std::string_view first_reduction(const cursiv::LrBuildTable& A) {
  for (unsigned s = 0; s < A.nstates; ++s)
    for (std::uint32_t k = A.actionBase[s] + 1;
         k < A.actionBase[s] + A.actionCount[s]; ++k) {
      const cursiv::LrAction& a = A.actions[k - 1];
      const cursiv::LrAction& b = A.actions[k];
      if (a.term != b.term || a.kind != cursiv::LrActionKind::reduce ||
          b.kind != cursiv::LrActionKind::reduce)
        continue;
      const std::string_view x = A.cfg.nt_name(A.cfg.prods[a.arg].lhs);
      const std::string_view y = A.cfg.nt_name(A.cfg.prods[b.arg].lhs);
      if ((x.ends_with("Whole_") && y.ends_with("Tail")) ||
          (x.ends_with("Tail") && y.ends_with("Whole_")))
        return x;
    }
  return {};
}

}  // namespace

void run() {
  const cursiv::LrBuildTable A =
      cursiv::lr_build_automaton(cursiv::LrLowering<tcornerkey::Lr>::table());
  CHECK(!A.overflow && !A.cfg.lowerFail());
  CHECK(first_reduction(A).ends_with("Whole_"));
}
}  // namespace case_lrcornerkey

namespace case_lrtableorder {
// The order the table lays a cell's arms in drops a stack only where it is the
// election's for every text. A chain's cells are; the cells of a run that
// grows by turns are not, and its texts read as the LL engine reads them.

namespace {

struct Cells {
  unsigned conflicts = 0, exact = 0;
};

Cells cells_of(const cursiv::LrBuildTable& A) {
  Cells n;
  for (unsigned s = 0; s < A.nstates; ++s)
    cursiv::lr_each_run(A.actions.data() + A.actionBase[s], A.actionCount[s],
                        [&](std::size_t i, std::size_t j) {
                          if (j - i < 2) return;
                          ++n.conflicts;
                          n.exact += A.actions[A.actionBase[s] + i].exact;
                        });
  return n;
}

void a_chain_is_ordered_for_every_text() {
  const cursiv::LrBuildTable A =
      cursiv::lr_build_automaton(cursiv::LrLowering<tgchain::Lr>::table());
  CHECK(!A.overflow && !A.cfg.lowerFail());
  const Cells n = cells_of(A);
  CHECK(n.conflicts != 0 && n.exact == n.conflicts);
}

void a_run_that_grows_by_turns_is_not() {
  const cursiv::LrBuildTable A =
      cursiv::lr_build_automaton(cursiv::LrLowering<ttableorder::Lr>::table());
  CHECK(!A.overflow && !A.cfg.lowerFail());
  const Cells n = cells_of(A);
  CHECK(n.conflicts != 0 && n.exact < n.conflicts);
}

}  // namespace

void run() {
  a_chain_is_ordered_for_every_text();
  a_run_that_grows_by_turns_is_not();
}
}  // namespace case_lrtableorder

namespace case_lrwrittenorder {
// Keep every ordered action, its write sites and its exactness unchanged when
// repeated derivation segments are condensed. Counts alone would miss a tie
// resolved differently or an inexact order accidentally promoted to exact.

namespace {
std::uint64_t action_digest(const cursiv::LrBuildTable& a) {
  std::uint64_t hash = 14695981039346656037ull;
  const auto add = [&](std::uint32_t x) {
    for (unsigned i = 0; i < 4; ++i) {
      hash ^= (x >> (i * 8)) & 255;
      hash *= 1099511628211ull;
    }
  };
  add(a.nstates);
  for (unsigned s = 0; s < a.nstates; ++s) {
    add(a.actionCount[s]);
    for (unsigned i = 0; i < a.actionCount[s]; ++i) {
      const auto& x = a.actions[a.actionBase[s] + i];
      add(x.term);
      add(static_cast<unsigned>(x.kind));
      add(x.arg);
      add(x.site);
      add(x.outer);
      add(x.exact);
    }
  }
  return hash;
}

// Digests of the action tables, both graph constructions: left corners of
// varying length, growing turns, shared origins, ties that stay inexact.
template <class G>
void keeps_actions(bool wide, std::uint64_t digest, unsigned states,
                   unsigned canonical, unsigned conflicts, unsigned owed) {
  const auto& low = cursiv::LrLowering<G>::table();
  const auto a =
      wide ? cursiv::lr_build_wide(low) : cursiv::lr_build_automaton(low);
  CHECK(!a.overflow && !a.cfg.lowerFail());
  CHECK(a.nstates == states && a.ncanonical == canonical);
  CHECK(a.nconflicts == conflicts && a.nOwed == owed);
  CHECK(a.nOrdered == 0);
  CHECK(action_digest(a) == digest);
}
}  // namespace

void run() {
  for (bool wide : {false, true}) {
    keeps_actions<ttableorder::Lr>(wide, 12584957980273038131ull, 13, 13, 5, 3);
    keeps_actions<tgchain::Lr>(wide, 7874924981012405869ull, 11, 12, 2, 2);
    keeps_actions<tcornerkey::Lr>(wide, 11114670162566949468ull, 49, 51, 1, 1);
  }
  // This grammar reaches the uniform-descendant shortcut, including the
  // inexact written order of nested dangling-else choices.
  keeps_actions<tlldangling::Lr>(false, 6348924868896240653ull, 38, 41, 3, 3);
  keeps_actions<tlldangling::Lr>(true, 15855575149852123932ull, 53, 71, 3, 3);
}
}  // namespace case_lrwrittenorder

namespace case_lrbuildbounds {
// The construction's own indices are as wide as the grammar needs: only the
// frozen table's widths refuse a grammar, by name.

namespace {

namespace longrun = tpasses::generated::G;

template <class G>
std::unique_ptr<cursiv::LrBuildTable> built() {
  return std::make_unique<cursiv::LrBuildTable>(
      cursiv::lr_build_automaton(cursiv::LrLowering<G>::table()));
}

template <class G>
void probe_cuts(unsigned expected) {
  auto a = built<G>();
  CHECK(!a->overflow);
  const std::vector<cursiv::ElementId> sites(a->writes.nchains + a->nfused,
                                             cursiv::kNoElement);
  const auto done = cursiv::LrFinalTable::make(std::move(a), sites);
  unsigned cuts = 0;
  CHECK(!cursiv::lr_emit_parser_states(done, "probes", &cuts).empty());
  CHECK(cuts == expected);
}

}  // namespace

void run() {
  {
    const auto a = built<tbounds::transitions::G>();
    CHECK(!a->overflow);
    CHECK(a->nstates != 0);
  }
  {
    const auto a = built<tbounds::longrun::G>();
    CHECK(!a->overflow);
    CHECK(a->nstates != 0);
    CHECK(a->closeMax == 256);
  }
  {
    const auto a = built<tbounds::peeks::G>();
    CHECK(a->npeeks == 32);
  }
  // Ineligible keywords do not count as truncated probes. Eligible literal
  // entries fit at the capacity and report a cut when it is exceeded.
  probe_cuts<tbounds::probes::G>(0);
  probe_cuts<tbounds::byteprobes::G<64>>(0);
  probe_cuts<tbounds::byteprobes::G<70>>(1);
  // One nonterminal entered from more states than its 16-bit goto count holds.
  {
    auto a = std::make_unique<cursiv::LrBuildTable>();
    a->cfg.nnts = 2;
    cursiv::LrGotoRows rows;
    const std::vector<cursiv::LrGotoRows::Edge> e{{1, 0, 0}};
    for (std::uint32_t s = 0; s <= 0xffffu; ++s) rows.push_row(e);
    a->set_gotos(std::move(rows));
    CHECK(a->overflow && std::string_view(a->overflowCap) ==
                             "one rule is reached from more than 65 535 "
                             "parser states");
  }
  // What a state owes runs past a byte's reach, in the frozen table as in the
  // construction.
  // The shared entry adds its tag to the 256-token completion.
  static_assert(longrun::kCold.closeMax == 257);
  {
    const auto parser = longrun::create_parser();
    std::string text = "longrun ";
    for (unsigned i = 0; i < 256; ++i) text += "a ";
    const auto r = parser->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
    const auto cut = parser->parse(text.substr(0, 20));
    CHECK(cut.accepted && !cut.diagnostics.empty());
  }
}
}  // namespace case_lrbuildbounds

namespace case_lrdeferredchoice {
namespace {

void nested(const cursiv::Parser& parser, std::string_view operand, bool type) {
  const auto text =
      std::string("X := (for all I in ") + std::string(operand) + " => True);";
  const auto result = parser.parse(text);
  CHECK(result.accepted && result.diagnostics.empty());
  CHECK(!result.cst().recovered());
  const auto* doc = result.value_as<tdeferred::Doc>();
  CHECK(doc != nullptr);
  const auto* quant =
      doc ? cursiv::ast_cast<tdeferred::Quant>(doc->value) : nullptr;
  CHECK(quant != nullptr && quant->var != nullptr);
  if (quant == nullptr || quant->var == nullptr) return;
  CHECK(quant->all);
  CHECK((quant->var->type != nullptr) == type);
  CHECK((quant->var->def != nullptr) == !type);
  bool writes = false;
  const auto& grammar = parser.getGrammar();
  const auto& cst = result.cst();
  for (cursiv::NodeId id = 0; id < cst.size(); ++id) {
    const auto& node = cst[id];
    writes |= grammar.recordWrites(node, type ? "type" : "def") ||
              grammar.recordFills(node, type ? "type" : "def");
    CHECK(!grammar.recordWrites(node, type ? "def" : "type"));
    CHECK(!grammar.recordFills(node, type ? "def" : "type"));
  }
  CHECK(writes);
}

}  // namespace

void run() {
  using namespace tdeferred::generated;
  const auto first = NestedFirst::create_parser();
  for (const auto operand : {"Integer", "Pkg.Integer", "reverse Pkg.Integer",
                             "/* before */ Pkg /* dot */ . /* name */ Integer", "'x'"}) {
    const bool type = std::string_view(operand) != "'x'";
    nested(*first, operand, type);
  }
}
}  // namespace case_lrdeferredchoice

void run() {
  // Naming an empty guarded creation preserves the lowered condition program.
  const auto& inlineGuards = cursiv::LrLowering<tguardedchoice::Inline>::cfg();
  const auto& namedGuards = cursiv::LrLowering<tguardedchoice::Named>::cfg();
  CHECK(!inlineGuards.lowerFail() && !namedGuards.lowerFail());
  CHECK(inlineGuards.ngapConds == namedGuards.ngapConds);
  CHECK(inlineGuards.ngapLeaves == namedGuards.ngapLeaves);

  case_lrmanyalternatives::run();
  case_lrchaininsert::run();
  case_lrinsertorder::run();
  case_lrguards::run();
  case_lrcornerkey::run();
  case_lrtableorder::run();
  case_lrwrittenorder::run();
  case_lrbuildbounds::run();
  case_lrdeferredchoice::run();
  identity::run();
  completion_sites::run();
}

}  // namespace lr_identity_cases
