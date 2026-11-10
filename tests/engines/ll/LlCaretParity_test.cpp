// The table engine's completion is the reference: at every caret of a valid
// document, and at the end of every cut of one, the descent offers the same
// tokens and names operators and the postfix mark the same.

#include <algorithm>
#include <chrono>
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
#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/Check.hpp"
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

// The tokens a caret offers, each once: one token has an entry per member, and
// the two engines name different members for an operand.
std::vector<std::string> spellings(const std::vector<Entry>& entries) {
  std::vector<std::string> out;
  for (const Entry& e : entries) out.push_back(e.spelling);
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

// The operators of the precedence table and the postfix mark: the table
// engine's names for them are the reference.
std::vector<Entry> marks(const std::vector<Entry>& entries) {
  std::vector<Entry> out;
  for (const Entry& e : entries)
    if (std::string_view("*/+-!").find(e.spelling) != std::string_view::npos &&
        e.spelling.size() == 1)
      out.push_back(e);
  return out;
}

struct Sweep {
  std::size_t asked = 0, sameTokens = 0, named = 0;
};

void compare(const cursiv::Parser& lr, const cursiv::Parser& ll,
             const std::string& text, cursiv::TextOffset at, Sweep& sweep) {
  const auto a = features_at(lr, text, at);
  const auto b = features_at(ll, text, at);
  ++sweep.asked;
  const bool tokens = spellings(a) == spellings(b);
  const bool names = marks(a) == marks(b);
  sweep.sameTokens += tokens;
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
               carets.sameTokens, carets.named, cuts.asked, cuts.sameTokens,
               cuts.named);
  CHECK(carets.sameTokens == carets.asked && carets.named == carets.asked);
  CHECK(cuts.sameTokens == cuts.asked && cuts.named == cuts.asked);
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

void an_implied_operator_reads_its_right_operand() {
  const auto lr = tllconditions::generated::Lr::create_parser();
  const auto ll = tllconditions::generated::Ll::create_parser();
  for (const auto* const parser : {lr.get(), ll.get()})
    for (const std::string_view text :
         {"times:2", "times:2 * 3", "times:2 3", "times:2 3 4", "times:2 3 * 4"}) {
      const auto parsed = parser->parse(text);
      const bool clean = parsed.accepted && parsed.diagnostics.empty();
      CHECK(clean);
      if (!clean) std::printf("  %s\n", std::string(text).c_str());
    }
}

void statements_ended_by_a_gap_or_a_semicolon() {
  const auto lr = tllconditions::generated::Lr::create_parser();
  const auto ll = tllconditions::generated::Ll::create_parser();
  sweep_language(
      "conditions", *lr, *ll,
      {"1 + 2;\nreturn x;\n", "{ return y\n a.b.c\n}", "return\nx;\n",
       "{ 1;\n{ 2 + 3 } }", "a.b;\n1 + 2 + 3\nreturn"});
}

// The word under the caret is a guarded terminal for one arm and a name for the
// other, and both go on from it: `do:` is as sound as `do?`, so the caret that
// replaces the `?` is offered the `:`.
void a_word_read_as_two_terminals_goes_on_as_either() {
  const auto ll = tllexpressions::generated::Ll::create_parser();
  CHECK((spellings(features_between(*ll, "do?", 2, 3, "reading:")) ==
         std::vector<std::string>{":", "?"}));
}

// A literal takes its spelling from the name at the same point: behind `do` the
// caret is offered what the literal goes on with, and not the `:` of the name.
void a_literal_takes_its_spelling_from_the_name() {
  const auto ll = tllexpressions::generated::Ll::create_parser();
  CHECK((spellings(features_between(*ll, "do?", 2, 3, "spelled:")) ==
         std::vector<std::string>{"?"}));
}

// After `LT`, which refuses a second `<`, the lexer picks `BARE`, yet the
// operand follows `LT`: a `<` typed there would turn `LT` into `BARE`, so it
// is not offered.
void the_twin_of_the_operator_is_not_offered_behind_it() {
  const auto ll = tllexpressions::generated::Ll::create_parser();
  for (const auto& [at, resume] : {std::pair{2u, 2u}, std::pair{2u, 3u}})
    CHECK((spellings(features_between(*ll, "a<b;", at, resume, "climb:")) ==
           std::vector<std::string>{"!", "ID"}));
}

// The choice between two rules that read `%` is made on the token after it,
// and the choice reads `->` where the position of the `-` does not: a `>`
// attached to the `-` makes it `->`, so it follows only after a blank.
void a_token_the_choice_read_joins_as_the_choice_reads_it() {
  const auto ll = tllexpressions::generated::Ll::create_parser();
  for (const std::string gap : {"", " "}) {
    const std::string text = "join:%-" + gap;
    const auto at = static_cast<cursiv::TextOffset>(text.size());
    const auto offered = spellings(features_between(*ll, text, at, at));
    CHECK(offered == (gap.empty() ? std::vector<std::string>{}
                                  : std::vector<std::string>{">"}));
    const std::string input = text + ">";
    const auto typed = ll->parse(input);
    CHECK((typed.accepted && typed.diagnostics.empty()) == !gap.empty());
  }
}

// A condition reads a run of `~` as one token and reads on past it. After a
// caret on its first token it reads the document; before a caret in the run it
// reads what is typed, and `?` makes it refuse the `|` after it.
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

// After a repair the replay still follows the descent: a run of inserted
// tokens, a misspelt word and deleted text each leave the stack the table
// engine names. Inside an unclosed comment, typed text belongs to the comment.
void carets_behind_a_repair_are_the_table_engines() {
  sweep_language("tpar repaired", *tpar::generated::Lr::create_parser(),
                 *tpar::generated::Ll::create_parser(),
                 {"sow a;", "shw all x, y;"}, false);
  sweep_language(
      "conditions repaired", *tllconditions::generated::Lr::create_parser(),
      *tllconditions::generated::Ll::create_parser(),
      {"a.(b;\n1 + 2 + 3\nreturn", "{ return y\n a.}b.c\n}", "/* c", "1 + /*"},
      false);
}

// Alternatives the same past their first token are all open behind the tokens
// they share, whichever the descent picked for the text past the caret.
void alternatives_equal_past_their_first_token_all_go_on() {
  sweep_language(
      "tpar forks", *tpar::generated::Lr::create_parser(),
      *tpar::generated::Ll::create_parser(),
      {"fork:fi a;", "fork:fi a()", "fork:x a a b", "fork:x a a c",
       "fork:up in a;", "fork:up in a()", "fork:ty a;", "fork:ty a()",
       "fork:tw tg tg;", "fork:tw tg tg:", "fork:fi a; ty b; x a b up in c()",
       "fork:pr a; pr b; pr c;", "fork:pr a; pr b; !"});
  sweep_language("conditions forks",
                 *tllconditions::generated::Lr::create_parser(),
                 *tllconditions::generated::Ll::create_parser(),
                 {"arrow:fk a", "arrow:fk a;", "arrow:fk a ()", "arrow:fk a x;",
                  "arrow:fk a; fk b x; fk c()"},
                 false);
}

// `->` covers the `-` a statement goes on with when the gap allows it: the
// stacks before the `-` are queried as the parser queried them, on the
// document's text, and a gap that refuses `->` leaves `-` and `>` to be read.
void a_gap_takes_a_token_off_the_position_as_the_drive_does() {
  const auto lr = tllconditions::generated::Lr::create_parser();
  const auto ll = tllconditions::generated::Ll::create_parser();
  const auto end = [](const cursiv::Parser& parser, const std::string& text) {
    return spellings(features_at(parser, text,
                                 static_cast<cursiv::TextOffset>(text.size())));
  };
  using Words = std::vector<std::string>;
  const std::string inLine = "arrow:sv a -";
  const std::string nextLine = "arrow:sv a\n-";
  const std::string refused = "arrow:sz a -";
  CHECK(end(*lr, inLine) == Words{">"});
  CHECK(end(*lr, nextLine).empty());
  CHECK(end(*lr, refused) == Words{">"});
  for (const std::string text : {"arrow:sz a -> b;", "arrow:sz a -> "}) {
    CHECK(!end(*lr, text).empty());
    CHECK(end(*lr, text) == end(*ll, text));
  }
}

// A word a repair read as the keyword it misspells is the author's: a word
// typed right behind it makes one word with it and is not offered there, a
// `(` is.
void a_misspelt_word_joins_what_is_typed_behind_it() {
  const auto lr = tpar::generated::Lr::create_parser();
  const auto ll = tpar::generated::Ll::create_parser();
  const std::string text = "shw all x, y;";
  for (const cursiv::Parser* parser : {lr.get(), ll.get()}) {
    const auto behind = features_at(*parser, text, 3);
    CHECK(std::ranges::any_of(behind,
                              [](const Entry& e) { return e.spelling == "("; }));
    CHECK(!offers(behind, "all", "all"));
    CHECK(offers(features_between(*parser, text, 4, 7), "all", "all"));
  }
}

// The parser refuses `aa` after the first `a` through the guarded reductions of
// the loop body, and reads `a`: the replay must read it the same way, or it
// loses the text from there on.
void a_guarded_reduction_refuses_a_token_as_the_drive_does() {
  const auto lr = tllconditions::generated::Lr::create_parser();
  const auto ll = tllconditions::generated::Ll::create_parser();
  const auto at = [](const cursiv::Parser& parser, const std::string& text,
                     std::size_t caret) {
    return spellings(
        features_at(parser, text, static_cast<cursiv::TextOffset>(caret)));
  };
  const std::string text = "andloop:aaa;";
  const auto clean = lr->parse(text);
  CHECK(clean.accepted && clean.diagnostics.empty());
  CHECK(std::ranges::count(at(*lr, text, 11), std::string(";")) == 1);
  CHECK(!at(*lr, text, 12).empty());
  CHECK(at(*lr, text, 12) == at(*ll, text, 12));
  const std::string behind = text + "\nreturn ";
  CHECK(at(*lr, behind, behind.size()) == at(*ll, behind, behind.size()));
  CHECK(!at(*lr, behind, behind.size()).empty());
  // The same refusal, by a condition that reads past the caret: the parser read
  // `a`, so the `b` after it comes next.
  const std::string pairs = "andpair:ababab;";
  const auto twice = lr->parse(pairs);
  CHECK(twice.accepted && twice.diagnostics.empty());
  for (const std::size_t caret : {9u, 11u, 13u}) {
    CHECK(at(*lr, pairs, caret) == std::vector<std::string>{"b"});
    CHECK(at(*lr, pairs, caret) == at(*ll, pairs, caret));
  }
}

// What the two engines say a token fills at a caret: the member, its class and
// whether the token opens the rule that writes it.
std::vector<std::string> members_at(const cursiv::Parser& parser,
                                    const std::string& text,
                                    cursiv::TextOffset at) {
  const cursiv::ParseResult parsed = parser.parse(text);
  std::vector<cursiv::NextFeature> next;
  parser.nextFeaturesAt(parsed.cst(), at, at, next);
  std::vector<std::string> out;
  for (const cursiv::NextFeature& f : next) {
    const auto spellings = parser.getGrammar().terminalSpellings(f.feature);
    std::string entry{spellings.size() == 1
                          ? spellings.front()
                          : parser.getGrammar().terminalName(f.feature)};
    if (!f.property.empty()) {
      entry += ':' + std::string(f.property);
      entry += '/' + std::string(f.type != nullptr ? f.type->name : "");
      if (f.entered) entry += "/entered";
    }
    out.push_back(std::move(entry));
  }
  std::sort(out.begin(), out.end());
  return out;
}

void each_member_a_token_fills_is_named_as_the_descent_names_it() {
  const auto lr = tpar::generated::Lr::create_parser();
  const auto ll = tpar::generated::Ll::create_parser();
  using Names = std::vector<std::string>;
  const auto end = [](const cursiv::Parser& parser, const std::string& text) {
    return members_at(parser, text, static_cast<cursiv::TextOffset>(text.size()));
  };
  // A record the token opens goes to the member of each call of its rule.
  CHECK(end(*lr, "member:twice ") ==
        (Names{"bx:first/Hold", "bx:second/Hold"}));
  // A word behind a rule of its own, past the token of the rule that writes it.
  CHECK(end(*lr, "member:through ") == Names{"ck:first/Hold"});
  // A rule that reads itself first: the call that names it, not its own.
  CHECK(end(*lr, "member:lone ") == Names{"lx:first/Hold"});
  CHECK(end(*lr, "member:both ") ==
        (Names{"lx:first/Hold", "lx:second/Hold"}));
  for (const std::string text :
       {"member:twice ", "member:through ", "member:lone ", "member:both ",
        "member:lone lx.", "member:lone lx.a ", "member:lone lx; lone "})
    CHECK(end(*lr, text) == end(*ll, text));
  // Only a site says what a token fills: elsewhere in the grammar the terminal
  // writes a member, which a site past a keyword does not.
  CHECK(end(*lr, "member:bare ") == Names{"TICK"});
  CHECK(end(*lr, "member:plain ") == Names{"QUOTE"});
  CHECK(end(*lr, "member:named ") == Names{"QUOTE:text/Hold"});
  // A rule that may read nothing keeps the member of the record behind it.
  CHECK(end(*lr, "member:marked ") == (Names{"mk:first/Hold", "nk:first/Hold"}));
  CHECK(end(*lr, "member:marked mk ") == Names{"nk:first/Hold"});
  for (const std::string text :
       {"member:bare ", "member:plain ", "member:named ", "member:marked ",
        "member:marked mk ", "member:", "member:bare $a; "})
    CHECK(end(*lr, text) == end(*ll, text));
  // A member past a repetition opens its rule while no turn was read, and so
  // does one under a choice of what spells it. Past a rule that may read
  // nothing, every call of the choice answers.
  const Names unread{"ID:text/Hold/entered", "bx:marks/Hold/entered",
                     "in:text/Hold/entered", "mk:turns/Hold",
                     "nk:text/Hold",         "zz:turns/Hold",
                     "}"};
  CHECK(end(*lr, "member:turns { ") == unread);
  CHECK(end(*lr, "member:turns { a = , ") == unread);
  CHECK(end(*lr, "member:turns { bx ") ==
        (Names{"ID:text/Hold", "bx:marks/Hold/entered", "in:text/Hold", "mk",
               "nk:text/Hold", "zz"}));
  // A word read in place, ahead of a call the descent comes back from.
  for (const auto* const parser : {lr.get(), ll.get()}) {
    const auto parsed = parser->parse("member:picked pa [[]] ; picked pb [] ;");
    CHECK(parsed.accepted && parsed.diagnostics.empty());
  }
  // A rule entered by itself alone fills nothing at a caret.
  const auto start = end(*lr, "");
  CHECK(std::ranges::count(start, std::string("s0")) == 1);
  CHECK(start == end(*ll, ""));
  // The member stands past the token of the rule that writes it: no token opens
  // that rule at the caret, whatever opens behind it.
  CHECK(end(*lr, "let a ") == end(*ll, "let a "));
}

// The closure stacks of a caret reach one frame from each of a long chain of
// prefixes: the cost is linear in the number of predecessors.
void a_frame_of_many_predecessors_costs_their_number() {
  const auto lr = tpar::generated::Lr::create_parser();
  const std::string text = "let a = " + std::string(512000, '~') + "1;";
  const auto parsed = lr->parse(text);
  CHECK(parsed.accepted && parsed.diagnostics.empty());
  std::vector<cursiv::NextFeature> next;
  const auto begin = std::chrono::steady_clock::now();
  lr->nextFeaturesAt(parsed.cst(), static_cast<cursiv::TextOffset>(text.size()),
                     static_cast<cursiv::TextOffset>(text.size()), next);
  CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(8));
  CHECK(!next.empty());
}

