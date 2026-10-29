// What a generated module builds: fused records, adopting and creating
// entries, namespaces and header names, and the lowering's symbol space.

#include <array>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

#include "adopt/generated/G/Module.hpp"
#include "astfusedlr/generated/Lr/Module.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/lr/AdoptEntryGrammar.hpp"
#include "engines/lr/AstFusedGrammar.hpp"
#include "engines/lr/ParseChecks.hpp"
#include "engines/lr/ShadowedNamespaceGrammars.hpp"
#include "engines/lr/sibling/Grammar.hpp"
#include "shadowll/generated/Ll/Module.hpp"
#include "shadowlr/generated/Lr/Module.hpp"
#include "siblinglr/generated/Lr/Module.hpp"
#include "siblingmodule/generated/Module/Module.hpp"

// What a fused record builds: the node made there is what its own nests wrap
// and what the rule's close adopts, and the entry's node is the root -- in
// every engine, which read one grammar here.
namespace ast_fused {

namespace {

using tfused::ast::Node;
using tfused::ast::Wrap;

// The AST points into the document: each case keeps its string alive.
using checks::clean;

void a_nest_on_the_creations_record_wraps_the_node_made_there(
    const cursiv::Parser& p) {
  const std::string doc = "$a !";
  const cursiv::ParseResult r = p.parse(doc);
  CHECK(clean(r));
  const Wrap* const w = cursiv::ast_cast<Wrap>(r.value_as<Node>());
  CHECK(w != nullptr && w->inner != nullptr);
  CHECK(w != nullptr && cursiv::ast_cast<Wrap>(w->inner) == nullptr);
}

void a_close_adopts_the_node_before_its_nest_wraps_it(const cursiv::Parser& p) {
  const std::string doc = "$b abc !";
  const cursiv::ParseResult r = p.parse(doc);
  CHECK(clean(r));
  const Wrap* const w = cursiv::ast_cast<Wrap>(r.value_as<Node>());
  CHECK(w != nullptr && w->inner != nullptr && w->inner->name == "abc");
}

void an_empty_run_of_nests_leaves_the_node_made_on_the_leaf(
    const cursiv::Parser& p) {
  {
    const std::string doc = "$e abc";
    const cursiv::ParseResult r = p.parse(doc);
    CHECK(clean(r));
    Node* const n = r.value_as<Node>();
    CHECK(n != nullptr && n->name == "abc");
    CHECK(cursiv::ast_cast<Wrap>(n) == nullptr);
  }
  {
    const std::string doc = "$e abc ! !";
    const cursiv::ParseResult r = p.parse(doc);
    CHECK(clean(r));
    const Wrap* const outer = cursiv::ast_cast<Wrap>(r.value_as<Node>());
    CHECK(outer != nullptr && outer->inner != nullptr);
    const Wrap* const mid =
        outer == nullptr ? nullptr : cursiv::ast_cast<Wrap>(outer->inner);
    CHECK(mid != nullptr && mid->inner != nullptr && mid->inner->name == "abc");
  }
}

void the_entrys_creation_is_the_root(const cursiv::Parser& p) {
  const std::string doc = "inner A;";
  const cursiv::ParseResult r = p.parse(doc);
  CHECK(clean(r));
  CHECK(cursiv::ast_cast<tadopt::ast::Doc>(r.value) != nullptr);
  bool sorted = true;
  for (std::size_t k = 1; k < r.nodes.size(); ++k)
    if (r.nodes[k]->cstNodeId < r.nodes[k - 1]->cstNodeId)
      sorted = false;
  CHECK(sorted);
}

void inline_creations_keep_their_own_payloads(const cursiv::Parser& p) {
  {
    const std::string doc = "$n abc";
    const auto r = p.parse(doc);
    CHECK(clean(r));
    const Node* const n = r.value_as<Node>();
    CHECK(n && n->child && n->child->child);
    if (n && n->child && n->child->child) {
      CHECK(n != n->child && n->child != n->child->child);
      CHECK(n->name.empty() && n->child->name.empty());
      CHECK(n->child->child->name == "abc");
      CHECK(n->child->parent == n && n->child->child->parent == n->child);
    }
  }
  for (const std::string doc : {"$t abc", "$c abc", "$c ?"}) {
    const auto r = p.parse(doc);
    CHECK(clean(r));
    const Node* const n = r.value_as<Node>();
    CHECK(n && n->child);
    if (!n || !n->child) continue;
    CHECK(n->name.empty() && n->alias.empty());
    const std::string_view name = doc == "$t abc" ? "" : "abc";
    CHECK(n->child->name == (doc == "$c ?" ? "?" : name));
    CHECK(n->child->parent == n && n->child != n);
    CHECK(n->child->alias == (doc == "$t abc" ? "abc" : ""));
    CHECK(!n->flagged && !n->child->flagged);
  }
  {
    const std::string doc = "$m abc def";
    const auto r = p.parse(doc);
    CHECK(clean(r));
    const Node* const n = r.value_as<Node>();
    CHECK(n && n->children.size() == 2);
    if (n && n->children.size() == 2) {
      CHECK(n->children[0]->name == "abc" && n->children[1]->name == "def");
      CHECK(n->children[0] != n->children[1]);
      CHECK(n->children[0]->parent == n && n->children[1]->parent == n);
    }
  }
}

void a_capsule_preserves_options_and_region_edges(const cursiv::Parser& p) {
  for (const std::string doc : {"$o", "$o abc", "$z", "$z abc"}) {
    const auto r = p.parse(doc);
    CHECK(clean(r));
    const Node* const n = r.value_as<Node>();
    CHECK(n != nullptr);
    if (!n) continue;
    CHECK((n->child != nullptr) == (doc != "$o"));
    if (n->child) CHECK(n->child->name == (doc == "$z" ? "" : "abc"));
  }
  {
    const std::string doc = "$s  abc:def  ;";
    const auto r = p.parse(doc);
    CHECK(clean(r));
    const Node* const n = r.value_as<Node>();
    CHECK(n && n->child && n->child->name == "abc" && n->child->alias == "def");
  }
  for (const std::string doc : {"$s abc :def;", "$s abc: def;"})
    CHECK(!clean(p.parse(doc)));
}

void nests_wrap_the_captured_child(const cursiv::Parser& p) {
  for (const std::string prefix : {"$w abc", "$l abc"}) {
    for (unsigned count = 0; count < 3; ++count) {
      const std::string doc = prefix + std::string(count, '!');
      const auto r = p.parse(doc);
      CHECK(clean(r));
      const Node* const n = r.value_as<Node>();
      CHECK(n && n->child);
      if (!n || !n->child) continue;
      CHECK(n->name.empty() && n->child->parent == n);
      CHECK(!n->flagged && !n->child->flagged);
      CHECK(r.cst().text(n->child->cstNodeId) ==
            "abc" + std::string(count, '!'));
      const Node* child = n->child;
      for (unsigned k = 0; k < count && child; ++k) {
        const Wrap* const w = cursiv::ast_cast<Wrap>(child);
        CHECK(w && w->inner && w->inner->parent == w);
        child = w ? w->inner : nullptr;
      }
      CHECK(child && child->name == "abc");
    }
  }
}

void text_actions_run_once_per_declared_occurrence(const cursiv::Parser& p) {
  const auto check = [&](const std::string& text,
                         std::initializer_list<std::string_view> expected) {
    tfused::conversions = 0;
    const auto result = p.parse(text);
    CHECK(clean(result));
    const auto* node = result.value_as<Node>();
    CHECK(node && node->texts.size() == expected.size());
    CHECK(tfused::conversions == expected.size());
    CHECK(result.nodes.size() == 1);
    if (!node || node->texts.size() != expected.size()) return;
    std::size_t i = 0;
    for (const auto value : expected) CHECK(node->texts[i++] == value);
  };
  check("$p abc", {"abc"});
  check("$v abc, def", {"abc", "def"});
  check("$r", {});
  check("$r abc def ghi", {"abc", "def", "ghi"});
  check("$u", {});
  check("$u abc", {"abc"});
  for (const std::string text : {"$h", "$h + abc"}) {
    const auto result = p.parse(text);
    CHECK(clean(result));
    const auto* node = result.value_as<Node>();
    CHECK(node);
    if (!node) continue;
    CHECK((node->child != nullptr) == (text == "$h + abc"));
    CHECK(result.nodes.size() == (text == "$h + abc" ? 2u : 1u));
    if (node->child)
      CHECK(node->child->parent == node && node->child->name == "abc");
  }
}

void a_bare_word_does_not_acquire_another_arms_flag(const cursiv::Parser& p) {
  for (const std::string_view word : {"COMMIT", "END"}) {
    const auto r = p.parse("$k " + std::string(word));
    CHECK(clean(r));
    const Node* const n = r.value_as<Node>();
    CHECK(n && !n->flagged && n->child);
    if (n && n->child) CHECK(n->child->flagged == (word == "END"));
  }
}

void every_grammar(const cursiv::Parser& fused, const cursiv::Parser& entry) {
  a_nest_on_the_creations_record_wraps_the_node_made_there(fused);
  a_close_adopts_the_node_before_its_nest_wraps_it(fused);
  an_empty_run_of_nests_leaves_the_node_made_on_the_leaf(fused);
  inline_creations_keep_their_own_payloads(fused);
  a_capsule_preserves_options_and_region_edges(fused);
  nests_wrap_the_captured_child(fused);
  text_actions_run_once_per_declared_occurrence(fused);
  a_bare_word_does_not_acquire_another_arms_flag(fused);
  the_entrys_creation_is_the_root(entry);
}

}  // namespace

void run() {
  every_grammar(*tfused::generated::Lr::create_parser(),
                *tadopt::generated::G::create_parser());
}

}  // namespace ast_fused

