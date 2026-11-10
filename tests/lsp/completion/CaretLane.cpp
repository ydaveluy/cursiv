#include "lsp/completion/CaretLane.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/Check.hpp"
#include "lsp/completion/CaretGrammar.hpp"
#include "support/OffersEveryKeyword.hpp"
#include "support/Editor.hpp"
#include "support/World.hpp"

namespace {

namespace ast = tcaret::ast;

using support::offered;
using support::offers;

struct World : support::World<cursiv::SharedLspServices> {
  cursiv::LspServices* lang;

  explicit World(const CaretEngine& engine)
      : lang(&support::add_language<cursiv::LspServices>(
            shared, {"caret", {".caret"}, {}}, engine.parser(),
            engine.reflection(), support::offer_every_keyword)) {
    support::take_both_ranges(shared);
  }
};

// `KW_END` is a terminal that holds `kw<"end">`.
void a_named_keyword_is_offered_by_its_bytes(const CaretEngine& engine) {
  const World world(engine);
  offered(*world.lang, "fn f() { e<|> }", [](const lsp::CompletionList& list) {
    CHECK(offers(list, "end"));
    CHECK(!offers(list, "KW_END"));
    CHECK(!offers(list, "ID"));
  });
}

void a_terminal_does_not_answer_to_its_own_name(const CaretEngine& engine) {
  const std::unique_ptr<cursiv::Parser> parser = engine.parser();
  const auto takes = [&](std::string_view text) {
    const cursiv::ParseResult r = parser->parse(text);
    return r.cst().accepted() && !r.cst().recovered();
  };
  CHECK(takes("fn f() { end; }"));
  CHECK(!takes("fn f() { KW_END; }"));
  CHECK(!takes("fn f() { end; IKW_LET; }"));
}

// `ID` is declared before `KW_END` and `IKW_LET`: the order does not decide
// the tie.
void a_named_literal_beats_a_named_terminal_at_equal_length(
    const CaretEngine& engine) {
  const std::unique_ptr<cursiv::Parser> parser = engine.parser();
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

// After `@~`, `DeclR` and `UseR` differ only on the next token, where the caret
// is: both are offered.
void a_two_token_cell_offers_both_arms(const CaretEngine& engine) {
  const World world(engine);
  offered(*world.lang, "fn f() { @~<|>!; }",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "!"));
            CHECK(offers(list, "?"));
          });
}

// `see` takes only an `Alpha`, though `Link::to` accepts any `Sym`: the
// reference is not written yet, so only the grammar can narrow it.
void a_narrowed_site_offers_its_subtype_alone(const CaretEngine& engine) {
  const World world(engine);
  offered(*world.lang, "fn f() { alpha a; beta b; see <|> }",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "a"));
            CHECK(!offers(list, "b"));
          });
}

// `Link` and `Bond` both have a `to` member: the class the rule writes decides
// the reference, here `Bond::to`, which takes a `Beta`.
void a_member_two_classes_spell_answers_for_its_own_site(
    const CaretEngine& engine) {
  const World world(engine);
  offered(*world.lang, "fn f() { alpha a; beta b; tie <|> }",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "b"));
            CHECK(!offers(list, "a"));
          });
}

void a_new_reference_uses_its_own_type_before_an_ancestors(
    const CaretEngine& engine) {
  const World world(engine);
  offered(*world.lang, "fn f() { alpha a; beta b; nest a { tie <|> } }",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "b"));
            CHECK(!offers(list, "a"));
          });
}

void a_site_that_narrows_nothing_offers_the_whole_member(
    const CaretEngine& engine) {
  const World world(engine);
  offered(*world.lang, "fn f() { alpha a; beta b; any <|> }",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "a"));
            CHECK(offers(list, "b"));
          });
}

// `TICK` writes a reference in `TickR`, but after `bare` it writes nothing.
void a_bare_terminal_offers_no_reference(const CaretEngine& engine) {
  const World world(engine);
  offered(*world.lang, "fn f() { alpha a; beta b; bare <|> }",
          [](const lsp::CompletionList& list) {
            CHECK(!offers(list, "a"));
            CHECK(!offers(list, "b"));
          });
}

// 40 000 nested levels of a data rule around the caret: completion time must
// stay linear in the depth.
void a_deep_data_rule_costs_its_depth(const CaretEngine& engine) {
  const World world(engine);
  const std::size_t depth = 40000;
  const std::string text = "fn f() { alpha a; deep " +
                           std::string(depth, '[') + "<|>x" +
                           std::string(depth, ']') + "; }";
  const auto begin = std::chrono::steady_clock::now();
  offered(*world.lang, text, [](const lsp::CompletionList& list) {
    CHECK(list.isIncomplete);
  });
  CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(8));
}

// The member the token `spelling` would fill at the end of `text`; a
// `CompletionList` does not carry it.
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

// The members an `ID` would fill at the end of `text`, sorted.
[[nodiscard]] std::vector<std::string_view> identifier_members(
    const World& world, std::string_view text) {
  const cursiv::Parser& parser = *world.lang->parser;
  const std::string source{text};
  const cursiv::ParseResult result = parser.parse(source);
  const auto at = static_cast<cursiv::TextOffset>(source.size());
  std::vector<cursiv::NextFeature> out;
  parser.nextFeaturesAt(result.cst(), at, at, out);
  std::vector<std::string_view> members;
  for (const cursiv::NextFeature& f : out)
    if (parser.getGrammar().terminalName(f.feature) == "ID")
      members.push_back(f.property);
  std::ranges::sort(members);
  return members;
}

