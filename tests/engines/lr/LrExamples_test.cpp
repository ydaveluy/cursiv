// The shipped examples under repair and at the caret.

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "arithmetics/generated/Arith/Module.hpp"
#include "arithmetics/generated/Arith/Table.hpp"
#include "asicases/generated/G/Module.hpp"
#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/test/Check.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"
#include "domainmodel/generated/DomainModel/Table.hpp"
#include "requirements/generated/Requirements/Module.hpp"
#include "requirements/generated/Requirements/Table.hpp"
#include "statemachine/generated/SM/Module.hpp"
#include "statemachine/generated/SM/Table.hpp"
#include "unsettled/generated/G/Module.hpp"

// What a repair did, applied. A reading that repaired a document laid its
// edits on the tape -- a note names a fault, not each token of it; making
// them for real must yield a document the parser reads clean. Only what an
// editor could apply is made here: text dropped, and a terminal whose bytes
// the grammar spells, invented or read in place of the author's.
namespace repair_closure {

namespace {

struct Tally {
  unsigned docs = 0, applicable = 0, closed = 0, opaque = 0, unspelled = 0,
           rounds = 0, deepest = 0;
};

// A reading names the innermost repair first, and the one above it only once
// that is made -- so the rounds are the walk out, and this is what bounds it.
constexpr unsigned kRounds = 8;

std::string read(const std::string& path) {
  std::string out;
  if (FILE* f = std::fopen(path.c_str(), "rb")) {
    char buf[4096];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) != 0;)
      out.append(buf, n);
    std::fclose(f);
  }
  return out;
}

// The files a corpus names, the valid twin apart: the manifest is the list.
std::vector<std::string> manifest(const std::string& dir) {
  std::vector<std::string> names;
  const std::string all = read(dir + "/MANIFEST.tsv");
  for (std::size_t at = 0, line = 0; at < all.size(); ++line) {
    const std::size_t nl = std::min(all.find('\n', at), all.size());
    const std::size_t tab = all.find('\t', at);
    if (line != 0 && tab != std::string::npos && tab < nl) {
      const std::string name = all.substr(at, tab - at);
      if (name.rfind("valid", 0) != 0) names.push_back(name);
    }
    at = nl + 1;
  }
  return names;
}

// An edit per repaired leaf, made from the last so no offset moves under
// another. A repair whose bytes the grammar cannot spell is one no editor
// could make either, and so is a construct given up: the document is set
// aside rather than judged. A trivia re-read in place of one the lexer
// refused lays no repaired leaf, and its closer has no spelling either.
template <const auto& A>
bool apply(const cursiv::ParseResult& r, std::string& text, bool& spelled) {
  struct Edit {
    cursiv::TextOffset begin, end;
    std::string with;
  };
  const cursiv::Cst& cst = r.cst();
  for (const cursiv::ParseDiagnostic& d : r.diagnostics) {
    bool laid = d.kind != cursiv::DiagnosticKind::replaced;
    for (cursiv::NodeId i = 0; i < cst.size() && !laid; ++i)
      laid = cst[i].recovered && d.begin <= cst[i].begin && cst[i].end <= d.end;
    if (!laid) {
      spelled = false;
      return false;
    }
  }
  std::vector<Edit> edits;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i) {
    const cursiv::CstNode& n = cst[i];
    if (!n.recovered) continue;
    if (n.subtreeSize != 1) return false;
    const auto state = A.stamp.state(n);
    if (state == 0) {
      edits.push_back({n.begin, n.end, {}});
      continue;
    }
    const std::string_view bytes =
        cst.grammar()->terminalText(A.derived.expect.shiftTerm[state]);
    if (bytes.empty()) {
      spelled = false;
      return false;
    }
    edits.push_back({n.begin, n.end, std::string(" ") + std::string(bytes)});
  }
  for (std::size_t k = edits.size(); k-- > 0;)
    text.replace(edits[k].begin, edits[k].end - edits[k].begin, edits[k].with);
  return true;
}