// An entry that adopts a child's node while reading tokens beside it: the
// root is the child's node, named, and nothing is left beside it.
namespace adopt_entry {

namespace {

template <class Engine>
std::string root_name(const Engine& engine, std::string_view text,
                      std::size_t& nodes) {
  const cursiv::ParseResult r = engine->parse(text);
  nodes = r.nodes.size();
  if (!r.accepted || !r.diagnostics.empty()) return "<refused>";
  const auto* const clade = r.value_as<tadopt::ast::Clade>();
  if (clade == nullptr) return "<not a clade>";
  // The root's record is the document's, tokens beside the clade included.
  const cursiv::CstNode& root = r.cst()[clade->cstNodeId];
  if (root.begin != 0 || root.end != text.size()) return "<not the document>";
  return std::string(clade->name);
}

}  // namespace

void run() {
  std::size_t nodes = 0;
  {
    const auto e = tadopt::generated::G::create_parser();
    const std::string name = root_name(e, "A;", nodes);
    std::printf("token: root '%s', %zu node(s)\n", name.c_str(), nodes);
    CHECK(name == "A");
    CHECK(nodes == 1);
  }
  {
    const auto e = tadopt::generated::G::create_parser();
    const std::string name = root_name(e, "#!lua\nA", nodes);
    std::printf("shebang: root '%s', %zu node(s)\n", name.c_str(), nodes);
    CHECK(name == "A");
    CHECK(nodes == 1);
    CHECK(root_name(e, "A", nodes) == "A");
  }
  {
    const auto e = tadopt::generated::G::create_parser();
    const cursiv::ParseResult r = e->parse("inner A;");
    const auto* const doc = r.value_as<tadopt::ast::Doc>();
    CHECK(r.accepted && doc != nullptr && doc->clade != nullptr &&
          doc->clade->name == "A");
    std::printf("inner: %zu node(s)\n", r.nodes.size());
    CHECK(r.nodes.size() == 2);
  }
}

}  // namespace adopt_entry

