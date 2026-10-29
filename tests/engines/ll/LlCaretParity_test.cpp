// The table engine's completion is the reference: at every caret of a valid
// document, and at the end of every cut of one, the descent offers the same
// tokens, and names the operators and the postfix mark as the table engine
// does.

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "indirectfoldll/generated/Ll/Module.hpp"
#include "conditionsll/generated/Ll/Module.hpp"
#include "conditionslr/generated/Lr/Module.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/Check.hpp"
#include "parityll/generated/Ll/Module.hpp"
#include "paritylr/generated/Lr/Module.hpp"

namespace {

struct Entry {
  std::string spelling, property;
  bool operator<(const Entry& other) const {
    return std::tie(spelling, property) <
           std::tie(other.spelling, other.property);
  }
  bool operator==(const Entry&) const = default;
};

std::vector<Entry> features_between(const cursiv::Parser& parser,
                                    const std::string& text,
                                    cursiv::TextOffset at,
                                    cursiv::TextOffset resume,
                                    std::string_view prefix = {}) {
  const std::string input = std::string(prefix) + text;
  const cursiv::ParseResult parsed = parser.parse(input);
  if (!prefix.empty())
    CHECK(parsed.accepted && parsed.diagnostics.empty() && parsed.value);
  std::vector<cursiv::NextFeature> next;
  const auto offset = static_cast<cursiv::TextOffset>(prefix.size());
  parser.nextFeaturesAt(parsed.cst(), at + offset, resume + offset, next);
  std::vector<Entry> out;
  for (const cursiv::NextFeature& f : next) {
    const auto spellings = parser.getGrammar().terminalSpellings(f.feature);
    out.push_back(
        {std::string(spellings.size() == 1
                         ? spellings.front()
                         : parser.getGrammar().terminalName(f.feature)),
         std::string(f.property)});
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<Entry> features_at(const cursiv::Parser& parser,
                               const std::string& text, cursiv::TextOffset at) {
  return features_between(parser, text, at, at);
}

std::string show(const std::vector<Entry>& entries) {
  std::string out;
  for (const Entry& e : entries)
    out += ' ' + e.spelling + (e.property.empty() ? "" : ':' + e.property);
  return out;
}

std::vector<std::string> spellings(const std::vector<Entry>& entries) {
  std::vector<std::string> out;
  for (const Entry& e : entries) out.push_back(e.spelling);
  return out;
}

// What a mark's name is the table engine's to state: the operators of the
// precedence table and the postfix mark.
std::vector<Entry> marks(const std::vector<Entry>& entries) {
  std::vector<Entry> out;
  for (const Entry& e : entries)
    if (std::string_view("*/+-!").find(e.spelling) != std::string_view::npos &&
        e.spelling.size() == 1)
      out.push_back(e);
  return out;
}

struct Sweep {
  std::size_t asked = 0, alike = 0, named = 0;
};

void compare(const cursiv::Parser& lr, const cursiv::Parser& ll,
             const std::string& text, cursiv::TextOffset at, Sweep& sweep) {
  const auto a = features_at(lr, text, at);
  const auto b = features_at(ll, text, at);
  ++sweep.asked;
  const bool tokens = spellings(a) == spellings(b);
  const bool names = marks(a) == marks(b);
  sweep.alike += tokens;
  sweep.named += names;
  if (!tokens || !names)
    std::fprintf(stderr, "caret parity [%s] at %u\n  LR:%s\n  LL:%s\n",
                 text.c_str(), static_cast<unsigned>(at), show(a).c_str(),
                 show(b).c_str());
}

// A document with a repair in it is swept at its carets alone: a cut of it is
// another document, and the repairs the two engines make for it are their own.
void sweep_language(std::string_view name, const cursiv::Parser& lr,
                    const cursiv::Parser& ll,
                    const std::vector<std::string_view>& documents,
                    bool cut_too = true) {
  Sweep carets, cuts;
  for (const std::string_view document : documents) {
    for (std::size_t at = 0; at <= document.size(); ++at)
      compare(lr, ll, std::string(document),
              static_cast<cursiv::TextOffset>(at), carets);
    for (std::size_t cut = 0; cut_too && cut <= document.size(); ++cut)
      compare(lr, ll, std::string(document.substr(0, cut)),
              static_cast<cursiv::TextOffset>(cut), cuts);
  }
  std::fprintf(stderr,
               "%.*s: carets %zu, same tokens %zu, same marks %zu; cuts %zu, "
               "same tokens %zu, same marks %zu\n",
               static_cast<int>(name.size()), name.data(), carets.asked,
               carets.alike, carets.named, cuts.asked, cuts.alike, cuts.named);
  CHECK(carets.alike == carets.asked && carets.named == carets.asked);
  CHECK(cuts.alike == cuts.asked && cuts.named == cuts.asked);
}

void an_expression_with_parentheses_calls_and_a_postfix_mark() {
  const auto lr = tpar::generated::Lr::create_parser();
  const auto ll = tpar::generated::Ll::create_parser();
  sweep_language(
      "tpar", *lr, *ll,
      {"let a = 1 + 2 * 3;", "export let a = f(1, 2) - (3 + b);",
       "let a = ~(a) + (b * (c - 1));", "let a = (f(1))!;",
       "let a = f(g(h(1)), (2));", "let a = 1; // tail\nlet b = a * a;",
       "let a = 1 /* x */ + 2 /**/ * 3;", "show all x, y;",
       "show (1 + 2) * 3 as v, ~(b);", "show a, (b), f((c));"});
}

bool offers(const std::vector<Entry>& entries, std::string_view spelling,
            std::string_view property = {}) {
  return std::any_of(entries.begin(), entries.end(), [&](const Entry& e) {
    return e.spelling == spelling && e.property == property;
  });
}

void a_closer_needs_its_opener_and_a_mark_its_member() {
  const auto ll = tpar::generated::Ll::create_parser();
  const std::string closed = "let a = 1 + 2 ";
  const auto after = features_at(*ll, closed, closed.size());
  CHECK(offers(after, ";") && offers(after, "+", "op") && offers(after, "!"));
  CHECK(!offers(after, ")") && !offers(after, ",") && !offers(after, "as"));
  const std::string open = "let a = (1 + 2";
  const auto inside = features_at(*ll, open, open.size());
  CHECK(offers(inside, ")") && !offers(inside, ",") && !offers(inside, ";"));
  const std::string call = "let a = f(1, g(2";
  const auto args = features_at(*ll, call, call.size());
  CHECK(offers(args, ")") && offers(args, ",") && offers(args, "*", "op"));
  CHECK(!offers(args, ";"));
}

void statements_ended_by_a_gap_or_a_semicolon() {
  const auto lr = tllconditions::generated::Lr::create_parser();
  const auto ll = tllconditions::generated::Ll::create_parser();
  sweep_language(
      "conditions", *lr, *ll,
      {"1 + 2;\nreturn x;\n", "{ return y\n a.b.c\n}", "return\nx;\n",
       "{ 1;\n{ 2 + 3 } }", "a.b;\n1 + 2 + 3\nreturn"});
}

// The table engine steps over a finished record when no condition asked in it
// reads the text a candidate replaces. The word's condition reads the two
// tokens behind it: `y` standing for `bb` would complete `x y` and refuse it.

// The condition of `a;` is asked of `x` and reads two tokens on: a caret on the
// third of `x y z`, or one that ends the document, replaces text it still
// reads, wherever the replay starts.

// A comment in `ignored` leaves no leaf: the caret has to read the blanks and
// the comments before it again to know that what is typed there goes into the
// comment, and a word the comment absorbs is not the keyword it spells.

// The word under the caret is a guarded terminal for one arm and a name for the
// other, and both go on from it: `do:` is as sound as `do?`, so the caret that
// replaces the `?` is offered the `:`.
void a_word_read_as_two_terminals_goes_on_as_either() {
  const auto ll = tllexpressions::generated::Ll::create_parser();
  for (const cursiv::Parser* parser : {ll.get()}) {
    const auto offered =
        spellings(features_between(*parser, "do?", 2, 3, "reading:"));
    CHECK((offered == std::vector<std::string>{":", "?"}));
  }
}

// A literal takes its spelling from the name at the same point: behind `do` the
// caret is offered what the literal goes on with, and not the `:` of the name.
void a_literal_takes_its_spelling_from_the_name() {
  const auto ll = tllexpressions::generated::Ll::create_parser();
  for (const cursiv::Parser* parser : {ll.get()}) {
    const auto offered =
        spellings(features_between(*parser, "do?", 2, 3, "spelled:"));
    CHECK((offered == std::vector<std::string>{"?"}));
  }
}

// A token goes on from the reading of its own arm alone: the guard refuses
// `do!`, so the `!` only the guarded terminal goes on with is not offered
// behind the name, with a condition in the grammar or without.

// Behind an operator guarded against a second `<` the lexer elects the bare
// twin, yet the operand goes on from the operator: the `<` that opens an
// operand, typed there, would make the operator the twin and is not offered.
void the_twin_of_the_operator_is_not_offered_behind_it() {
  const auto ll = tllexpressions::generated::Ll::create_parser();
  for (const cursiv::Parser* parser : {ll.get()}) {
    for (const auto& [at, resume] : {std::pair{2u, 2u}, std::pair{2u, 3u}}) {
      const auto offered =
          features_between(*parser, "a<b;", at, resume, "climb:");
      CHECK((spellings(offered) == std::vector<std::string>{"!", "ID"}));
    }
  }
}

// The choice between two rules that read `%` is made on the token behind it,
// and the choice reads `->` where the position of the `-` does not: a `>`
// glued to the `-` makes it `->`, so it follows only after a blank.
void a_token_the_choice_read_glues_as_the_choice_reads_it() {
  const auto ll = tllexpressions::generated::Ll::create_parser();
  for (const std::string gap : {"", " "}) {
    const std::string text = "glue:%-" + gap;
    const auto at = static_cast<cursiv::TextOffset>(text.size());
    const auto offered = spellings(features_between(*ll, text, at, at));
    CHECK(offered == (gap.empty() ? std::vector<std::string>{}
                                  : std::vector<std::string>{">"}));
    const auto typed = ll->parse(text + ">");
    CHECK((typed.accepted && typed.diagnostics.empty()) == !gap.empty());
  }
}

// A condition reads a run of `~` as one token, however many the arms read it
// as, and reads on to what follows the run. Past a caret on its first token it
// reads the document's text; before a caret in the run it reads what is typed
// there, and a `?` makes it refuse the `|` it stands before.
void a_condition_reads_the_tokens_behind_the_caret_its_own_way() {
  const auto ll = tllexpressions::generated::Ll::create_parser();
  const auto clean = [&](const std::string& text) {
    const auto parsed = ll->parse(text);
    return parsed.accepted && parsed.diagnostics.empty();
  };
  const auto offers = [](const std::vector<Entry>& entries,
                         std::string_view word) {
    return std::ranges::any_of(
        entries, [&](const Entry& e) { return e.spelling == word; });
  };
  const std::string run(40, '~');
  const std::string held = "cond:|" + run + "!";
  CHECK(clean(held) && offers(features_between(*ll, held, 5, 6), "|"));
  const std::string refused = "not:|" + run + "?";
  CHECK(!clean(refused) && !offers(features_between(*ll, refused, 4, 5), "|"));
  const std::string cut = "not:|" + run.substr(20);
  const auto at = static_cast<cursiv::TextOffset>(cut.size());
  for (const std::string& text : {cut + run.substr(20) + "!", cut}) {
    const auto words = features_between(
        *ll, text, at, std::min<cursiv::TextOffset>(at + 1, text.size()));
    CHECK(offers(words, "!") && !offers(words, "?"));
  }
  CHECK(clean(cut + "!") && !clean(cut + "?"));
}

// The document fails the condition of a word and a candidate would satisfy it:
// `a;` is refused while `x y` stands behind it, and what is typed in front of
// them lets it stand. Every caret and every span a candidate may replace.

// Behind a repair the replay is still the descent's: a run of invented tokens,
// a misspelt word and dropped text each leave the stack the table engine
// names.
void carets_behind_a_repair_are_the_table_engines() {

  sweep_language("tpar repaired", *tpar::generated::Lr::create_parser(),
                 *tpar::generated::Ll::create_parser(),
                 {"sow a;", "shw all x, y;"}, false);
  sweep_language("conditions repaired",
                 *tllconditions::generated::Lr::create_parser(),
                 *tllconditions::generated::Ll::create_parser(),
                 {"a.(b;\n1 + 2 + 3\nreturn", "{ return y\n a.}b.c\n}"}, false);
}

// A word a repair read as the keyword it misspells is the author's: a word
// typed right behind it makes one word with it and is not offered there, a
// `(` is.
void a_misspelt_word_glues_what_is_typed_behind_it() {
  const auto lr = tpar::generated::Lr::create_parser();
  const auto ll = tpar::generated::Ll::create_parser();
  const std::string text = "shw all x, y;";
  for (const cursiv::Parser* parser : {lr.get(), ll.get()}) {
    const auto behind = features_at(*parser, text, 3);
    CHECK(offers(behind, "(") && !offers(behind, "all", "all"));
    CHECK(offers(features_between(*parser, text, 4, 7), "all", "all"));
  }
}

}  // namespace

int main() {
  an_expression_with_parentheses_calls_and_a_postfix_mark();
  a_closer_needs_its_opener_and_a_mark_its_member();
  statements_ended_by_a_gap_or_a_semicolon();
  a_word_read_as_two_terminals_goes_on_as_either();
  a_literal_takes_its_spelling_from_the_name();
  the_twin_of_the_operator_is_not_offered_behind_it();
  a_token_the_choice_read_glues_as_the_choice_reads_it();
  a_condition_reads_the_tokens_behind_the_caret_its_own_way();
  carets_behind_a_repair_are_the_table_engines();
  a_misspelt_word_glues_what_is_typed_behind_it();
  return cursiv::test::report("LlCaretParity_test");
}