template <const auto& A>
void language(const cursiv::Parser& engine, const char* name,
              const std::string& dir, Tally& tally) {
  const std::vector<std::string> names = manifest(dir);
  if (names.empty()) {
    std::printf("FAIL %s: no manifest at %s\n", name, dir.c_str());
    ++failures;
    return;
  }
  unsigned closed = 0, applicable = 0;
  for (const std::string& file : names) {
    const std::string text = read(dir + "/" + file);
    if (text.empty()) continue;
    ++tally.docs;
    const cursiv::ParseResult r = engine.parse(text);
    if (r.diagnostics.empty()) continue;
    std::string fixed = text;
    bool spelled = true;
    if (!apply<A>(r, fixed, spelled)) {
      ++tally.opaque;
      tally.unspelled += spelled ? 0u : 1u;
      continue;
    }
    ++tally.applicable;
    ++applicable;
    // Round after round: each one makes what the reading last named, and the
    // next names what stood above it. A document that stops shrinking is one
    // whose repairs do not describe what the reading did.
    unsigned round = 1;
    cursiv::ParseResult again = engine.parse(fixed);
    while (!again.diagnostics.empty() && round < kRounds) {
      std::string next = fixed;
      if (!apply<A>(again, next, spelled)) break;
      ++round;
      fixed = std::move(next);
      again = engine.parse(fixed);
    }
    tally.rounds += round;
    if (round > tally.deepest) tally.deepest = round;
    if (again.accepted && again.diagnostics.empty()) {
      ++tally.closed;
      ++closed;
      continue;
    }
    std::printf("FAIL %s/%s: %u rounds of repair, %zu still owed\n", name,
                file.c_str(), round, again.diagnostics.size());
    ++failures;
  }
  std::printf("  %-14s %u applicable, %u closed\n", name, applicable, closed);
}

}  // namespace

int run() {
  Tally t;
  const std::string dir = CURSIV_EXAMPLE_DIR;
#define LANG(FACTORY, NAME, SUB) \
  language<FACTORY::kTable>(*FACTORY::create_parser(), NAME, dir + SUB, t)
  LANG(arith::generated::Arith, "arithmetics", "/arithmetics/tests/recovery");
  LANG(dm::generated::DomainModel, "domainmodel",
       "/domainmodel/tests/recovery");
  LANG(req::generated::Requirements, "requirements",
       "/requirements/tests/recovery");
  LANG(sm::generated::SM, "statemachine", "/statemachine/tests/recovery");
#undef LANG
  std::printf(
      "  %u documents: %u applicable, %u closed in %u rounds (deepest %u), %u "
      "out of reach (%u a repair the grammar cannot spell)\n",
      t.docs, t.applicable, t.closed, t.rounds, t.deepest, t.opaque,
      t.unspelled);
  return 0;
}

}  // namespace repair_closure