// A grammar whose namespace holds a `cursiv` and a `std` of its own: its
// generated units compile there, under either engine, and read the text.
namespace shadowed_namespace {

namespace {

void reads(const std::unique_ptr<::cursiv::Parser>& parser) {
  using Doc = tshadow::cursiv::std::ast::Doc;
  const ::cursiv::ParseResult r = parser->parse("a bc d");
  CHECK(r.accepted && r.diagnostics.empty());
  const Doc* const doc = r.value_as<Doc>();
  CHECK(doc != nullptr && doc->items.size() == 3 && doc->items[1] == "bc");
}

}  // namespace

void run() {
  reads(tshadow::cursiv::std::generated::Lr::create_parser());
  reads(tshadow::cursiv::std::generated::Ll::create_parser());
}

}  // namespace shadowed_namespace

// A grammar header named like a generated sibling: the units that include it
// read the author's, and each module builds the author's types.
namespace sibling_header {

namespace {

bool reads(const cursiv::Parser& parser) {
  const cursiv::ParseResult r = parser.parse("word");
  const auto* const word = r.value_as<tsibling::Word>();
  return r.accepted && word != nullptr && word->name == "word";
}

}  // namespace

void run() {
  CHECK(reads(*tsibling::generated::Lr::create_parser()));
  CHECK(reads(*tsibling::generated::Module::create_parser()));
}

}  // namespace sibling_header

