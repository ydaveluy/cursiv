#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "door/generated/G/Module.hpp"
#include "engines/lr/DoorGrammar.hpp"
#include "engines/lr/ParseChecks.hpp"
#include "engines/lr/PassesGrammar.hpp"
#include "passes/generated/G/Module.hpp"

namespace lr_passes_cases {

// The cases of the passes grammar: construction passes, ranked cells and
// soft keywords.

// The construction's passes keep the language: what a table by default reads
// is what the grammar declares.
namespace passes {

namespace {

const cursiv::Parser& passes() {
  static const auto p = tpasses::generated::G::create_parser();
  return *p;
}

using checks::clean, checks::repaired;

// A case of the passes grammar: its opening keyword, then the text.
bool clean(std::string_view head, std::string_view text) {
  return clean(passes(), std::string(head) + std::string(text));
}
bool repaired(std::string_view head, std::string_view text) {
  return repaired(passes(), std::string(head) + std::string(text));
}

// Two states whose gotos name distinct auxiliaries of one shape stay apart.
void minimisation_keeps_every_goto() {
  for (const std::string_view t :
       {"b k x", "b k y", "b x", "b y", "a x", "a y", "c x", "c y", "d k x",
        "d k y", "d x", "d y"})
    CHECK(clean("min ", t));
}

// A unit production restricted to `$end` keeps the restriction through the
// unit passes.
void end_assertion_survives_the_unit_passes() {
  CHECK(clean("end ", "! a #"));
  CHECK(clean("end ", "! a @ ;"));
  CHECK(repaired("end ", "! a # ;"));
}

// Distinct terminal alternatives may share the same unit continuation, but
// their shifted states must still identify the token written to the CST.
void terminal_alternatives_keep_their_continuations() {
  for (const auto text : {"(a*==$", "(a&==$", "(a* = =$", "(a& = =$",
                           ")a*==$", ")a&==$"})
    CHECK(clean("end ", text));
  CHECK(repaired("end ", ")a* = =$"));
  CHECK(repaired("end ", ")a& = =$"));
}

// The entry's empty production adds no action of the production after it.
void an_empty_production_reads_no_neighbour() {
  const auto p = tpasses::generated::G::create_parser();
  CHECK(clean(*p, ""));
  CHECK(clean(*p, "k m x"));
  CHECK(repaired(*p, "m x"));
  CHECK(repaired(*p, "m"));
}

// A cell the second token would decide keeps its fork when a reduce lands
// in another lex mode.
void a_peek_never_crosses_a_lex_mode() {
  CHECK(clean("peek ", "b x in foo"));
  CHECK(clean("peek ", "b x foo bar"));
  CHECK(clean("peek ", "a x in z"));
  CHECK(clean("peek ", "a x in foo bar baz"));
  CHECK(repaired("peek ", "b x in"));

  // Completion supplies its own lookahead. Returning through C changes the
  // mode, where `in` can be an ID; D can still read the named IN terminal.
  const cursiv::Parser* const p = &passes();
  for (const std::string body : {"b x in foo", "b x foo bar"}) {
    const std::string head = "peek ";
    const std::string text = head + body;
    const auto from = static_cast<cursiv::TextOffset>(head.size() + 4);
    const auto end = static_cast<cursiv::TextOffset>(text.find(' ', from));
    bool id = false, in = false;
    for (const auto& feature :
         checks::features(*p, p->parse(text), from, end)) {
      const auto name = p->getGrammar().terminalName(feature.feature);
      const auto spellings = p->getGrammar().terminalSpellings(feature.feature);
      const bool spellsIn = spellings.size() == 1 && spellings.front() == "in";
      id = id || name == "ID";
      in = in || spellsIn;
      CHECK(name == "ID" || spellsIn);
    }
    CHECK(id && in);
  }
}

// An unbounded repetition keeps its floor.
void an_unbounded_repetition_keeps_its_floor() {
  CHECK(repaired("repeat ", "a"));
  CHECK(clean("repeat ", "a b"));
  CHECK(clean("repeat ", "a b c d"));
}

}  // namespace

void run() {
  minimisation_keeps_every_goto();
  end_assertion_survives_the_unit_passes();
  terminal_alternatives_keep_their_continuations();
  an_empty_production_reads_no_neighbour();
  a_peek_never_crosses_a_lex_mode();
  an_unbounded_repetition_keeps_its_floor();
}

}  // namespace passes

// A soft keyword: the word is a keyword in one alternative and a name in
// another, and what follows decides -- once the grammar spells it as one
// terminal on both sides. Against the plain identifier, the keyword takes
// the spelling wherever the state reads it, as the reference's single lexer
// would.
namespace soft_keyword {

namespace {

// The names a case reads behind its opening keyword `head`.
template <class Engine>
std::string names(const Engine& engine, std::string_view head,
                  std::string_view body) {
  const std::string text = std::string(head) + std::string(body);
  const cursiv::ParseResult r = engine->parse(text);
  const auto* const doc = r.value_as<tsoft::ast::Doc>();
  if (!r.accepted || doc == nullptr) return "<refused>";
  std::string out;
  for (const auto n : doc->names) {
    if (!out.empty()) out += ' ';
    out += n;
  }
  if (!r.diagnostics.empty()) out += " <repaired>";
  return out;
}

}  // namespace

void run() {
  const auto e = tpasses::generated::G::create_parser();
  std::printf("spelled: '%s' | '%s' | '%s'\n",
              names(e, "soft ", "type = 5").c_str(),
              names(e, "soft ", "type x = y").c_str(),
              names(e, "soft ", "type type = type").c_str());
  CHECK(names(e, "soft ", "type = 5") == "type");
  CHECK(names(e, "soft ", "type x = y") == "x y");
  CHECK(names(e, "soft ", "type type = type") == "type type");
  std::printf("named: '%s' | '%s'\n", names(e, "named ", "type = 5").c_str(),
              names(e, "named ", "type x = y").c_str());
  CHECK(names(e, "named ", "type = 5") == "type");
  CHECK(names(e, "named ", "type x = y") == "x y");
  std::printf("plain: '%s' | '%s' | '%s'\n",
              names(e, "soft ", "{get: 1}").c_str(),
              names(e, "soft ", "{get x {}}").c_str(),
              names(e, "soft ", "{a: 1, get: 2}").c_str());
  CHECK(names(e, "soft ", "{get: 1}") == " <repaired>");
  CHECK(names(e, "soft ", "{get x {}}") == "x");
  CHECK(names(e, "soft ", "{a: 1, get: 2}") == "a <repaired>");
}

}  // namespace soft_keyword

// A reduce/reduce cell takes the order of the choice it stands in, not of the
// nearest choice anywhere in the grammar.
namespace rank_context {

namespace {

// `A`, `B`, or `-` where the parse complained, behind the grammar's keyword.
char kid(const cursiv::Parser& p, std::string_view head,
         std::string_view body) {
  const std::string text = std::string(head) + std::string(body);
  const cursiv::ParseResult r = p.parse(text);
  if (!r.accepted || !r.diagnostics.empty()) return '-';
  const auto* const d = r.value_as<trank::ast::Doc>();
  if (d == nullptr || d->kid == nullptr) return '-';
  if (cursiv::ast_cast<trank::ast::AKid>(d->kid) != nullptr) return 'A';
  if (cursiv::ast_cast<trank::ast::BKid>(d->kid) != nullptr) return 'B';
  return '?';
}

}  // namespace

void run() {
  const auto p = tpasses::generated::G::create_parser();
  for (const std::string_view head : {"plain ", "grown "}) {
    CHECK(kid(*p, head, "x c") == 'A');
    CHECK(kid(*p, head, "y c z") == 'B');
  }
  CHECK(kid(*p, "context ", "x c w") == 'A');
  CHECK(kid(*p, "context ", "y c w") == 'B');
}

}  // namespace rank_context

// A look-ahead answers on the text, not on how many reductions an arm
// performs before it reads: two arms the same token refutes tie, and the tie
// is the grammar's preference.
namespace settle_phase {

namespace {

namespace ast = tphase::ast;

// The kid of the settle case's reading; the tree lives until the next call.
const ast::Kid* kid_of(const cursiv::Parser& p, std::string_view body) {
  static std::string text;
  static cursiv::ParseResult r;
  text = "settle " + std::string(body);
  r = p.parse(text);
  CHECK(r.accepted);
  const auto* doc = r.value_as<ast::Doc>();
  return doc != nullptr ? doc->kid : nullptr;
}

}  // namespace

void run() {
  const auto parser = tpasses::generated::G::create_parser();
  CHECK(cursiv::ast_cast<ast::AKid>(kid_of(*parser, "c x y q")) != nullptr);
  CHECK(cursiv::ast_cast<ast::Wrap>(kid_of(*parser, "c x y r")) != nullptr);
  CHECK(cursiv::ast_cast<ast::AKid>(kid_of(*parser, "c x #")) != nullptr);
  CHECK(cursiv::ast_cast<ast::AKid>(kid_of(*parser, "c x y #")) != nullptr);
}

}  // namespace settle_phase

// The small languages of the door grammar, read through the parser.

// The walk reads to where the automaton dies and hands back the last place it
// accepted. Where a longer spelling stays alive to the end of an unbounded
// run and the shorter one wins anyway, every position re-reads that run: the
// same bytes, once per byte. `a+b` beside `a` over sixteen thousand `a`
// doubles four times for every doubling of the text.
//
// The reading is right, which is what this pins; the cost is not, and no memo
// of how far the automaton can run from a place is kept.
namespace maximal_munch {

void run() {
  const auto engine = tdoor::generated::G::create_parser();
  for (const std::size_t n : {std::size_t{64}, std::size_t{4000}}) {
    const std::string text = "munch " + std::string(n, 'a');
    const cursiv::ParseResult r = engine->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
    const auto* const doc = r.value_as<tmunch::ast::Doc>();
    CHECK(doc != nullptr);
    // One name per byte: the long spelling never closes, so the short one
    // takes every position.
    if (doc != nullptr) CHECK(doc->parts.size() == n);
  }
  // ...and the closed one is one name.
  const std::string closed = "munch " + std::string(64, 'a') + "b";
  const cursiv::ParseResult r = engine->parse(closed);
  CHECK(r.accepted && r.diagnostics.empty());
  if (const auto* const doc = r.value_as<tmunch::ast::Doc>())
    CHECK(doc->parts.size() == 1);
  std::printf("  a run read one name at a time, and closed read as one\n");
}

}  // namespace maximal_munch

// A run read eight bytes at a time answers a byte past 0x7F by its high bit
// alone, the ranges being read off the low seven. A class holding some of
// that half and not all is therefore not a shape the step can read: it would
// stop on a byte of its own, and the jump past the byte loop means nothing
// looks again.
namespace high_byte {

void run() {
  const auto engine = tdoor::generated::G::create_parser();
  // Long enough for the eight-byte step to run at all.
  const std::string texts[] = {"abcdefghij",
                               "abcd\x85"
                               "fghij",
                               "\x85\x85\x85\x85\x85\x85\x85\x85\x85",
                               "abcdefgh\x85j",
                               "abcdefghi\x8f",
                               "\x80"
                               "bcdefghij"};
  unsigned read = 0;
  for (const std::string& text : texts) {
    const std::string doc = "high " + text;
    const cursiv::ParseResult r = engine->parse(doc);
    if (r.accepted && r.diagnostics.empty()) {
      ++read;
      continue;
    }
    std::printf("FAIL: %zu bytes of one class read as %zu repairs\n",
                text.size(), r.diagnostics.size());
    ++failures;
  }
  std::printf("  %u names over a class whose high half is partial\n", read);
  for (const std::string text : {"abc?\?=", "abc\xff\xfe"}) {
    const std::string doc = "high " + text;
    const cursiv::ParseResult r = engine->parse(doc);
    CHECK(r.accepted && r.diagnostics.empty());
  }
}

}  // namespace high_byte

// A precedence table reads its operators from data rules: the step rides the
// token the rule opens on, the trivia between its words is the skipper's, and
// the rule's converter hands `op` its value.
namespace infix_op {

namespace {

using tinfix::Cmp;
using tinfix::ast::Bin;
using tinfix::ast::Name;

const Bin* as_bin(const cursiv::AstNode* n) {
  return n != nullptr && n->type == &cursiv::ast_type_tag<Bin>
             ? static_cast<const Bin*>(n)
             : nullptr;
}
std::string_view as_name(const cursiv::AstNode* n) {
  return n != nullptr && n->type == &cursiv::ast_type_tag<Name>
             ? static_cast<const Name*>(n)->name
             : std::string_view{};
}

// The tree borrows the document, so the document outlives the reads.
void one_operator_of_several_words(const cursiv::Parser& p,
                                   std::string_view text, Cmp op) {
  const std::string doc = "infix " + std::string(text);
  const cursiv::ParseResult r = p.parse(doc);
  CHECK(r.accepted && r.diagnostics.empty());
  const Bin* b = as_bin(r.value);
  CHECK(b != nullptr);
  if (b == nullptr) return;
  CHECK(b->op == op);
  CHECK(as_name(b->left) == "a" && as_name(b->right) == "b");
}

void the_words_and_the_trivia_between_them() {
  const auto p = tdoor::generated::G::create_parser();
  one_operator_of_several_words(*p, "a is b", Cmp::is);
  one_operator_of_several_words(*p, "a is not b", Cmp::is_not);
  one_operator_of_several_words(*p, "a != b", Cmp::is_not);
  one_operator_of_several_words(*p, "a is less b", Cmp::less);
  one_operator_of_several_words(*p, "a is less or equal b", Cmp::leq);
  one_operator_of_several_words(*p, "a is /* c */ not b", Cmp::is_not);
  one_operator_of_several_words(*p, "a is\n  less\n  or /* c */ equal b",
                                Cmp::leq);
}

void the_step_rides_the_opening_token() {
  const auto p = tdoor::generated::G::create_parser();
  const cursiv::ParseResult r = p->parse("infix a is not b and c is less d");
  CHECK(r.accepted && r.diagnostics.empty());
  const Bin* root = as_bin(r.value);
  CHECK(root != nullptr && root->op == Cmp::both);
  if (root == nullptr) return;
  const Bin* l = as_bin(root->left);
  const Bin* rr = as_bin(root->right);
  CHECK(l != nullptr && l->op == Cmp::is_not && as_name(l->left) == "a");
  CHECK(rr != nullptr && rr->op == Cmp::less && as_name(rr->right) == "d");

  const cursiv::ParseResult s = p->parse("infix a is less b is c");
  const Bin* top = as_bin(s.value);
  CHECK(top != nullptr && top->op == Cmp::is && as_name(top->right) == "c");
  if (top == nullptr) return;
  const Bin* inner = as_bin(top->left);
  CHECK(inner != nullptr && inner->op == Cmp::less);
}

}  // namespace

void run() {
  the_words_and_the_trivia_between_them();
  the_step_rides_the_opening_token();
}

}  // namespace infix_op

// A statement whose expression read no operator still builds: the infix
// rule hands its operand back, and the rule adopting it closes on the base
// the operand and the nest share.
namespace nest_after_infix {

namespace {

using namespace tnestinfix::ast;

void spell(const Expr* e, std::string& out) {
  if (const auto* const i = cursiv::ast_cast<Ident>(e)) {
    out += i->name;
    return;
  }
  if (const auto* const n = cursiv::ast_cast<IntLit>(e)) {
    out += std::to_string(n->value);
    return;
  }
  if (const auto* const a = cursiv::ast_cast<Assign>(e)) {
    out += '(';
    out += a->target->name;
    out += '=';
    spell(a->value, out);
    out += ')';
    return;
  }
  if (const auto* const b = cursiv::ast_cast<Bin>(e)) {
    out += '(';
    spell(b->left, out);
    out += b->op;
    spell(b->right, out);
    out += ')';
    return;
  }
  out += '?';
}

std::string read(std::string_view text) {
  static const auto p = tdoor::generated::G::create_parser();
  const std::string doc = "after " + std::string(text);
  const cursiv::ParseResult r = p->parse(doc);
  if (!r.accepted || !r.diagnostics.empty()) return "<refused>";
  const auto* const s = r.value_as<Stmt>();
  if (s == nullptr) return "<no stmt>";
  if (s->value == nullptr) return "";
  std::string out;
  spell(s->value, out);
  return out;
}

bool same(std::string_view text, std::string_view want) {
  return checks::matches(text, read(text), want);
}

namespace paren {

namespace pa = tnestinfix::paren::ast;

void spell(const pa::Ty* t, std::string& out) {
  if (const auto* const n = cursiv::ast_cast<pa::Named>(t)) {
    out += n->name;
    return;
  }
  if (const auto* const b = cursiv::ast_cast<pa::Bin>(t)) {
    out += '(';
    spell(b->left, out);
    out += b->op;
    spell(b->right, out);
    out += ')';
    return;
  }
  out += '?';
}

std::string read(std::string_view text) {
  static const auto p = tdoor::generated::G::create_parser();
  const std::string doc = "after " + std::string(text);
  const cursiv::ParseResult r = p->parse(doc);
  if (!r.accepted || !r.diagnostics.empty()) return "<refused>";
  std::string out;
  spell(r.value_as<pa::Decl>()->type, out);
  return out;
}

bool same(std::string_view text, std::string_view want) {
  return checks::matches(text, read(text), want);
}

}  // namespace paren

}  // namespace

void run() {
  // A lone operand, assigned or not.
  CHECK(same("i;", "i"));
  CHECK(same("1;", "1"));
  CHECK(same("i=1;", "(i=1)"));
  CHECK(same("i=j;", "(i=j)"));
  // ...and with an operator somewhere.
  CHECK(same("1+2;", "(1+2)"));
  CHECK(same("i=1+2;", "(i=(1+2))"));
  CHECK(same("i<1;", "(i<1)"));
  CHECK(same("i+1<j+2;", "((i+1)<(j+2))"));
  CHECK(same(";", ""));
  // Through a parenthesis, the cycle back to `Expr` included.
  CHECK(same("(i);", "i"));
  CHECK(same("(i+1)<2;", "((i+1)<2)"));
  CHECK(same("j=(i);", "(j=i)"));
  // The parenthesis through a pass-through rule, as typescript spells it.
  CHECK(paren::same("let : a", "a"));
  CHECK(paren::same("let : a | b", "(a|b)"));
  CHECK(paren::same("let : (a | b)", "(a|b)"));
  CHECK(paren::same("let : ((a)) | (b | c)", "(a|(b|c))"));
}

}  // namespace nest_after_infix

// A nest finished inside a recovered region wraps nothing outside it: the
// document keeps a tree, and every node in it stands inside its owner.
namespace nest_recovered {

namespace {

using namespace tnestrec::ast;

struct Walk {
  const cursiv::ParseResult& r;
  std::string why;