// A caret past the last token of a document. What stands closed there names
// what may be opened next; the token just read names what may still carry
// ON, and both are owed -- answering from the close alone drops every
// continuation of the statement being written.
namespace caret_end {

namespace {

// Whether the caret at the end of `text` is offered a token spelled `want`.
bool offers(const cursiv::Parser& engine, const std::string& text,
            std::string_view want) {
  const cursiv::ParseResult r = engine.parse(text);
  std::vector<cursiv::NextFeature> out;
  const auto at = static_cast<cursiv::TextOffset>(text.size());
  engine.nextFeaturesAt(r.cst(), at, at, out);
  const cursiv::Grammar& g = engine.getGrammar();
  for (const cursiv::NextFeature& f : out)
    if (g.terminalText(f.feature) == want) return true;
  return false;
}

// Whether the caret at the end of `text` is offered a terminal named `name`.
bool offers_named(const cursiv::Parser& engine, const std::string& text,
                  std::string_view name) {
  const cursiv::ParseResult r = engine.parse(text);
  std::vector<cursiv::NextFeature> out;
  const auto at = static_cast<cursiv::TextOffset>(text.size());
  engine.nextFeaturesAt(r.cst(), at, at, out);
  for (const cursiv::NextFeature& f : out)
    if (engine.getGrammar().terminalName(f.feature) == name) return true;
  return false;
}

void a_statement_may_still_be_carried_on() {
  const auto engine = req::generated::Requirements::create_parser();
  // A requirement whose text is written and nothing else: what stands open is
  // a new `req`, and what the last token can carry on with is `applicable`.
  const std::string one = "req R1 \"t\" ";
  CHECK(engine->parse(one).accepted);
  CHECK(offers(*engine, one, "req"));
  CHECK(offers(*engine, one, "applicable"));

  // Past `applicable`, only `for` continues the written prefix. Starting
  // another requirement would first require abandoning the unfinished clause.
  const std::string two = "contact: \"m\"\nreq R1 \"t\" applicable ";
  CHECK(engine->parse(two + "for env").diagnostics.empty());
  CHECK(!engine->parse(two + "req R2 \"next\"").diagnostics.empty());
  CHECK(!offers(*engine, two, "req"));
  CHECK(offers(*engine, two, "for"));
}

void a_closed_block_offers_only_its_enclosing_scope() {
  const auto engine = dm::generated::DomainModel::create_parser();
  for (const std::string text :
       {"package Demo\n{\n}\n", "package Demo {}",
        "package Demo { package Inner {} }\n", "package Demo { entity S {} }\n",
        "entity S { x: T }\n", "package First {} package Second {}\n"}) {
    const auto r = engine->parse(text);
    CHECK(r.accepted && !r.cst().recovered());
    std::vector<cursiv::NextFeature> out;
    const auto at = static_cast<cursiv::TextOffset>(text.size());
    engine->nextFeaturesAt(r.cst(), at, at, out);
    CHECK(offers(*engine, text, "package"));
    CHECK(offers(*engine, text, "entity"));
    CHECK(offers(*engine, text, "datatype"));
    for (const auto& f : out) {
      const auto spelling = engine->getGrammar().terminalText(f.feature);
      const bool opens = spelling == "package" || spelling == "entity" ||
                         spelling == "datatype";
      if (!opens)
        std::printf("unexpected completion '%.*s' after %s\n",
                    static_cast<int>(spelling.size()), spelling.data(),
                    text.c_str());
      CHECK(opens);
    }
  }
  // Inside the enclosing package, declarations and its own closer remain
  // valid even after a nested package has just ended.
  const std::string inside = "package Demo { package Inner {} ";
  CHECK(offers(*engine, inside, "package"));
  CHECK(offers(*engine, inside, "entity"));
  CHECK(offers(*engine, inside, "}"));
}

// A word the parser read after a token it invented still swallows what is
// typed against it: `many` glued to `A` is the one word `Amany`.
void a_word_after_a_repair_swallows_what_is_glued_to_it() {
  const auto engine = dm::generated::DomainModel::create_parser();
  for (const std::string text : {"entity E { x : A", "entity E { x A"}) {
    CHECK(!offers(*engine, text, "many"));
    CHECK(offers(*engine, text + " ", "many"));
  }
}

// A line comment that ends at the caret takes a name as it takes a keyword.
void a_comment_takes_the_name_typed_against_its_end() {
  const auto engine = dm::generated::DomainModel::create_parser();
  CHECK(offers_named(*engine, "entity E { x : ", "ID"));
  CHECK(!offers_named(*engine, "entity E { x : // c", "ID"));
  CHECK(offers_named(*engine, "entity E { x : /* c */", "ID"));
}

}  // namespace

int run() {
  a_statement_may_still_be_carried_on();
  a_closed_block_offers_only_its_enclosing_scope();
  a_word_after_a_repair_swallows_what_is_glued_to_it();
  a_comment_takes_the_name_typed_against_its_end();
  return 0;
}

}  // namespace caret_end