// What the grammar reaches, and the symbol space that follows from it. The
// graph is a type function, so its laws are `static_assert`s; the symbol
// space is a value, checked where it is built.
namespace lowering {

namespace {

using namespace cursiv;
using detail::tl_has;
using detail::tl_size;

// One terminal in the skipper, one reached only through a literal's guard, one
// reached from nowhere.
struct Doc : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct KW : terminal<lit<"end", !ref<ID>>> {};
  struct Never : terminal<lit<"never">> {};
  struct Word : rule<ref<ID>> {};
  struct Root : rule<ref<Word> + many(lit<","> + ref<Word>) + ref<KW>> {};
  using entry = Root;
  using ignored = WS;
};

using Dc = LrLowering<Doc>;

static_assert(tl_has<Doc::Root, Dc::rules>::value);
static_assert(tl_has<Doc::Word, Dc::rules>::value);
static_assert(tl_has<Doc::ID, Dc::rules>::value);
// A terminal named only inside another's guard is reached: the guard is an
// expression the lexer runs.
static_assert(tl_has<Doc::WS, Dc::rules>::value);
// ...and one the skipper names is a root of its own.
static_assert(tl_has<Doc::KW, Dc::rules>::value);
// A rule a grammar declares and never reaches is not in its language.
static_assert(!tl_has<Doc::Never, Dc::rules>::value);
static_assert(tl_size<Dc::rules>::value == 5);

// The split is total and the order is the closure's.
static_assert(tl_size<Dc::terminals>::value +
                  tl_size<Dc::nonterminals>::value ==
              tl_size<Dc::rules>::value);
static_assert(std::same_as<Dc::terminals, TypeList<Doc::WS, Doc::KW, Doc::ID>>);
static_assert(std::same_as<Dc::nonterminals, TypeList<Doc::Root, Doc::Word>>);

struct Other : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUM : terminal<some(d)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct Word : rule<ref<ID>> {};
  struct Root : rule<ref<Word>> {};
  using entry = Root;
  using ignored = WS;
};
// Restating a rule over a terminal the base never reached moves the graph: the
// closure reads the body in force, never the declared one.
struct Derived : extends<Other> {
  template <class>
  struct override;
  template <class T>
    requires std::same_as<T, Other::Word>
  struct override<T> : rule<ref<Other::NUM>> {};
};

using D = LrLowering<Derived>;
static_assert(tl_has<Other::NUM, D::rules>::value);
static_assert(!tl_has<Other::ID, D::rules>::value);

// On a grammar of its own: `Doc`'s guard names a rule, which the closure must
// reach and the token graph cannot lex -- the reachability above is about the
// graph, this is about the table.
struct Sym : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct KW : terminal<lit<"end", !cr<"a-zA-Z_">>> {};
  struct Word : rule<ref<ID>> {};
  struct Root : rule<ref<Word> + many(lit<","> + ref<Word>) + ref<KW>> {};
  using entry = Root;
  using ignored = WS;
};
using L = LrLowering<Sym>;

void the_symbol_space() {
  const auto& c = L::cfg();
  // Terminal 0 is end of input, nonterminal 0 the augmented start.
  CHECK(c.term_name(0) == "$end");
  CHECK(c.nt_name(0) == "$accept");
  // The three terminal rules, plus the one spelling the body holds.
  CHECK(c.nterms == 1 + 3 + 1);
  // The two rules, plus the two auxiliaries the repetition and its body need.
  CHECK(c.nnts == 1 + 2 + 2);
  // The order the graph was closed in is the numbering: the roots, then what
  // they reach, breadth first.
  CHECK(c.term_name(1) == "WS");
  CHECK(c.term_name(3) == "ID");
  CHECK(c.nt_name(1) == "Root");
  CHECK(c.nt_name(2) == "Word");
  CHECK(c.entryNt == 1);
  // Names and spellings are stored end to end, with explicit lengths.
  CHECK(c.nameLen == 4 + 2 + 2 + 2 +  // $end WS ID KW
                         3 +        // KW's own "end"
                         7 + 4 + 4 +  // $accept Root Word
                         1 + 2 + 2);  // "," _0 _1

  // Naming a literal does not unspell it: `KW` is offered and tie-broken as
  // the bytes it matches, not as the identifier it was declared under.
  CHECK(c.term_name(2) == "KW" && c.term_text(2) == "end");
  CHECK(c.terms[2].spelled);
  CHECK(!c.terms[3].spelled);  // ID matches, it does not spell
}

// A terminal's body becomes a tree of `LexNode` and byte classes; nothing of
// the grammar's types survives in it.
void the_token_graph() {
  const auto& c = L::cfg();
  CHECK(c.nlex != 0 && c.nsets != 0);
  // `ignored` is dropped, `hidden` is kept in the tree and out of the parse.
  CHECK(c.term_name(1) == "WS");
  CHECK(c.terms[1].trivia && !c.terms[1].recorded);
  // A keyword guards only where it could run into a word: `end` ends on a
  // letter, so its graph ends on the refusal of the class...
  const auto guarded = [&](unsigned t) {
    const LexNode& e = c.lex[c.termBuild[t].lex];
    return e.kind == LexKind::seq && e.b != 0 &&
           c.lex[c.lexKid[e.a + e.b - 1u]].kind == LexKind::nguard;
  };
  CHECK(c.term_name(2) == "KW" && guarded(2));
  // ...and a spelling that could not run into one refuses nothing.
  CHECK(c.term_name(4) == "," && !guarded(4));
}

// A choice becomes several, a repetition becomes left recursion, and an
// optional position is distributed over the productions of whoever holds it.
// No right-hand side is ever empty, and that is a property of the pass.
struct Pl : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct Item : rule<ref<ID> | lit<"*">> {};
  struct List : rule<lit<"["> + many(ref<Item>) + option(lit<"!">) + lit<"]">> {
  };
  using entry = List;
  using ignored = WS;
};