  void child(const cursiv::AstNode* owner, const cursiv::AstNode* c,
             const char* what, bool inside = true) {
    if (!why.empty()) return;
    if (c == nullptr) {
      why = std::string("null ") + what;
      return;
    }
    if (c->parent != owner) why = std::string("parent of ") + what;
    const auto& o = r.cst()[owner->cstNodeId];
    const auto& k = r.cst()[c->cstNodeId];
    if (inside && (k.begin < o.begin || k.end > o.end))
      why = std::string("outside its owner: ") + what;
    node(c);
  }
  void node(const cursiv::AstNode* n) {
    if (const auto* const b = cursiv::ast_cast<Bin>(n)) {
      child(n, b->left, "Bin.left");
      child(n, b->right, "Bin.right");
    } else if (const auto* const c = cursiv::ast_cast<Call>(n)) {
      // A wrapper's span is its own region: the callee stands before it.
      child(n, c->callee, "Call.callee", false);
      for (const auto* const a : c->args) child(n, a, "Call.args");
    } else if (const auto* const m = cursiv::ast_cast<Member>(n)) {
      child(n, m->receiver, "Member.receiver", false);
    } else if (const auto* const s = cursiv::ast_cast<Stmt>(n)) {
      child(n, s->expr, "Stmt.expr");
    } else if (const auto* const d = cursiv::ast_cast<Model>(n)) {
      for (const auto* const s2 : d->stmts) child(n, s2, "Model.stmts");
    }
  }
};

std::string verdict(std::string_view text) {
  static const auto p = tdoor::generated::G::create_parser();
  const std::string doc = "nest " + std::string(text);
  const cursiv::ParseResult r = p->parse(doc);
  if (!r.accepted) return "refused";
  if (r.error != cursiv::AstBuildError::none || r.value == nullptr)
    return "no tree";
  for (const auto* node : r.nodes) {
    const auto* ancestor = node;
    std::size_t left = r.nodes.size();
    while (ancestor && ancestor != r.value && left != 0) {
      ancestor = ancestor->parent;
      --left;
    }
    if (ancestor != r.value) return "orphan or parent cycle";
  }
  Walk w{r, {}};
  w.node(r.value);
  return w.why;
}

bool sound(std::string_view text) {
  const std::string got = verdict(text);
  if (got.empty()) return true;
  std::printf("  %s -> %s\n", std::string(text).c_str(), got.c_str());
  return false;
}

// A repair anywhere in the document arms the check that a nest's rule was not
// given up on, once per nest: two hundred thousand of them in one region cost
// one look each.
void long_postfix_chain_beside_a_fault() {
  constexpr unsigned count = 200000;
  std::string text = "x";
  for (unsigned i = 0; i < count; ++i) text += i % 2 == 0 ? ".a()" : ".b()";
  text += "; y = = ;";
  const auto p = tdoor::generated::G::create_parser();
  const auto r = p->parse("nest " + text);
  CHECK(r.accepted && r.error == cursiv::AstBuildError::none);
  CHECK(!r.diagnostics.empty() || !r.cst().diagnostics().empty());
  const auto* model = r.value_as<Model>();
  CHECK(model != nullptr && !model->stmts.empty());
  if (model == nullptr || model->stmts.empty()) return;
  const Expr* expr = model->stmts[0]->expr;
  unsigned depth = 0;
  while (const auto* call = cursiv::ast_cast<Call>(expr)) {
    const auto* member = cursiv::ast_cast<Member>(call->callee);
    if (member == nullptr) break;
    expr = member->receiver;
    ++depth;
  }
  CHECK(depth == count);
}

void long_postfix_chain() {
  constexpr unsigned count = 4096;
  std::string text = "x";
  for (unsigned i = 0; i < count; ++i) text += i % 2 == 0 ? ".a()" : ".b()";
  text += ';';
  const auto p = tdoor::generated::G::create_parser();
  const std::string doc = "nest " + text;
  const auto r = p->parse(doc);
  CHECK(r.accepted && r.error == cursiv::AstBuildError::none);
  CHECK(r.diagnostics.empty());
  const auto* model = r.value_as<Model>();
  CHECK(model != nullptr && model->stmts.size() == 1);
  if (model == nullptr || model->stmts.size() != 1) return;
  const cursiv::AstNode* owner = model->stmts[0];
  const Expr* expr = model->stmts[0]->expr;
  for (unsigned i = count; i-- > 0;) {
    const auto* call = cursiv::ast_cast<Call>(expr);
    CHECK(call != nullptr && call->parent == owner && call->args.empty());
    if (call == nullptr) return;
    const auto* member = cursiv::ast_cast<Member>(call->callee);
    CHECK(member != nullptr && member->parent == call);
    if (member == nullptr) return;
    CHECK(member->member == (i % 2 == 0 ? "a" : "b"));
    owner = member;
    expr = member->receiver;
  }
  const auto* name = cursiv::ast_cast<Name>(expr);
  CHECK(name != nullptr && name->name == "x" && name->parent == owner);
  CHECK(std::is_sorted(r.nodes.begin(), r.nodes.end(),
                       [](const auto* a, const auto* b) {
                         return a->cstNodeId < b->cstNodeId;
                       }));
}

}  // namespace

void run() {
  long_postfix_chain();
  long_postfix_chain_beside_a_fault();
  CHECK(sound("a+b(c).d;"));
  CHECK(sound("a+b(c).1;"));
  CHECK(sound("x(y(z(1)). 1;"));
  CHECK(sound("a.b(c}(d).1* (f + g);"));
  CHECK(sound("f(){ x = a * b(c)(d).1 + g; }"));
  CHECK(sound("a * b(c)(d).1 + g;"));
}

}  // namespace nest_recovered

// A data rule a repair completed by inserting one of its parts denotes no
// value: the member keeps its default, no conversion runs, and the inserted
// diagnostic is the only sentence.
namespace data_inserted {

namespace {

using namespace tdatains::ast;

// The data case's reading; its tree lives until the next call.
cursiv::ParseResult parse(std::string_view text) {
  static const auto p = tdoor::generated::G::create_parser();
  static std::string doc;
  doc = "data " + std::string(text);
  return p->parse(doc);
}

const Decl* first(const cursiv::ParseResult& r) {
  const auto* const m = r.value_as<Model>();
  return m == nullptr || m->decls.empty() ? nullptr : m->decls[0];
}

bool converted_nothing(const cursiv::ParseResult& r) {
  for (const auto& d : r.diagnostics)
    if (d.kind == cursiv::DiagnosticKind::conversion_error) return false;
  return true;
}

}  // namespace

void run() {
  {
    const auto r = parse("let x = -5 v 1.2 @ 12:345;");
    const Decl* const d = first(r);
    CHECK(r.accepted && r.diagnostics.empty() && d != nullptr);
    CHECK(d != nullptr && d->value == -5 && d->ver == 1.2 && d->pair == 203);
  }
  {
    const auto r = parse("let x = -;");
    const Decl* const d = first(r);
    CHECK(r.accepted && d != nullptr && converted_nothing(r));
    CHECK(d != nullptr && d->value == -99);
  }
  {
    const auto r = parse("let x = 5 v .2;");
    const Decl* const d = first(r);
    CHECK(r.accepted && d != nullptr && converted_nothing(r));
    CHECK(d != nullptr && d->value == 5 && d->ver == -1);
  }
  for (const std::string_view text : {"let x = 1 @ 12: ;", "let x = 1 @ :34;"}) {
    const auto r = parse(text);
    const Decl* const d = first(r);
    CHECK(r.accepted && d != nullptr && d->pair == -1);
  }
  CHECK(tdatains::fewestParts == 3);
}

}  // namespace data_inserted

// Two alternatives parting on their last token: both documents are valid.
namespace twin_arm {

namespace {
void row(const cursiv::Parser& p, const char* who, const std::string& body) {
  const std::string text = std::string(who) + " " + body;
  const cursiv::ParseResult r = p.parse(text);
  std::string d;
  for (const cursiv::ParseDiagnostic& x : r.cst().diagnostics()) {
    char b[64];
    std::snprintf(b, sizeof b, " kind=%d[%u,%u)", static_cast<int>(x.kind),
                  x.begin, x.end);
    d += b;
  }
  std::printf("%s %-12s accepted=%d value=%d diagnostics=%zu%s\n", who,
              body.c_str(), static_cast<int>(r.accepted),
              static_cast<int>(r.value != nullptr),
              r.cst().diagnostics().size(), d.c_str());
  CHECK(r.accepted && r.cst().diagnostics().empty());
}
}  // namespace

void run() {
  const std::unique_ptr<cursiv::Parser> p = tdoor::generated::G::create_parser();
  row(*p, "plain", "k a end");
  row(*p, "plain", "k a done");
  for (const char* d : {"k { } end", "k { } done", "k { a } end",
                        "k { a } done", "k { a ; b } end", "k { a ; b } done"})
    row(*p, "rep", d);
  for (const char* d : {"k { } end", "k { } done", "k { a } end",
                        "k { a } done", "k { a ; b } end", "k { a ; b } done"})
    row(*p, "sh", d);
}

}  // namespace twin_arm

void run() {
  maximal_munch::run();
  high_byte::run();
  infix_op::run();
  nest_after_infix::run();
  nest_recovered::run();
  data_inserted::run();
  twin_arm::run();
  passes::run();
  soft_keyword::run();
  rank_context::run();
  settle_phase::run();
}

}  // namespace lr_passes_cases