// A caret behind the repairs a search wrote answers as the text they spell
// would: each repaired leaf is replayed through the table's own actions on
// its terminal, the reductions it needs included.
namespace caret_replay {

namespace {

std::vector<cursiv::ElementId> offered(const cursiv::Parser& parser,
                                       const std::string& text,
                                       cursiv::TextOffset at) {
  const cursiv::ParseResult r = parser.parse(text);
  std::vector<cursiv::NextFeature> out;
  parser.nextFeaturesAt(r.cst(), at, at, out);
  std::vector<cursiv::ElementId> ids;
  for (const cursiv::NextFeature& f : out) ids.push_back(f.feature);
  std::ranges::sort(ids);
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}

// The caret before `mark` in `broken`, and before it in `spelled`: the one
// answer, and not an empty one.
void answers_as_spelled(const cursiv::Parser& parser, const std::string& broken,
                        const std::string& spelled, std::string_view mark) {
  const auto a = offered(parser, broken,
                         static_cast<cursiv::TextOffset>(broken.rfind(mark)));
  const auto b = offered(parser, spelled,
                         static_cast<cursiv::TextOffset>(spelled.rfind(mark)));
  if (a.empty() || a != b) {
    std::printf("FAIL \"%s\": %zu offered, %zu where spelled \"%s\"\n",
                broken.c_str(), a.size(), b.size(), spelled.c_str());
    ++failures;
  }
}

// Invented tokens -- an operand, and the `:` before it -- shifted after the
// reductions the table makes on them, which no row of the state names.
void an_insertion_replays_through_the_reductions_it_needs() {
  const auto parser = arith::generated::Arith::create_parser();
  const cursiv::ParseResult r = parser->parse("Module m\n\ndef a; 5;\n");
  CHECK(r.accepted && r.diagnostics.size() == 1);
  answers_as_spelled(*parser, "Module m\n\ndef a; 5;\n",
                     "Module m\n\ndef a: 0; 5;\n", "5;");
  answers_as_spelled(*parser, "Module m\n\na +;\n2;\n",
                     "Module m\n\na + 0;\n2;\n", "2;");
  answers_as_spelled(*parser, "Module m\n\ndef a: [;\ndef b: 3;\n",
                     "Module m\n\ndef a: 0;\ndef b: 3;\n", "def b");
}

// An operand given up down to the start state: the unwind names that state,
// and the caret stands where the parse resumed.
void an_abandon_to_the_start_state_replays() {
  const auto parser = tasi::cases::generated::G::create_parser();
  const std::string text = "tailops a +b;";
  const cursiv::ParseResult r = parser->parse(text);
  CHECK(r.accepted && r.diagnostics.size() == 1 &&
        r.diagnostics[0].kind == cursiv::DiagnosticKind::incomplete);
  const auto at_end = offered(*parser, text + " ", text.size() + 1);
  CHECK(at_end == offered(*parser, "tailops b; ", 11));
  CHECK(!at_end.empty());
}

// A record whose rule the drive read off text past the caret is walked: the
// other rule's continuation stands too.
void a_record_the_text_has_not_decided_is_walked() {
  const auto parser = tunsettled::generated::G::create_parser();
  bool o0 = false;
  for (const cursiv::ElementId e : offered(*parser, "p x a a o1", 8))
    o0 = o0 || parser->getGrammar().terminalText(e) == "o0";
  CHECK(o0);
}

}  // namespace

int run() {
  an_insertion_replays_through_the_reductions_it_needs();
  an_abandon_to_the_start_state_replays();
  a_record_the_text_has_not_decided_is_walked();
  return 0;
}

}  // namespace caret_replay

int main() {
  int status = 0;
  status |= repair_closure::run();
  status |= caret_end::run();
  status |= caret_replay::run();
  return cursiv::test::report("LrExamples_test") | status;
}