using P = LrLowering<Pl>;

// One right-hand side, spelled: `p` is the production, `k` the position.
std::uint16_t rhs(unsigned p, unsigned k) {
  return P::cfg().sym_at(P::cfg().prods[p], k);
}
std::uint16_t len(unsigned p) { return P::cfg().prods[p].len; }
std::uint16_t nt(unsigned i) { return nt_sym(i); }
constexpr std::uint16_t kList = 1, kItem = 2, kAux = 3;

void the_productions() {
  const auto& c = P::cfg();
  // $end, WS, ID, then the spellings in the order the bodies were walked.
  CHECK(c.nterms == 7);
  // $accept, List, Item, and the one auxiliary the repetition needs.
  CHECK(c.nnts == 4);
  CHECK(c.auxCount == 1);
  CHECK(c.nprods == 9);

  // The list is left recursion, never `| epsilon`: the empty case is the
  // caller's business, not the loop's.
  CHECK(c.prods[0].lhs == kAux && len(0) == 2 && rhs(0, 0) == nt(kAux) &&
        rhs(0, 1) == nt(kItem));
  CHECK(c.prods[1].lhs == kAux && len(1) == 1 && rhs(1, 0) == nt(kItem));

  // ...and the caller holds two optional positions, so it becomes four: one per
  // subset, and the shortest is still not empty.
  CHECK(c.prods[2].lhs == kList && len(2) == 2);
  CHECK(c.prods[3].lhs == kList && len(3) == 3 && rhs(3, 1) == nt(kAux));
  CHECK(c.prods[4].lhs == kList && len(4) == 3);
  CHECK(c.prods[5].lhs == kList && len(5) == 4 && rhs(5, 1) == nt(kAux));

  // A choice of two is two productions of the rule itself -- no auxiliary: the
  // alternatives are the rule's.
  CHECK(c.prods[6].lhs == kItem && len(6) == 1);
  CHECK(c.prods[7].lhs == kItem && len(7) == 1);

  // `$accept -> entry`, last, and it is what `startProd` names.
  CHECK(c.prods[8].lhs == 0 && rhs(8, 0) == nt(kList));
  CHECK(c.startProd == 8);

  // Not one empty right-hand side, anywhere. The construction's lookaheads are
  // computable because of this and nothing else.
  for (unsigned k = 0; k < c.nprods; ++k) CHECK(c.prods[k].len != 0);
}

// `E -> E op E` per operator, and the ambiguity is deliberate: what
// separates the readings is the step, which the construction resolves. The
// synthesized body `P (ops P)*` would say the same language and group it
// wrong.
namespace ast {
struct Node : AstNode {};
struct Num : Node {
  using ast_bases = TypeList<Node>;
  double value = 0;
};
struct Bin : Node {
  using ast_bases = TypeList<Node>;
  Node* left = nullptr;
  std::string op;
  Node* right = nullptr;
};
}  // namespace ast

struct Ex : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUM : terminal<as<double>(some(d))> {};
  struct Atom : rule<create<ast::Num> + assign<&ast::Num::value>(ref<NUM>)> {};
  struct Expr : infix<Atom, ast::Bin,
                      levels(left(lit<"*">), left(lit<"+"> | lit<"-">))> {};
  using entry = Expr;
  using ignored = WS;
};

using X = LrLowering<Ex>;

// The same table, its operators named. A terminal that states no conversion
// reads its span exactly as the literal does, so naming it may not change one
// row of the graph.
struct ExNamed : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct NUM : terminal<as<double>(some(d))> {};
  struct STAR : terminal<lit<"*">> {};
  struct PLUS : terminal<lit<"+">> {};
  struct MINUS : terminal<lit<"-">> {};
  struct Atom : rule<create<ast::Num> + assign<&ast::Num::value>(ref<NUM>)> {};
  struct Expr : infix<Atom, ast::Bin,
                      levels(left(ref<STAR>), left(ref<PLUS> | ref<MINUS>))> {};
  using entry = Expr;
  using ignored = WS;
};

using XN = LrLowering<ExNamed>;

