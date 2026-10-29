// Embedded grammars keep their trivia, word boundaries, keywords and trees.
// Fixed expectations cover their repairs, delimiters and completion in both
// engines.

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

#include "cursiv/test/Check.hpp"
#include "embedll/generated/DocLl/GrammarSource.hpp"
#include "embedll/generated/DocLl/Module.hpp"
#include "embedlr/generated/DocLr/GrammarSource.hpp"
#include "embedlr/generated/DocLr/Module.hpp"
#include "engines/lr/EmbedGrammar.hpp"
#include "engines/lr/ParseChecks.hpp"

namespace {

using cursiv::Parser;
using cursiv::ParseResult;
using cursiv::TextOffset;
using checks::clean, checks::leaves, checks::offered;

// Read with a repair, the first one in [at, upto]: where the text breaks the
// law the case is about.
bool repaired(const Parser& p, std::string_view text, TextOffset at,
              TextOffset upto = 0) {
  const ParseResult r = p.parse(text);
  const TextOffset first =
      r.diagnostics.empty() ? ~TextOffset{0} : r.diagnostics.front().begin;
  const bool ok = r.accepted && first >= at && first <= std::max(at, upto);
  if (!ok)
    std::printf("  not repaired at %u (first %d): \"%.*s\"\n", at,
                r.diagnostics.empty() ? -1
                                      : static_cast<int>(first),
                static_cast<int>(text.size()), text.data());
  return ok;
}

// Some repair of `r` owes the token `text` spells.
bool owes(const Parser& p, const ParseResult& r, std::string_view text) {
  for (const cursiv::ParseDiagnostic& d : r.diagnostics)
    if (d.element != cursiv::kNoElement)
      for (const std::string_view s :
           p.getGrammar().terminalSpellings(d.element))
        if (s == text) return true;
  return false;
}

template <class T>
const T* as(const cursiv::AstNode* n) {
  return n->type == &cursiv::ast_type_tag<T> ? static_cast<const T*>(n)
                                             : nullptr;
}

// The tree built, by class and member.
std::string ast(const cursiv::AstNode* n) {
  using namespace tembed::ast;
  if (n == nullptr) return "-";
  if (const auto* x = as<Num>(n)) return std::to_string(x->n);
  if (const auto* x = as<Str>(n)) return std::string(x->text);
  if (const auto* x = as<Flag>(n)) return x->on ? "on" : "off";
  if (const auto* x = as<Pair>(n))
    return std::string(x->key) + ':' + ast(x->value);
  if (const auto* x = as<Obj>(n)) {
    std::string out = "{";
    for (const Pair* e : x->pairs) out += ast(e) + ',';
    return out + '}';
  }
  if (const auto* x = as<List>(n)) {
    std::string out = "[";
    for (const Value* e : x->items) out += ast(e) + ',';
    return out + ']';
  }
  if (const auto* x = as<Set>(n))
    return std::string(x->name) + '=' + std::to_string(x->value);
  if (const auto* x = as<Lines>(n)) {
    std::string out = "(";
    for (const Set* e : x->sets) out += ast(e) + ';';
    return out + ')';
  }
  return "?";
}

// The node the document's first block holds.
const cursiv::AstNode* first_block(const ParseResult& r, bool script) {
  const auto* doc = r.value_as<tembed::ast::Doc>();
  if (doc == nullptr || doc->blocks.empty()) return nullptr;
  const tembed::ast::Block* b = *doc->blocks.begin();
  if (b == nullptr) return nullptr;
  return script ? static_cast<const cursiv::AstNode*>(b->script) : b->json;
}

void a_section_reads_its_own_trivia(const Parser& p) {
  CHECK(clean(p, "json '''[1]'''\n"));
  CHECK(clean(p, "json ''' [1] '''\n"));
  CHECK(clean(p, "json '''\n  [1,\n 2] // end\n'''  # after\n"));
  CHECK(clean(p, "json '''[1] // embedded close ''' is still comment text\n'''\n"));
  CHECK(leaves(p, "json ''' [1] // c\n''' # d\n") ==
        "json ''' [ 1 ] [// c] ''' [# d] \n");
  CHECK(repaired(p, "json '''[1 # no]'''\n", 11));
  CHECK(repaired(p, "json '''[1]''' // no\n", 15));
  CHECK(repaired(p, "json '''[1, >]'''\n", 12));
  CHECK(repaired(p, "raw (<[1, ''']>)\n", 10));
}

// A section whose closing token never comes runs to the end of the document,
// where the repair owes that token: the section's tree stands.
void a_missing_close_is_owed_at_the_end(const Parser& p) {
  const ParseResult r = p.parse("json ''' [1]\n");
  CHECK(r.accepted && ast(first_block(r, false)) == "[1,]" &&
        !r.diagnostics.empty() &&
        r.diagnostics.front().kind == cursiv::DiagnosticKind::inserted &&
        r.diagnostics.front().begin == 12 && owes(p, r, "'''"));
  for (const std::string_view text :
       {"json '''[1]\ncount 0x2\n", "json '''[1]''\ncount 0x2\n"}) {
    const ParseResult x = p.parse(text);
    CHECK(x.accepted && ast(first_block(x, false)) == "[1,]" &&
          owes(p, x, "'''"));
  }
  const ParseResult y = p.parse("raw (<[1])\ncount 0x2\n");
  CHECK(y.accepted && ast(first_block(y, false)) == "[1,]" && owes(p, y, ">"));
}

// A grammar that skips nothing, where the document's region skips nothing
// either: its section lexes a fault with its own terminals, never with the
// document's comment, which would run past the closing brace.
void a_fault_is_read_with_the_sections_terminals(const Parser& p) {
  CHECK(clean(p, "tiny {a;};\n"));
  const ParseResult r = p.parse("tiny {a#};\ncount 0x2\n");
  const auto* doc = r.value_as<tembed::ast::Doc>();
  CHECK(r.accepted && doc != nullptr && doc->blocks.size() == 2 &&
        ast(doc->blocks.begin()[0]->json) == "on" &&
        doc->blocks.begin()[1]->count == 2 && !r.diagnostics.empty());
  for (const cursiv::ParseDiagnostic& d : r.diagnostics)
    CHECK(d.begin >= 7 && d.end <= 8);
}

void the_word_boundary_and_the_keywords_are_the_sections(const Parser& p) {
  CHECK(clean(p, "json '''[x10, True, FALSE]'''\n"));
  CHECK(ast(first_block(p.parse("json '''[x10, True, FALSE]'''\n"), false)) ==
        "[10,on,off,]");
  CHECK(repaired(p, "json '''[x10, xa]'''\n", 14));
  CHECK(repaired(p, "json '''[true_]'''\n", 9));
}

// The document restates Json's number for itself; the section keeps Json's.
void an_override_stays_outside(const Parser& p) {
  const ParseResult r = p.parse("count 0x1f\n");
  CHECK(r.accepted && r.diagnostics.empty());
  const auto* doc = r.value_as<tembed::ast::Doc>();
  CHECK(doc != nullptr && doc->blocks.size() == 1 &&
        (*doc->blocks.begin())->count == 31);
  CHECK(repaired(p, "count 31\n", 5, 6));
  CHECK(clean(p, "json '''31'''\n"));
  CHECK(repaired(p, "json '''0x1f'''\n", 9));
}

// A region the document opens around the section's delimiters reads them
// tight; the section's own edges are still Json's.
void the_document_cuts_its_blanks_around(const Parser& p) {
  CHECK(clean(p, "raw (<[1]>)\n"));
  CHECK(clean(p, "raw (< [1]\n>)\n"));
  CHECK(clean(p, "raw  (<[1]>)\n"));
  CHECK(repaired(p, "raw ( <[1]>)\n", 5));
  CHECK(repaired(p, "raw (<[1]> )\n", 10));
}

void the_tree_holds_the_section(const Parser& p) {
  const ParseResult r =
      p.parse("json '''{\"a\": [1, x2], \"b\": {}}'''\nraw (<true>)\n");
  CHECK(r.accepted && r.diagnostics.empty());
  CHECK(ast(first_block(r, false)) == "{\"a\":[1,2,],\"b\":{},}");
  const auto* doc = r.value_as<tembed::ast::Doc>();
  CHECK(doc != nullptr && doc->blocks.size() == 2 &&
        ast(doc->blocks.begin()[1]->json) == "on");
}

// A directive over a section writes the entry's value; one over a delimiter
// writes the delimiter.
void a_directive_stands_over_the_section_or_a_delimiter(const Parser& p) {
  const ParseResult r = p.parse("each (1) (true)\n");
  const auto* doc = r.value_as<tembed::ast::Doc>();
  CHECK(r.accepted && r.diagnostics.empty() && doc != nullptr &&
        doc->blocks.size() == 1);
  if (doc == nullptr || doc->blocks.size() != 1) return;
  const tembed::ast::Block* b = doc->blocks.begin()[0];
  CHECK(b->opener == "(" && b->list.size() == 2);
  if (b->list.size() == 2) {
    CHECK(ast(b->list.begin()[0]) == "1");
    CHECK(ast(b->list.begin()[1]) == "on");
  }
}

// The caret reads a section as its grammar does, and a repair inside one
// leaves the document around it read.
void the_caret_and_the_repair_read_the_section(const Parser& p) {
  const std::string inside = offered(p, "json '''[1");
  CHECK(inside.find(',') != std::string::npos &&
        inside.find(']') != std::string::npos);
  const std::string item = offered(p, "json '''[1, ");
  CHECK(item.find('[') != std::string::npos &&
        item.find('{') != std::string::npos &&
        item.find("true") != std::string::npos);
  CHECK(offered(p, "json '''[1]").find("'''") != std::string::npos);
  // The token that closes a section is the caller's: behind it the caret is
  // offered what the caller goes on with, and the blank in front of it is the
  // section's.
  CHECK(offered(p, "json '''[1]'''").find("'''") == std::string::npos);
  CHECK(offered(p, "narrow <get;>").find('>') == std::string::npos);
  CHECK(offered(p, "raw (<[1]>").find('>') == std::string::npos);
  CHECK(offered(p, "raw (<[1]>").find(')') != std::string::npos);
  CHECK(offered(p, "narrow < get a ; ").find('>') != std::string::npos);
  const ParseResult r = p.parse("json '''[1,,2]'''\ncount 0x2\n");
  CHECK(r.accepted && !r.diagnostics.empty());
  const auto* doc = r.value_as<tembed::ast::Doc>();
  CHECK(doc != nullptr && doc->blocks.size() == 2 &&
        doc->blocks.begin()[1]->count == 2);
}

// The dump names the section by its grammar, and every rule of that grammar
// by a name of its own.
void the_dump_qualifies_the_section(std::string_view dump) {
  CHECK(dump.find("json=Json '\\'\\'\\''") != std::string_view::npos);
  CHECK(dump.find("grammar Json skip Json::WS hidden Json::COMMENT: "
                  "Json::Text;") != std::string_view::npos);
  CHECK(dump.find("Json::Num returns Num: {Num} n=Json::NUMBER | {Num} "
                  "\"x\"\\b[A-Z_a-z] n=Json::NUMBER;") !=
        std::string_view::npos);
  CHECK(dump.find("terminal NUMBER returns int64: '0x' [0-9a-f]+;") !=
        std::string_view::npos);
}

// A keyword reserves its spelling by the word class of the section's grammar,
// and what runs into it is that class's: the host's `w` is another.
void a_section_keeps_its_own_word_class(const Parser& p) {
  CHECK(repaired(p, "narrow <get;>\n", 11));
  CHECK(repaired(p, "dash <get;>\n", 9));
  CHECK(repaired(p, "wide <get;>\n", 9));
  CHECK(clean(p, "dash <get-x;>\n"));
  CHECK(clean(p, "dash <let1;>\n"));
  CHECK(clean(p, "narrow <let1;>\n"));
  CHECK(clean(p, "narrow <let-x;>\n"));
  CHECK(repaired(p, "wide <let\xC3\xA9;>\n", 9));
  CHECK(clean(p, "letters <letx;>\n"));
  CHECK(clean(p, "letters <let\xC3\xA9;>\n"));
  CHECK(repaired(p, "letters <let;>\n", 9));
  for (const char* text :
       {"narrow <rem x;>\n", "dash <rem x;>\n", "wide <rem x;>\n"}) {
    const ParseResult r = p.parse(text);
    CHECK(r.accepted && r.diagnostics.empty() &&
          ast(first_block(r, true)) == "(x=0;)");
  }
  const ParseResult r = p.parse("dash <get-x;>\n");
  CHECK(ast(first_block(r, true)) == "(get-x=0;)");
}

void each(const char* name, const Parser& p, std::string_view dump) {
  std::printf("%s\n", name);
  a_section_reads_its_own_trivia(p);
  the_word_boundary_and_the_keywords_are_the_sections(p);
  an_override_stays_outside(p);
  the_document_cuts_its_blanks_around(p);
  the_tree_holds_the_section(p);
  a_directive_stands_over_the_section_or_a_delimiter(p);
  the_caret_and_the_repair_read_the_section(p);
  a_missing_close_is_owed_at_the_end(p);
  a_fault_is_read_with_the_sections_terminals(p);
  the_dump_qualifies_the_section(dump);
  a_section_keeps_its_own_word_class(p);

}

// The end of a section is the end of its grammar's text: a `;` implied there
// as at the end of the text alone.
void the_end_of_a_section_is_the_end_of_the_text(const Parser& p) {
  std::printf("LR script\n");
  CHECK(clean(p, "script <%a=1%>\n"));
  CHECK(clean(p, "script <% a = 1\nb = 2 %>\n"));
  CHECK(clean(p, "script <%\n a = 1\n%>\n"));
  CHECK(ast(first_block(p.parse("script <% a = 1\nb = 2 %>\n"), true)) ==
        "(a=1;b=2;)");
  CHECK(repaired(p, "script <% a = 1 b = 2 %>\n", 15));
  const ParseResult r = p.parse("script <% a = 1\n");
  CHECK(r.accepted && ast(first_block(r, true)) == "(a=1;)" &&
        owes(p, r, "%>"));
}

}  // namespace

int main() {
  const auto lr = tembed::generated::DocLr::create_parser();
  const auto ll = tembed::generated::DocLl::create_parser();
  each("LR", *lr, tembed::generated::DocLr::kGrammarText);
  each("LL", *ll, tembed::generated::DocLl::kGrammarText);
  the_end_of_a_section_is_the_end_of_the_text(*lr);
  return cursiv::test::report("Embed_test");
}
