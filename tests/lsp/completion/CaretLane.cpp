// What the caret reads off the engine: a keyword the author gave a name of its
// own, the arm a cell decided past the caret did not keep, the state a repair
// left standing behind an unclosed construct, and what the site lets a
// reference hold. One grammar, because every answer comes from the same read.

#include "lsp/completion/CaretLane.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/test/Check.hpp"
#include "lsp/completion/CaretGrammar.hpp"
#include "support/Editor.hpp"
#include "support/World.hpp"

namespace {

namespace ast = tcaret::ast;

using support::offered;
using support::offers;

struct World : support::World<cursiv::SharedLspServices> {
  cursiv::LspServices* lang;

  explicit World(const CaretDoor& door)
      : lang(&support::add_language<cursiv::LspServices>(
            shared, {"caret", {".caret"}, {}}, door.parser(),
            door.reflection())) {
    support::take_both_ranges(shared);
  }
};

// `KW_END` is `kw<"end">` behind a `terminal<>` of its own, which is what the
// grammar carries: naming it does not stop it spelling its bytes.
void a_named_keyword_is_offered_by_its_bytes(const CaretDoor& door) {
  const World world(door);
  offered(*world.lang, "fn f() { e<|> }", [](const lsp::CompletionList& list) {
    CHECK(offers(list, "end"));
    CHECK(!offers(list, "KW_END"));
    CHECK(!offers(list, "ID"));
  });
}

// A terminal answers to what it SPELLS, never to what its author called it:
// the scan's probe compares the text, and `KW_END` is a name that means
// nothing to a document.
void a_terminal_does_not_answer_to_its_own_name(const CaretDoor& door) {
  const std::unique_ptr<cursiv::Parser> parser = door.parser();
  const auto takes = [&](std::string_view text) {
    const cursiv::ParseResult r = parser->parse(text);
    return r.cst().accepted() && !r.cst().recovered();
  };
  CHECK(takes("fn f() { end; }"));
  CHECK(!takes("fn f() { KW_END; }"));
  CHECK(!takes("fn f() { end; IKW_LET; }"));
}

// ...and it wins its ties the same way, whichever side declared first: `end`
// and `let` both stand against an `ID` declared before them.
void a_named_literal_beats_a_named_terminal_at_equal_length(
    const CaretDoor& door) {
  const std::unique_ptr<cursiv::Parser> parser = door.parser();
  const cursiv::ParseResult r = parser->parse("fn f() { end; let; }");
  CHECK(r.cst().accepted() && !r.cst().recovered());
  auto* const file = r.value_as<ast::File>();
  CHECK(file != nullptr);
  if (file == nullptr || file->fns.size() != 1) return;
  auto* const block = static_cast<ast::Block*>(file->fns[0]->body);
  CHECK(block != nullptr);
  if (block == nullptr || block->stmts.size() != 2) return;
  CHECK(cursiv::ast_cast<ast::KwWord>(block->stmts[0]) != nullptr);
  CHECK(cursiv::ast_cast<ast::IKwWord>(block->stmts[1]) != nullptr);
}

// After `@ ~` the engine has reduced one of two readings, elected on the token
// that follows; a caret sits before that token, so both arms are still open.
void a_k2_cell_offers_both_arms(const CaretDoor& door) {
  const World world(door);
  offered(*world.lang, "fn f() { @~<|>!; }",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "!"));
            CHECK(offers(list, "?"));
          });
}

// `see` binds an `Alpha` and nothing else, though `Link::to` accepts every
// `Sym`: at the caret the reference is not written yet, so the narrowing can
// only come from the grammar's site.
void a_narrowed_site_offers_its_subtype_alone(const CaretDoor& door) {
  const World world(door);
  offered(*world.lang, "fn f() { alpha a; beta b; see <|> }",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "a"));
            CHECK(!offers(list, "b"));
          });
}

// Two classes spell `to`, so the member name settles nothing: the class the
// site writes in is what picks the reference, and the record is not open yet
// at a caret standing in front of it.
void a_member_two_classes_spell_answers_for_its_own_site(const CaretDoor& door) {
  const World world(door);
  offered(*world.lang, "fn f() { alpha a; beta b; tie <|> }",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "b"));
            CHECK(!offers(list, "a"));
          });
}

void a_new_reference_uses_its_own_type_before_an_ancestors(
    const CaretDoor& door) {
  const World world(door);
  offered(*world.lang, "fn f() { alpha a; beta b; nest a { tie <|> } }",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "b"));
            CHECK(!offers(list, "a"));
          });
}

// The same member at a site that narrows nothing: what the member accepts is
// what stands there, and nothing is thinned.
void a_site_that_narrows_nothing_offers_the_whole_member(
    const CaretDoor& door) {
  const World world(door);
  offered(*world.lang, "fn f() { alpha a; beta b; any <|> }",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "a"));
            CHECK(offers(list, "b"));
          });
}