void a_precedence_table() {
  const auto& c = X::cfg();
  const auto prod = [&](unsigned k) { return c.prods[k]; };
  const auto xsym = [&](unsigned k, unsigned i) {
    return c.sym_at(c.prods[k], i);
  };
  // Three operators over two steps, then the operand; the primary's own
  // production and the augmented start close the count.
  CHECK(c.nprods == 6);
  // Tightest binds hardest: the table declares them tightest first, and a
  // step's number is what the resolution compares.
  CHECK(prod(0).len == 3 && c.prodBuild[0].prec == 2);
  CHECK(c.prodBuild[1].prec == 1 && c.prodBuild[2].prec == 1);
  // The operand ends the chain and sits on no step.
  CHECK(prod(3).len == 1 && c.prodBuild[3].prec == 0);
  // A production of the table is `E op E`: both operands are the rule itself.
  CHECK(xsym(0, 0) == xsym(0, 2) && xsym(0, 0) == nt_sym(c.entryNt));
  // The step and the way it groups ride the operator: a shift/reduce covered by
  // one is decided at construction, not by a fork.
  CHECK(c.termBuild[xsym(0, 1)].prec == 2);
  CHECK(c.termBuild[xsym(0, 1)].assoc == cursiv::Association::left);
  CHECK(!X::table().fault);

  // An action says what is done, never where: the three operators share one
  // creation and three writes, whichever of them the reduce was.
  CHECK(prod(0).node == prod(1).node && prod(1).node == prod(2).node);
  CHECK(c.row_at(prod(0), 0) == c.row_at(prod(1), 0));

  // Naming the operators changes nothing the graph is made of: the same
  // symbols, the same productions, the same steps. Row numbers are per
  // grammar, so what is compared is their agreement, not their value.
  const auto& n = XN::cfg();
  CHECK(n.nprods == c.nprods && n.nnts == c.nnts && n.nterms == c.nterms);
  for (unsigned k = 0; k < c.nprods; ++k) {
    CHECK(n.prods[k].len == c.prods[k].len);
    CHECK(n.prodBuild[k].prec == c.prodBuild[k].prec);
  }
  // The three named operators write the member through ONE row, exactly as
  // three literals do. A row apiece would stop the table's productions from
  // sharing a class, and the automaton would carry one state per operator
  // instead of one per step.
  CHECK(n.row_at(n.prods[0], 1) == n.row_at(n.prods[1], 1));
  CHECK(n.row_at(n.prods[1], 1) == n.row_at(n.prods[2], 1));
  CHECK(n.prods[0].node == n.prods[1].node &&
        n.prods[1].node == n.prods[2].node);
}

// A rule whose match is one element lays no record: its actions ride the record
// below, which spans the same text. A run of such rules puts a succession of
// actions on one node instead of a comb of nested ones.
namespace ast {
struct Name : Node {
  using ast_bases = TypeList<Node>;
  std::string name;
};
struct Wrap : Node {
  using ast_bases = TypeList<Node>;
  Name* name = nullptr;
};
struct Top : Node {
  using ast_bases = TypeList<Node>;
  Wrap* test = nullptr;
};
}  // namespace ast

struct Chain : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct Name : rule<assign<&ast::Name::name>(ref<ID>)> {};
  struct Wrap : rule<assign<&ast::Wrap::name>(ref<Name>)> {};
  struct Top : rule<assign<&ast::Top::test>(ref<Wrap>)> {};
  using entry = Top;
  using ignored = WS;
};

using Ch = LrLowering<Chain>;

// The action's id is its position in the published list, so a reader decides
// what it is by asking that list -- exactly as the AST builder does.
template <WriteKind... Ks, class... Ts, class... Ms, class... Ns, class... Ss>
constexpr auto kinds_of(TypeList<detail::Row<Ks, Ts, Ms, Ns, Ss>...>) {
  return std::array<WriteKind, sizeof...(Ks)>{Ks...};
}
constexpr auto ch_kinds = kinds_of(Ch::actions{});

void one_node_a_succession_of_actions() {
  const auto& c = Ch::cfg();
  const auto& acts = Ch::table().writes;
  // Each rule opens a record before it fills it: the creation the rule owes,
  // then the write its body states over the record below.
  const auto opens_then_fills = [&](std::uint16_t r) {
    const cursiv::Chain ch = acts.chains[r];
    const ChainBuild cb = acts.chainBuild[r];
    return ch.count == 2 && cb.makes &&
           ch_kinds[acts.acts[ch.off]] == WriteKind::create &&
           ch_kinds[acts.acts[ch.off + 1]] == WriteKind::assign;
  };
  // Three rules, three productions, and the augmented start.
  CHECK(c.nprods == 4);
  // `Top -> Wrap` and `Wrap -> Name` lay one: the symbol below opens a node of
  // its own, and two creations cannot share a record -- they are two nodes over
  // one span. So a chain that opens one never rides down onto another.
  CHECK(opens_then_fills(c.prods[0].node));
  CHECK(opens_then_fills(c.prods[1].node));
  // `Name -> ID` hands its chain down, and that is the whole point of a chain:
  // the terminal's record only says what its span denotes, so the rule's
  // creation and write ride onto it instead of laying a second record over the
  // same text.
  CHECK(c.prods[2].node == kNoRow);
  CHECK(opens_then_fills(c.row_at(c.prods[2], 0)));

  // A chain that opens a record, and one that does nothing else. Read off the
  // action's kind, so no table beside the chains falls out of step with them.
  const ChainBuild own = acts.chainBuild[c.prods[0].node];
  CHECK(own.makes);
  // The ID rule's conversion is one action and opens nothing.
  bool convert_makes_nothing = false;
  for (std::uint16_t i = 0; i < acts.nchains; ++i)
    if (acts.chains[i].count == 1 && !acts.chainBuild[i].makes)
      convert_makes_nothing = true;
  CHECK(convert_makes_nothing);
}

