#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/validation/RepairMessage.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/ll/runtime/ll_predictor.hpp"
#include "cursiv/engines/ll/runtime/ll_read.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "dedentrunflatll/generated/FlatLl/Module.hpp"
#include "dedentrunll/generated/Ll/Module.hpp"
#include "delimll/generated/Ll/Module.hpp"
#include "engines/ll/DedentRunGrammar.hpp"
#include "engines/ll/DelimSpellingGrammar.hpp"
#include "engines/ll/LlTerms.hpp"
#include "engines/lr/IndentAst.hpp"
#include "indent/generated/G/Module.hpp"
#include "indentrepaired/generated/G/Module.hpp"
#include "indentfolded/generated/Cases/Module.hpp"
#include "indentimplied/generated/Cases/Module.hpp"
#include "llindent/generated/Ll/Module.hpp"
#include "llindent/generated/Ll/Table.hpp"
#include "llindentbare/generated/Bare/Module.hpp"
#include "llindentfolded/generated/Folded/Module.hpp"
#include "llindentlaid/generated/Laid/Module.hpp"

namespace {
using cursiv::test::failures;

void explicit_statement_newlines_win_over_their_trivia_twin() {
  constexpr auto& table = tllindent::generated::Ll::kTable;
  constexpr auto newline = [] {
    for (std::uint16_t t = 1; t < table.nterms; ++t)
      if (table.terms[t].joins) return t;
    return cursiv::kNoElement;
  }();
  static_assert(newline != cursiv::kNoElement);
  constexpr auto mode = [] {
    for (std::uint16_t m = 0; m < table.nmodes; ++m)
      if (table.mode_admits(m, newline)) return m;
    return cursiv::kInvalidSymbol;
  }();
  static_assert(mode != cursiv::kInvalidSymbol);
  // The trivia twin is no rival: no mode reads two terminals from one text,
  // so a choice the newline settles is a table, not a prediction.
  static_assert([] {
    for (std::uint16_t m = 0; m < table.nmodes; ++m)
      if (table.modeMulti[m] != 0) return false;
    return true;
  }());
  {
    cursiv::Cst tree("\n  b");
    cursiv::CstBuilder builder(tree);
    builder.bind_stream();
    cursiv::LlContext context(tree.text(), builder);
    const auto scan =
        cursiv::ll_match_text<table>(context.src, context.len, mode, 0);
    CHECK(scan.term == newline && scan.end == 1);
    cursiv::ll_peek<table>(context, mode);
    CHECK(context.la.sym == newline && context.la.begin == 0 &&
          context.la.end == 1);
  }
  {
    cursiv::Cst tree("\na");
    cursiv::CstBuilder builder(tree);
    builder.bind_stream();
    cursiv::LlContext context(tree.text(), builder);
    cursiv::LlMask<table.termWords> viable;
    viable.set(newline);
    constexpr auto identifier = ll_terms::named<table>("ID");
    static_assert(identifier != cursiv::kInvalidSymbol);
    viable.set(identifier);
    std::vector<std::uint16_t> readings;
    cursiv::TextOffset from = 0;
    cursiv::LlLookRead detail;
    CHECK(cursiv::ll_look<table>(context, from, viable, readings,
                                table.errMode, &detail));
    CHECK(detail.token.begin == 0 && detail.token.end == 1);
    CHECK(readings.size() == 1 && readings[0] == newline);
    viable.reset();
    viable.set(identifier);
    from = 0;
    CHECK(cursiv::ll_look<table>(context, from, viable, readings,
                                table.errMode, &detail));
    CHECK(detail.token.begin == 1 && detail.token.end == 2);
    CHECK(readings.size() == 1 && readings[0] == identifier);
    // An active newline joins the physical lines inside brackets.
    viable.set(newline);
    cursiv::LlLookIndent indent;
    indent.at.depth = 1;
    from = 0;
    CHECK(cursiv::ll_look<table>(context, from, viable, readings,
                                table.errMode, &detail, &indent));
    CHECK(detail.token.begin == 1 && detail.token.end == 2);
    CHECK(readings.size() == 1 && readings[0] == identifier);
  }
  {
    // Inside brackets the same newline remains a blank.
    cursiv::Cst tree("\n");
    cursiv::CstBuilder builder(tree);
    builder.bind_stream();
    cursiv::LlContext context(tree.text(), builder);
    context.indent.depth = 1;
    cursiv::ll_peek<table>(context, table.errMode);
    CHECK(context.la.sym == cursiv::kEofSymbol && context.la.begin == 1);
  }
}

void parity(const cursiv::Parser& ll, const cursiv::Parser& lr,
            const cursiv::AstReflection& reflection,
            const std::vector<std::string>& corpus) {
  for (const auto& text : corpus) {
    const auto a = ll.parse(text);
    const auto b = lr.parse(text);
    const bool cleanA = a.accepted && a.diagnostics.empty();
    const bool cleanB = b.accepted && b.diagnostics.empty();
    if (cleanA != cleanB)
      std::printf(
          "indent parity input (LL=%d LR=%d, diagnostics %zu/%zu): %s\n",
          cleanA, cleanB, a.diagnostics.size(), b.diagnostics.size(),
          text.c_str());
    CHECK(cleanA == cleanB);
    if (cleanA && cleanB && a.value != nullptr && b.value != nullptr) {
      std::string x, y;
      cursiv::test::ast_of(reflection, *a.value, x);
      cursiv::test::ast_of(reflection, *b.value, y);
      CHECK(x == y);
    }
    for (cursiv::NodeId i = 0; i < a.cst().size(); ++i) {
      const auto& node = a.cst()[i];
      CHECK(node.begin <= node.end && node.end <= text.size());
      CHECK(node.subtreeSize != 0 && node.subtreeSize <= i + 1u);
    }
  }
}

// A dedent the repair drops closes its level and owns no byte: what follows
// it is read whole.
void dropped_dedents_split_no_token() {
  const auto ll = tllindent::generated::Ll::create_parser();
  const auto letter = [](char ch) { return ch >= 'a' && ch <= 'z'; };
  for (const std::string_view text :
       {"\n    :\n    ::\nbb bb\n\nc", ":\n  \n  :\ncbb]\n    \n    "}) {
    const auto result = ll->parse(text);
    CHECK(!result.diagnostics.empty());
    for (const auto& d : result.diagnostics) {
      if (d.kind != cursiv::DiagnosticKind::deleted || d.begin == d.end)
        continue;
      CHECK(d.begin == 0 || !letter(text[d.begin - 1]) ||
            !letter(text[d.begin]));
      CHECK(d.end == text.size() || !letter(text[d.end - 1]) ||
            !letter(text[d.end]));
    }
  }
}

// An indent a repair drops owns no byte, as a dedent does, and invents nothing:
// the name it falls inside is written, as it is where the next line is at the
// same level.
void a_dropped_indent_leaves_the_name_it_splits(const cursiv::Parser& p) {
  for (const std::string_view text : {"dotted a.\nb\n", "dotted a.\n  b\n"}) {
    const auto result = p.parse(text);
    const auto* root = result.value_as<tindent::ast::Node>();
    CHECK(result.accepted && result.diagnostics.size() == 1 && root);
    for (const auto& d : result.diagnostics)
      CHECK(d.kind == cursiv::DiagnosticKind::deleted &&
            text.substr(d.begin, d.end - d.begin) == "\n");
    if (root != nullptr) CHECK(root->name == "a.b");
  }
}

// The end of input under a level whose indent a repair dropped closes it as
// the blank it stood for: no dedent is read there, and none is dropped.
void muted_levels_lay_no_dedent_at_the_end() {
  const auto ll = tllindent::generated::Ll::create_parser();
  for (const std::string_view text : {"a\n  b", "a\n  b\n", "a:\n    b\n  c"}) {
    const auto result = ll->parse(text);
    const auto& cst = result.cst();
    for (cursiv::NodeId i = 0; i < cst.size(); ++i)
      CHECK(cst[i].subtreeSize != 1 || cst[i].begin != text.size());
  }
}

// Each way a line's indentation goes wrong -- too deep, back to no level,
// another blank, a form feed -- is one repair that says so, in both engines.
void an_indentation_error_says_so() {
  const auto ll = tllindent::generated::Ll::create_parser();
  const auto lr = tindent::generated::G::create_parser();
  for (const cursiv::Parser* const p : {ll.get(), lr.get()})
    for (const std::string_view text :
         {"a\n  b\nc\n", "a:\n    b\n  c\n", "a:\n\tb\n    c\n",
          "a:\n  b\n\f  c\n"}) {
      const auto result = p->parse(text);
      CHECK(result.diagnostics.size() == 1);
      for (const auto& d : result.diagnostics)
        CHECK(cursiv::repair_message(p->getGrammar(), d, text) ==
              "Unexpected indentation.");
    }
}

// An indent a repair invents opens a level the line back at the enclosing one
// closes: one fault, and the tree the other engine builds.
void an_invented_indent_opens_a_level() {
  const auto ll = tllindent::generated::Ll::create_parser();
  const auto lr = tindent::generated::G::create_parser();
  const auto rl = tllindent::generated::Ll::create_ast_reflection();
  const auto rr = tindent::generated::G::create_ast_reflection();
  for (const std::string_view text : {"a:\nb\n", "a:\n  b:\nc\n"}) {
    const auto a = ll->parse(text);
    const auto b = lr->parse(text);
    CHECK(a.diagnostics.size() == b.diagnostics.size());
    std::string x, y;
    cursiv::test::ast_of(*rl, *a.value, x);
    cursiv::test::ast_of(*rr, *b.value, y);
    CHECK(x == y);
  }
}

// The repairs are the other engine's, down to what they name.
void repairs_agree_with_the_table_engine(
    const std::vector<std::string_view>& corpus) {
  const auto ll = tllindent::generated::Ll::create_parser();
  const auto lr = tindent::generated::G::create_parser();
  const auto rl = tllindent::generated::Ll::create_ast_reflection();
  const auto rr = tindent::generated::G::create_ast_reflection();
  for (const std::string_view text : corpus) {
    const auto a = ll->parse(text);
    const auto b = lr->parse(text);
    CHECK(a.diagnostics.size() == b.diagnostics.size());
    for (std::size_t i = 0;
         i < a.diagnostics.size() && i < b.diagnostics.size(); ++i) {
      const auto& x = a.diagnostics[i];
      const auto& y = b.diagnostics[i];
      CHECK(x.kind == y.kind && x.begin == y.begin && x.end == y.end &&
            x.element == y.element && x.also == y.also);
      CHECK(x.kind != cursiv::DiagnosticKind::incomplete);
    }
    std::string s, t;
    cursiv::test::ast_of(*rl, *a.value, s);
    cursiv::test::ast_of(*rr, *b.value, t);
    CHECK(s == t);
  }
}

// An indent no block asked for is one fault, and the lines behind it are read,
// a last line with no newline too.
void a_stray_indent_costs_one_fault() {
  repairs_agree_with_the_table_engine(
      {"a:\n  b\n    c\n  d\ne\n", "  a\nb\n", "a:\n  b\n c\n", "a\n  b\nc\n",
       "    a\n  b\nc\n", "a::\n  a\nb\n", "s0::\n  0\n", "s0:s1\n",
       "s0\n s1\n", "s0\n\ts1\n", "s0\n  s1\n", "0:\n  s0\n", "s0\ns1\n s\n",
       "\ns0\n\ts1\n", "a:\n  b\n    c"});
}

// A form feed in a run is an indentation error, wherever the line goes on --
// to the end of the input too. Past content it is plain trivia.
void a_form_feed_in_a_run_is_an_error() {
  const auto ll = tllindent::generated::Ll::create_parser();
  for (const std::string_view text :
       {"a:\n  b\n\f\nc\n", "a:\n  b:\n    c\n\fd\n", "a:\n  b\n \f c\n",
        "a\n\f", "a:\n  b\n\f", "a:\n  b\n  \f", "a:\n  b:\n    c\n\f",
        "a:\n  b\nc\n\f"}) {
    const auto result = ll->parse(text);
    CHECK(result.accepted && !result.diagnostics.empty());
  }
  for (const std::string_view text : {"a:\n  b\f", "a:\n  b \f\n"}) {
    const auto result = ll->parse(text);
    CHECK(result.accepted && result.diagnostics.empty());
  }
}

// A bracket a repair invents in front of the text it cannot read is a level
// the lines behind it are read in: a newline there is a blank, not an end.
void an_invented_bracket_moves_the_levels_of_the_lines_behind() {
  repairs_agree_with_the_table_engine({"a\n  c\n]\n", "a\n  c\n]\nd\n",
                                       "a  b\n  c\n]\n", "ab\n  c\n]\nd\n",
                                       "a  b\n c\n]\nd"});
}

// A dropped dedent moves no byte: the invention it undoes is not progress.
void an_invented_block_closed_at_once_ends() {
  const auto ll = tllindent::generated::Ll::create_parser();
  CHECK(ll->parse("s:\n  s:\n# c\n    s\n").accepted);
}

// Blocks with comment and blank lines at any depth: where the line a trivia
// ends on would say an indentation, the two engines must still agree.
std::vector<std::string> blocks(unsigned count) {
  std::mt19937 rng(7);
  std::vector<std::string> corpus;
  const auto block = [&](auto& self, std::string& out, unsigned depth) -> void {
    for (unsigned i = 0, n = 1 + rng() % 3; i < n; ++i) {
      if (rng() % 4 == 0) out += std::string(2 * (rng() % 3), ' ') + "# c\n";
      if (rng() % 5 == 0) out += "\n";
      out += std::string(2 * depth, ' ') + "s";
      if (depth < 3 && rng() % 3 == 0) {
        out += ":\n";
        self(self, out, depth + 1);
      } else {
        out += "\n";
      }
    }
  };
  for (unsigned i = 0; i < count; ++i) {
    std::string text;
    block(block, text, 0);
    corpus.push_back(std::move(text));
  }
  return corpus;
}

// What a caret over [from, to) is offered, by the name and spelling of each
// terminal. The tokens the indentation lays are no text to offer, unless
// `laid` asks for them.
std::vector<std::string> offered(const cursiv::Parser& parser,
                                 const std::string& text,
                                 cursiv::TextOffset from, cursiv::TextOffset to,
                                 bool laid = false) {
  const auto result = parser.parse(text);
  std::vector<cursiv::NextFeature> next;
  parser.nextFeaturesAt(result.cst(), from, to, next);
  std::vector<std::string> out;
  for (const auto& f : next) {
    const auto name = parser.getGrammar().terminalName(f.feature);
    if (!laid && (name == "INDENT" || name == "DEDENT")) continue;
    const auto spellings = parser.getGrammar().terminalSpellings(f.feature);
    if (spellings.empty()) out.push_back(std::string(name) + " ");
    for (const std::string_view s : spellings)
      out.push_back(std::string(name) + " " + std::string(s));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

bool offers(const cursiv::Parser& parser, const std::string& text,
            cursiv::TextOffset from, cursiv::TextOffset to,
            std::string_view spelling) {
  const auto offers = offered(parser, text, from, to);
  return std::find_if(offers.begin(), offers.end(), [&](const auto& offer) {
           return offer.ends_with(" " + std::string(spelling));
         }) != offers.end();
}

std::string joined(const std::vector<std::string>& names) {
  std::string out;
  for (const auto& name : names) out += "[" + name + "]";
  return out;
}

// A caret stands where the other engine's does, in or behind every token of a
// valid document: past an indent, a dedent and the closing of a suite alike.
void a_caret_agrees_with_the_table_engine_past_the_levels(
    const cursiv::Parser& ll, const cursiv::Parser& lr,
    const std::vector<std::string>& corpus) {
  for (const auto& text : corpus) {
    CHECK(ll.parse(text).accepted && ll.parse(text).diagnostics.empty());
    for (cursiv::TextOffset at = 1; at <= text.size(); ++at) {
      if (std::string_view(" \t\r\n").find(text[at - 1]) !=
          std::string_view::npos)
        continue;
      const auto a = offered(ll, text, at, at);
      const auto b = offered(lr, text, at, at);
      CHECK(a == b);
      if (a != b)
        std::printf("caret %u in [%s]: LL %s | LR %s\n", at,
                    std::string(text).c_str(), joined(a).c_str(),
                    joined(b).c_str());
    }
  }
}

// A suite's first line is offered its words: the indent the caret stands
// glued to is no text a lexer reads again.
void a_caret_behind_an_indent_is_offered_the_words_of_the_suite() {
  const auto ll = tllindent::generated::Laid::create_parser();
  CHECK(offers(*ll, "suites top a:\n  pa", 16, 18, "pass"));
  CHECK(offers(*ll, "suites top a:\n  pass\n", 16, 20, "pass"));
  CHECK(offers(*ll, "suites top a:\n  pass\n  pa", 23, 25, "pass"));
}

// A dedent the descent read closed the suite: the line it opens is offered the
// words of the level it leaves, whether the caret stands at the end of the
// text or inside the word.
void a_caret_past_a_dedent_is_offered_the_words_of_the_outer_level() {
  const auto ll = tllindent::generated::Laid::create_parser();
  CHECK(offers(*ll, "suites top a:\n  pass\n", 21, 21, "top"));
  CHECK(!offers(*ll, "suites top a:\n  pass\n", 21, 21, "pass"));
  CHECK(offers(*ll, "suites top a:\n  pass\ntop b:\n  pass\n", 21, 24, "top"));
  CHECK(
      !offers(*ll, "suites top a:\n  pass\ntop b:\n  pass\n", 21, 24, "pass"));
  CHECK(!offers(*ll, "suites top a:\n  pass", 20, 20, "top"));
}

struct Reading {
  bool accepted = false;
  std::size_t diagnostics = 0;
  std::string ast;
};

template <class Pair>
Reading read(Pair& p, std::string_view text) {
  const auto result = p.first->parse(text);
  Reading out{result.accepted, result.diagnostics.size(), {}};
  if (result.value) cursiv::test::ast_of(*p.second, *result.value, out.ast);
  return out;
}

template <class Pair>
void expect_closures(Pair& ll, std::string_view text, std::size_t closures,
           bool clean) {
  const auto a = read(ll, text);
  CHECK((a.diagnostics == 0) == clean);
  if (!clean) return;
  std::string want = "Node{";
  for (std::size_t i = 0; i < closures; ++i)
    want += "closures" + std::to_string(i) + "=Closure{} ";
  want += '}';
  CHECK(a.accepted && a.ast == want);
}

template <class Parser>
void closes(const Parser& parser, std::string_view text, std::size_t levels) {
  const auto result = parser->parse(text);
  CHECK(result.accepted && result.diagnostics.empty());
  CHECK(result.template value_as<tlldedentrun::Marks>()->dedents.size() ==
        levels);
}

// A dropped dedent closes a level and owns no byte: the text past it is still
// dropped, whether the entry stops at `eof` or before the text does.
template <class Parser>
void drops_past_dedents_to_the_end(const Parser& parser,
                                   std::string_view text) {
  const auto result = parser->parse(text);
  const auto& cst = result.cst();
  cursiv::TextOffset end = 0;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i)
    end = std::max(end, cst[i].end);
  CHECK(!result.diagnostics.empty() && end == text.size());
}

template <class... Pairs>
void agree(std::string_view text, std::string_view expected, Pairs&... pairs) {
  const Reading readings[] = {read(pairs, text)...};
  std::size_t at = 0;
  for (const auto& r : readings) {
    const bool same =
        r.accepted && r.diagnostics == 0 && !r.ast.empty() &&
        r.ast == expected;
    if (!same)
      std::printf("delim spelling reading %zu (%zu diagnostics): %s\n", at,
                  r.diagnostics, std::string(text).c_str());
    CHECK(same);
    ++at;
  }
}

// The brackets are the terminals the rules read: a spelling read only through
// a named terminal is that terminal, and no terminal of its own.
template <class Lowered>
void brackets_are_terminals_read(const Lowered& low) {
  unsigned opens = 0, closes = 0;
  for (std::uint16_t t = 1; t < low.cfg.nterms; ++t) {
    const std::string_view name = low.cfg.term_name(t);
    for (const std::string_view named : {"[", "]", "{", "}", ">"})
      CHECK(name != named);
    opens += low.cfg.terms[t].delim > 0 ? 1 : 0;
    closes += low.cfg.terms[t].delim < 0 ? 1 : 0;
  }
  CHECK(opens == 4 && closes == 4);
}
}  // namespace

// The dedents a descent read have left the levels behind: the caret on the line
// after them is not offered one to close what is closed.
void a_caret_past_closed_suites_is_not_offered_their_closer() {
  const auto ll = tllindent::generated::Ll::create_parser();
  const std::string text = "a:\n  b\n  c:\n    d\n  e:\n    f\ng\nh\n";
  const auto offers_dedent = [&](cursiv::TextOffset at) {
    const auto offers = offered(*ll, text, at, at, true);
    return std::find(offers.begin(), offers.end(), "DEDENT ") != offers.end();
  };
  CHECK(!offers_dedent(29));
}

int main() {
  a_caret_past_closed_suites_is_not_offered_their_closer();
  a_caret_behind_an_indent_is_offered_the_words_of_the_suite();
  a_caret_past_a_dedent_is_offered_the_words_of_the_outer_level();
  a_caret_agrees_with_the_table_engine_past_the_levels(
      *tllindent::generated::Laid::create_parser(),
      *tindent::laid::generated::Cases::create_parser(),
      {"suites top a:\n  pass\n", "suites top a:\n  pass\n  pass\n",
       "suites top a:\n  pass\ntop b:\n  pass\n",
       "suites top a:\n  pass\n  pass\ntop b:\n  pass\n  pass\n"});
  a_caret_agrees_with_the_table_engine_past_the_levels(
      *tllindent::generated::Ll::create_parser(),
      *tindent::generated::G::create_parser(),
      {"a:\n  b\n  c\n", "a:\n  b\nc\n", "a:\n  b:\n    c\n  d\ne\n",
       "a:\n  b:\n    c\n", "a\nb:\n  c\n"});
  explicit_statement_newlines_win_over_their_trivia_twin();
  dropped_dedents_split_no_token();
  muted_levels_lay_no_dedent_at_the_end();
  a_dropped_indent_leaves_the_name_it_splits(
      *tllindent::generated::Laid::create_parser());
  a_dropped_indent_leaves_the_name_it_splits(
      *tindent::laid::generated::Cases::create_parser());
  an_invented_indent_opens_a_level();
  an_indentation_error_says_so();
  a_stray_indent_costs_one_fault();
  an_invented_bracket_moves_the_levels_of_the_lines_behind();
  an_invented_block_closed_at_once_ends();
  a_form_feed_in_a_run_is_an_error();
  std::string suites;
  for (unsigned i = 0; i < 1024; ++i) suites += "a:\n  b\n";
  parity(*tllindent::generated::Ll::create_parser(),
         *tindent::generated::G::create_parser(),
         *tllindent::generated::Ll::create_ast_reflection(),
         {"a\n", "a:\n  b\nc\n", "a:\n  b:\n    c\n  d\ne\n",
          "a:\n\n  b\n\nc\n", "a:\n\tb\nc\n", "a:\n  b", "a [\n  b\n  c\n]\n",
          "a \\\n b\n", "a:\n  b\n c\n", "a:\n\tb\n c\n", "a\n\n", "a\n  ",
          "a\n#tail\n", "a:\n  b\n\n", "a:\n  b\n  #tail\n", "a:\n  b\n\f\nc\n",
          "a:\n  b:\n    c\n\fd\n", "a:\n  b\n \f c\n", "a:\n  b\n\f",
          "a:\n  b:\n    c\n  \f", "a\n\f", suites});
  parity(*tllindent::generated::Folded::create_parser(),
         *tindent::folded::generated::Cases::create_parser(),
         *tllindent::generated::Folded::create_ast_reflection(),
         {"a:\n  # comment\n  b\nc\n", "a:\n  b # tail\n\n  c\nd\n"});
  parity(*tllindent::generated::Ll::create_parser(),
         *tindent::generated::G::create_parser(),
         *tllindent::generated::Ll::create_ast_reflection(), blocks(2000));
  parity(*tllindent::generated::Folded::create_parser(),
         *tindent::folded::generated::Cases::create_parser(),
         *tllindent::generated::Folded::create_ast_reflection(), blocks(2000));
  parity(*tllindent::generated::Bare::create_parser(),
         *tindent::repaired::generated::G::create_parser(),
         *tllindent::generated::Bare::create_ast_reflection(),
         {"a:\n  b\nc\n", "a:\n  b:\n    c\n"});
  parity(*tllindent::generated::Laid::create_parser(),
         *tindent::laid::generated::Cases::create_parser(),
         *tllindent::generated::Laid::create_ast_reflection(),
         {"a", "a:\n  b", "a:\n  b\nc"});
  namespace dedent = tlldedentrun::generated;
  auto runs = std::pair{dedent::Ll::create_parser(),
                        dedent::Ll::create_ast_reflection()};
  expect_closures(runs, "some:a\n b\n  c\nd", 2, true);
  expect_closures(runs, "some:a\n b\n  c\n d", 1, true);
  expect_closures(runs, "many:a\n b\n  c\nd", 2, true);
  expect_closures(runs, "many:a\n b\n  c\n d", 1, true);
  expect_closures(runs, "many:a\n b\n  c\n  d", 0, true);
  expect_closures(runs, "some:a\n b\n  c\n  d", 0, false);
  drops_past_dedents_to_the_end(runs.first, "many:a\n b\n  c\n  d\n e\nf");
  drops_past_dedents_to_the_end(runs.first, "tail:a\n b\n  c\nd");
  const auto flatLl = dedent::FlatLl::create_parser();
  for (const auto text : {"a:\n  b:\n    c", "a:\n  b:\n    c\n"})
    closes(flatLl, text, 2);
  brackets_are_terminals_read(cursiv::LlLowering<tdelim::Ll>::lower());
  brackets_are_terminals_read(cursiv::LrLowering<tdelim::Lr>::lower());
  auto delim = std::pair{tdelim::generated::Ll::create_parser(),
                         tdelim::generated::Ll::create_ast_reflection()};
  struct Case { std::string_view before, body, after; };
  for (const auto& c : {
           Case{"a ", "b\n c", "\nd\n"},
           Case{"a ", "\n  b\n  .c\n", "\nd\n"},
           Case{"s:\n  a ", "\nb\n   .c\n  ", "\n  d\n"},
           Case{"s:\n  a ", " b\n .c d\n.e", "\ne\n"},
       }) {
    const auto wrap = [&](std::string_view open, std::string_view close) {
      return std::string(c.before) + std::string(open) + std::string(c.body) +
             std::string(close) + std::string(c.after);
    };
    const auto expected = read(delim, wrap("(", ")"));
    for (const auto [open, close] :
         {std::pair{"(", ")"}, std::pair{"[", "]"}, std::pair{"{", "}"},
          std::pair{"<", ">"}, std::pair{"<~", ">"}})
      agree(wrap(open, close), expected.ast, delim);
  }
  return cursiv::test::report("LlIndent_test");
}