// After `either`, `dotted` or `opens`, the next `ID` may fill `alpha` or
// `beta`: completion offers the targets of both.
void readings_of_one_token_each_name_their_member(const CaretEngine& engine) {
  const World world(engine);
  const std::vector<std::string_view> both{"alpha", "beta"};
  for (const std::string_view head : {"either", "dotted", "opens"}) {
    const std::string text =
        "fn f() { alpha a; beta b; " + std::string(head) + " ";
    CHECK(identifier_members(world, text) == both);
    offered(*world.lang, text + "<|> }", [](const lsp::CompletionList& list) {
      CHECK(offers(list, "a"));
      CHECK(offers(list, "b"));
    });
  }
}

// `ModelR` and `AssemblyR` share the prefix `x :`, then each fill their own
// member through data rules.
void rules_sharing_a_prefix_each_name_their_member(const CaretEngine& engine) {
  const World world(engine);
  const std::string text = "fn f() { alpha a; beta b; child x : ";
  CHECK(identifier_members(world, text) ==
        (std::vector<std::string_view>{"alpha", "beta"}));
  offered(*world.lang, text + "<|> }", [](const lsp::CompletionList& list) {
    CHECK(offers(list, "a"));
    CHECK(offers(list, "b"));
  });
}

// After `maybe`, one branch writes `alpha` and the other writes nothing.
void a_reading_naming_nothing_leaves_the_member(const CaretEngine& engine) {
  const World world(engine);
  const std::string text = "fn f() { alpha a; beta b; maybe ";
  CHECK(identifier_members(world, text) ==
        std::vector<std::string_view>{"alpha"});
  offered(*world.lang, text + "<|> }", [](const lsp::CompletionList& list) {
    CHECK(offers(list, "a"));
    CHECK(!offers(list, "b"));
  });
}

// `box` starts `BoxR`, which `HoldR` assigns whole to `inner`: the member is
// `inner`, not the enclosing `stmts`.
void a_member_written_by_a_record_of_its_own_is_named(
    const CaretEngine& engine) {
  const World world(engine);
  CHECK(member(world, "fn f() { ", "box") == "inner");
  CHECK(member(world, "fn f() { ", "end") == "stmts");
}

// `FnR` is appended to `fns` by the call that names it, whatever rules lead to
// that call.
void the_call_that_names_a_rule_wins_over_one_that_reaches_it(
    const CaretEngine& engine) {
  const World world(engine);
  CHECK(member(world, "", "fn") == "fns");
}

// Inside a rule already started, a token that is not a member fills nothing:
// `)` closes a call, it does not append an argument.
void a_token_inside_a_rule_names_no_member(const CaretEngine& engine) {
  const World world(engine);
  CHECK(member(world, "fn f() { f(", "end") == "args");
  CHECK(member(world, "fn f() { f(", ")").empty());
  CHECK(member(world, "fn f() { ", "}").empty());
}

// With two constructs left open, completion answers from the last token read,
// not from the start of the file.
void a_caret_under_nested_open_constructs_keeps_its_state(
    const CaretEngine& engine) {
  const World world(engine);
  offered(*world.lang, "fn add(<|>", [](const lsp::CompletionList& list) {
    CHECK(offers(list, ")"));
    CHECK(!offers(list, "fn"));
  });
  offered(*world.lang, "fn f() { f(<|>", [](const lsp::CompletionList& list) {
    CHECK(offers(list, ")"));
    CHECK(!offers(list, "fn"));
  });
}

// A non-ASCII name is one token: the completion edit replaces it whole, not
// only up to `Ü`.
void a_name_outside_ascii_is_replaced_whole(const CaretEngine& engine) {
  const World world(engine);
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

void run_caret_lane(const CaretEngine& engine) {
  a_new_reference_uses_its_own_type_before_an_ancestors(engine);
  a_name_outside_ascii_is_replaced_whole(engine);
  a_named_keyword_is_offered_by_its_bytes(engine);
  a_terminal_does_not_answer_to_its_own_name(engine);
  a_named_literal_beats_a_named_terminal_at_equal_length(engine);
  a_two_token_cell_offers_both_arms(engine);
  a_narrowed_site_offers_its_subtype_alone(engine);
  a_member_two_classes_spell_answers_for_its_own_site(engine);
  a_site_that_narrows_nothing_offers_the_whole_member(engine);
  a_bare_terminal_offers_no_reference(engine);
  a_deep_data_rule_costs_its_depth(engine);
  a_caret_under_nested_open_constructs_keeps_its_state(engine);
  a_member_written_by_a_record_of_its_own_is_named(engine);
  the_call_that_names_a_rule_wins_over_one_that_reaches_it(engine);
  a_token_inside_a_rule_names_no_member(engine);
  readings_of_one_token_each_name_their_member(engine);
  rules_sharing_a_prefix_each_name_their_member(engine);
  a_reading_naming_nothing_leaves_the_member(engine);
}