// The same rule spanning several symbols has no record below to ride, so it
// lays one -- and the call site's write renames that instead of a leaf.
struct Dotted : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct QName : rule<ref<ID> + many(lit<"."> + ref<ID>)> {};
  struct Root : rule<assign<&ast::Name::name>(ref<QName>) + lit<"A">> {};
  using entry = Root;
  using ignored = WS;
};

using Dot = LrLowering<Dotted>;

void the_record_that_is_needed() {
  const auto& c = Dot::cfg();
  // `QName -> ID` and `QName -> ID _1`: the two spellings are two productions,
  // which is what makes the answer static. The first rides, the second lays.
  const auto qname_prod = [&](unsigned n) {
    for (unsigned k = 0; k < c.nprods; ++k)
      if (c.prods[k].lhs == 2 && c.prods[k].len == n) return k;
    return ~0u;
  };
  CHECK(c.prods[qname_prod(1)].node == kNoRow);
  CHECK(c.prods[qname_prod(2)].node != kNoRow);
  // The conversion is owed either way: eliding the record must never elide it.
  CHECK(c.row_at(c.prods[qname_prod(1)], 0) != kNoRow);
}

// ...and the demand travels down a pass-through, which lays none of its own:
// the site names `Line`, the record is owed by whoever spans the text.
struct Relay : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct Named : rule<create<ast::Name>(assign<&ast::Name::name>(ref<ID>))> {};
  struct Isle : rule<lit<"["> + ref<Named> + lit<"]">> {};
  struct Line : rule<ref<Isle>> {};
  struct Root : rule<create<ast::Wrap>(assign<&ast::Wrap::name>(ref<Line>) +
                                       lit<"A">)> {};
  using entry = Root;
  using ignored = WS;
};

using Rl = LrLowering<Relay>;

void the_demand_travels_down() {
  const auto& c = Rl::cfg();
  const auto nt_named = [&](std::string_view n) {
    for (std::uint16_t i = 0; i < c.nnts; ++i)
      if (c.nt_name(i) == n) return i;
    return kNoRow;
  };
  const auto prod_of = [&](std::uint16_t lhs) {
    for (unsigned k = 0; k < c.nprods; ++k)
      if (c.prods[k].lhs == lhs) return k;
    return ~0u;
  };
  CHECK(c.prods[prod_of(nt_named("Line"))].node == kNoRow);
  CHECK(c.prods[prod_of(nt_named("Isle"))].node != kNoRow);
}

// A grammar spells the same list at two places and each spelling minted its
// own auxiliary. They derive the same language with the same actions, so they
// are one -- and the second's productions are dead.
struct Twin : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct Root : rule<lit<"("> + many(ref<ID>) + lit<")"> + lit<"["> +
                     many(ref<ID>) + lit<"]">> {};
  using entry = Root;
  using ignored = WS;
};

using Tw = LrLowering<Twin>;

void two_auxiliaries_that_say_the_same_thing() {
  const auto& c = Tw::cfg();
  // Two auxiliaries were minted...
  CHECK(c.auxCount == 2);
  // ...and one loop survives: `R -> R ID | ID`, then four productions of `Root`
  // for the two optional lists, then the augmented start.
  CHECK(c.nprods == 2 + 4 + 1);
  // Nothing derives the merged one any more.
  const std::uint16_t merged = static_cast<std::uint16_t>(c.nnts - 1);
  for (unsigned k = 0; k < c.nprods; ++k) {
    CHECK(c.prods[k].lhs != merged);
    for (unsigned i = 0; i < c.prods[k].len; ++i)
      CHECK(c.sym_at(c.prods[k], i) != nt_sym(merged));
  }
}

// The regime rides the element, not the rule: a rule whose own body opens a
// region has a tight interior and an ambient entry. What runs between the
// body's elements is the region's business; what runs in front of it is the
// caller's.
struct Region : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct Key : rule<ref<ID> + many(lit<"."> + ref<ID>)> {};
  struct Root : rule<tight(lit<"["> + ref<Key> + lit<"]">)> {
  };
  using entry = Root;
  using ignored = WS;
};

using Rg = LrLowering<Region>;