// A stop requested before the question throws once the replay reaches a poll.
void a_stopped_question_throws_in_the_replay() {
  std::string text = "fork:";
  for (int i = 0; i < 4000; ++i) text += "pr a; ";
  const auto at = static_cast<cursiv::TextOffset>(text.size());
  cursiv::CancellationTokenSource stop;
  stop.request_stop();
  const auto lr = tpar::generated::Lr::create_parser();
  const auto ll = tpar::generated::Ll::create_parser();
  for (const cursiv::Parser* parser : {lr.get(), ll.get()}) {
    const auto parsed = parser->parse(text);
    std::vector<cursiv::NextFeature> next;
    bool stopped = false;
    try {
      parser->nextFeaturesAt(parsed.cst(), at, at, next, stop.get_token());
    } catch (const cursiv::OperationCancelled&) {
      stopped = true;
    }
    CHECK(stopped);
  }
}

}  // namespace

int main() {
  an_expression_with_parentheses_calls_and_a_postfix_mark();
  a_closer_needs_its_opener_and_a_mark_its_member();
  statements_ended_by_a_gap_or_a_semicolon();
  an_implied_operator_reads_its_right_operand();
  a_word_read_as_two_terminals_goes_on_as_either();
  a_literal_takes_its_spelling_from_the_name();
  the_twin_of_the_operator_is_not_offered_behind_it();
  a_token_the_choice_read_joins_as_the_choice_reads_it();
  a_condition_reads_the_tokens_behind_the_caret_its_own_way();
  carets_behind_a_repair_are_the_table_engines();
  alternatives_equal_past_their_first_token_all_go_on();
  a_gap_takes_a_token_off_the_position_as_the_drive_does();
  a_misspelt_word_joins_what_is_typed_behind_it();
  a_guarded_reduction_refuses_a_token_as_the_drive_does();
  each_member_a_token_fills_is_named_as_the_descent_names_it();
  a_frame_of_many_predecessors_costs_their_number();
  a_stopped_question_throws_in_the_replay();
  return cursiv::test::report("LlCaretParity_test");
}