// The member a completion would fill, which no `CompletionList` carries: it
// is read off the engine beside the terminal, at the end of what is written.
[[nodiscard]] std::string_view member(const World& world, std::string_view text,
                                      std::string_view spelling) {
  const cursiv::Parser& parser = *world.lang->parser;
  // The tree borrows its text, so the source outlives the read below.
  const std::string source{text};
  const cursiv::ParseResult result = parser.parse(source);
  const auto at = static_cast<cursiv::TextOffset>(source.size());
  std::vector<cursiv::NextFeature> out;
  parser.nextFeaturesAt(result.cst(), at, at, out);
  const cursiv::Grammar& grammar = parser.getGrammar();
  for (const cursiv::NextFeature& f : out)
    for (const std::string_view s : grammar.terminalSpellings(f.feature))
      if (s == spelling) return f.property;
  return "<absent>";
}

// A member filled by a whole rule that lays a record of its own: the write
// cannot ride onto the callee, and reading the call site instead names the
// member of whatever encloses it -- `stmts`, which `box` does not fill.
void a_member_written_by_a_record_of_its_own_is_named(const CaretDoor& door) {
  const World world(door);
  CHECK(member(world, "fn f() { ", "box") == "inner");
  CHECK(member(world, "fn f() { ", "end") == "stmts");
}

// A rule reached through a chain of renames is entered at the call that names
// it, not at one that only reaches it: `fn` is appended to `fns`, whatever the
// repetition above it was entered through.
void the_call_that_names_a_rule_wins_over_one_that_reaches_it(
    const CaretDoor& door) {
  const World world(door);
  CHECK(member(world, "", "fn") == "fns");
}

// Past the token that opens a rule the record is already laid, and what
// follows fills nothing more: `)` closes a call, it does not append an
// argument to it.
void a_token_inside_a_rule_names_no_member(const CaretDoor& door) {
  const World world(door);
  CHECK(member(world, "fn f() { f(", "end") == "args");
  CHECK(member(world, "fn f() { f(", ")").empty());
  CHECK(member(world, "fn f() { ", "}").empty());
}

// Two constructs open at once is where the entry root collapses onto a record
// the repair invented: the caret must still answer from the last token read,
// not from the state that opens the file.
void a_caret_under_nested_open_constructs_keeps_its_state(
    const CaretDoor& door) {
  const World world(door);
  offered(*world.lang, "fn add(<|>", [](const lsp::CompletionList& list) {
    CHECK(offers(list, ")"));
    CHECK(!offers(list, "fn"));
  });
  offered(*world.lang, "fn f() { f(<|>", [](const lsp::CompletionList& list) {
    CHECK(offers(list, ")"));
    CHECK(!offers(list, "fn"));
  });
}

// A name reaching past ASCII is one token to the lexer, and an item replaces
// it whole: the anchor is the leaf, not a byte class stopping at `Ü`.
void a_name_outside_ascii_is_replaced_whole(const CaretDoor& door) {
  const World world(door);
  offered(*world.lang,
          "fn f() { alpha \xC3\x9C"
          "ber; see \xC3\x9C"
          "b<|> }",
          [](const lsp::CompletionList& list) {
            const auto at = std::ranges::find_if(
                list.items, [](const lsp::CompletionItem& i) {
                  return i.label ==
                         "\xC3\x9C"
                         "ber";
                });
            CHECK(at != list.items.end());
            if (at == list.items.end() || !at->textEdit.has_value()) return;
            const auto& edit = std::get<lsp::InsertReplaceEdit>(*at->textEdit);
            // `Ü` is one UTF-16 unit: the word starts at column 25.
            CHECK(edit.replace.start.character == 25);
            CHECK(edit.replace.end.character == 27);
            CHECK(edit.insert.start.character == 25);
            CHECK(edit.insert.end.character == 27);
          });
}

}  // namespace

void run_caret_lane(const CaretDoor& door) {
  a_new_reference_uses_its_own_type_before_an_ancestors(door);
  a_name_outside_ascii_is_replaced_whole(door);
  a_named_keyword_is_offered_by_its_bytes(door);
  a_terminal_does_not_answer_to_its_own_name(door);
  a_named_literal_beats_a_named_terminal_at_equal_length(door);
  a_k2_cell_offers_both_arms(door);
  a_narrowed_site_offers_its_subtype_alone(door);
  a_member_two_classes_spell_answers_for_its_own_site(door);
  a_site_that_narrows_nothing_offers_the_whole_member(door);
  a_caret_under_nested_open_constructs_keeps_its_state(door);
  a_member_written_by_a_record_of_its_own_is_named(door);
  the_call_that_names_a_rule_wins_over_one_that_reaches_it(door);
  a_token_inside_a_rule_names_no_member(door);
}