void a_regions_boundaries() {
  const auto& c = Rg::cfg();
  unsigned at = 0;
  for (unsigned k = 0; k < c.nprods; ++k)
    if (c.prods[k].len == 3) at = k;
  const std::uint16_t pi = static_cast<std::uint16_t>(at);
  // Bit k is the boundary before `rhs[k]`, and bit `len` the one after the
  // last symbol. Both edges are the caller's, and only what runs between the
  // body's elements is the region's business: a boundary the region owned on
  // the way out would refuse the trivia after it.
  CHECK(c.cfg_at(pi, 0) == 0 && c.cfg_at(pi, 1) != 0 && c.cfg_at(pi, 2) != 0 &&
        c.cfg_at(pi, 3) == 0);
  // A region asks for a second reading of the rules it reaches, so the clone
  // half mirrors the rules and is reserved before any auxiliary: a clone's
  // index is its rule's plus a fixed count, and an auxiliary minted in between
  // would move it.
  CHECK(c.nt_name(3) == c.nt_name(1));
  CHECK(c.nt_name(4) == c.nt_name(2));
  // ...and only the rule the region actually reaches owes one.
  CHECK(c.cloneNeeded[4] && !c.cloneNeeded[3]);
}

// A join is what its two halves say, whole -- the pair over a position is the
// one chain nothing else reads it back from -- and the same pair joined twice
// is one entry.
bool join_keeps_everything() {
  constexpr unsigned kWide = 20;
  ChainPool w{};
  std::uint16_t a[kWide]{}, b[kWide]{};
  for (unsigned i = 0; i < kWide; ++i) {
    a[i] = static_cast<std::uint16_t>(i);
    b[i] = static_cast<std::uint16_t>(kWide + i);
  }
  // Sequenced, and it has to be: the two internings decide which chain is
  // first, and the order of a call's arguments is not the order they run in.
  const std::uint16_t ia = w.intern_chain(a, kWide);
  const std::uint16_t ib = w.intern_chain(b, kWide);
  const std::uint16_t r = w.concat(ia, ib);
  const cursiv::Chain ch = w.chains[r];
  if (ch.count != 2 * kWide) return false;
  const std::uint16_t* const ac = w.acts.data();
  for (unsigned i = 0; i < 2 * kWide; ++i)
    if (ac[ch.off + i] != i) return false;
  return w.concat(0, 1) == r && w.nchains == 3;
}

// ...and the run a position owes is as long as the directives standing there.
namespace ast {
struct Flags : Node {
  using ast_bases = TypeList<Node>;
  bool on = false;
};
struct Box : Flags {
  using ast_bases = TypeList<Flags>;
  Flags* in = nullptr;
};
}  // namespace ast

template <unsigned N>
constexpr auto stacked =
    nest<ast::Box, &ast::Box::in>(flag<&ast::Flags::on>(stacked<N - 1>));
template <>
constexpr auto stacked<0> = kw<"x">;

constexpr unsigned kDeep = 20;

struct Stack : GrammarSpec {
  struct WS : terminal<some(s)> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};
  struct Root : rule<create<ast::Flags>(ref<ID> + stacked<kDeep>)> {};
  using entry = Root;
  using ignored = WS;
};

using Stk = LrLowering<Stack>;
constexpr auto stk_kinds = kinds_of(Stk::actions{});

void a_run_of_actions_has_no_cap() {
  CHECK(join_keeps_everything());
  CHECK(Stk::table().fault.code == GrammarFaultCode::none);
  const auto& w = Stk::table().writes;
  const auto whole = [&w](WriteKind kind, bool turn) {
    for (std::uint16_t i = 0; i < w.nchains; ++i) {
      if (w.chains[i].count != kDeep || w.chainBuild[i].turn != turn) continue;
      bool same = true;
      for (unsigned k = 0; k < kDeep; ++k)
        same = same && stk_kinds[w.acts[w.chains[i].off + k]] == kind;
      if (same) return true;
    }
    return false;
  };
  // The turn's record buries what stands before it, the position under it
  // keeps the conditions spelled over it.
  CHECK(whole(WriteKind::nest, true));
  CHECK(whole(WriteKind::flag, false));
}

}  // namespace

void run() {
  the_symbol_space();
  the_token_graph();
  the_productions();
  a_precedence_table();
  one_node_a_succession_of_actions();
  the_record_that_is_needed();
  the_demand_travels_down();
  two_auxiliaries_that_say_the_same_thing();
  a_regions_boundaries();
  a_run_of_actions_has_no_cap();
}

}  // namespace lowering

int main() {
  ast_fused::run();
  adopt_entry::run();
  shadowed_namespace::run();
  sibling_header::run();
  lowering::run();
  return cursiv::test::report("LrAst_test");
}
