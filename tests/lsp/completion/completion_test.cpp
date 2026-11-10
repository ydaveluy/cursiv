#include <lsp/types.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "block/generated/Block/Module.hpp"
#include "caret/generated/Caret/AstReflection.hpp"
#include "caret/generated/Caret/Module.hpp"
#include "caret/generated/Caret/Table.hpp"
#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/references/DefaultScopeProvider.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/completion/DefaultCompletionProvider.hpp"
#include "cursiv/completion/DefaultFuzzyMatcher.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/ExpectedCompletion.hpp"
#include "lsp/completion/CaretLane.hpp"
#include "support/OffersEveryKeyword.hpp"
#include "support/Editor.hpp"
#include "support/WorkspaceWorld.hpp"
#include "wordsll/generated/Ll/Module.hpp"
#include "wordslr/generated/Lr/Module.hpp"

namespace default_completion_provider {

namespace {

using support::offered;
using support::offers;
using World = support::LspWorkspace;

// A name attached to the caret is still being typed: not even `{`, which would
// not join it, is offered.
void a_caret_attached_to_a_name_is_offered_nothing() {
  const World world(support::offer_every_keyword);
  offered(*world.ws, "entity E<|>", [](const lsp::CompletionList& list) {
    CHECK(!offers(list, "{"));
    CHECK(!offers(list, "extends"));
  });
}

void a_caret_past_the_space_is_offered_the_word() {
  const World world(support::offer_every_keyword);
  offered(*world.ws, "entity E <|>", [](const lsp::CompletionList& list) {
    CHECK(offers(list, "extends"));
    CHECK(offers(list, "{"));
  });
}

struct PropertyScope final : cursiv::DefaultScopeProvider {
  using DefaultScopeProvider::DefaultScopeProvider;
  using DefaultScopeProvider::visitScopeEntries;
  mutable bool asked = false;

  bool visitScopeEntries(
      const cursiv::ReferenceInfo& context,
      cursiv::function_ref<bool(const cursiv::AstNodeDescription&)> visitor)
      const override {
    asked = true;
    CHECK(context.property == "superType");
    return DefaultScopeProvider::visitScopeEntries(context, visitor);
  }
};

void a_reference_context_names_the_member_being_completed() {
  const World world;
  auto scope = std::make_unique<PropertyScope>(*world.ws);
  const PropertyScope& observed = *scope;
  world.ws->scopeProvider = std::move(scope);
  offered(*world.ws, "entity Base {} entity E extends <|>",
          [](const lsp::CompletionList& list) { CHECK(offers(list, "Base")); });
  CHECK(observed.asked);
}

}  // namespace

void run() {
  a_caret_attached_to_a_name_is_offered_nothing();
  a_caret_past_the_space_is_offered_the_word();
  a_reference_context_names_the_member_being_completed();
}

}  // namespace default_completion_provider

namespace fuzzy_matcher {

namespace {

void a_query_anchors_on_a_word_boundary() {
  const cursiv::DefaultFuzzyMatcher matcher;

  CHECK(matcher.match("grammar", "Grammar"));    // whole, case folded
  CHECK(matcher.match("gr", "Grammar"));         // prefix
  CHECK(matcher.match("gmr", "Grammar"));        // subsequence from the start
  CHECK(matcher.match("GK", "GrammarKeyword"));  // camel case
  CHECK(matcher.match("dsp", "documentSymbolProvider"));
  CHECK(matcher.match("", "Grammar"));  // nothing typed answers all

  // `r`, `a`, `m` appear in order in `Grammar`, but none starts a word: a plain
  // subsequence match would let almost any query match almost any candidate.
  CHECK(!matcher.match("ram", "Grammar"));
  CHECK(!matcher.match("xyz", "Grammar"));
}

void an_underscore_opens_a_word() {
  const cursiv::DefaultFuzzyMatcher matcher;
  CHECK(matcher.match("sp", "symbol_provider"));
  CHECK(matcher.match("p", "symbol_provider"));
  CHECK(!matcher.match("rov", "symbol_provider"));
}

void unicode_characters_are_not_matched_in_separate_byte_fragments() {
  const cursiv::DefaultFuzzyMatcher matcher;
  CHECK(matcher.match("é", "éclair"));
  CHECK(matcher.match("é", "nom_éclair"));
  CHECK(!matcher.match("é", "Ã©"));
  CHECK(!matcher.match("é", "nom_Ã©"));
  CHECK(!matcher.match("😀", "😁À"));
  CHECK(matcher.match("😀", "😀"));
  // Matching works on UTF-16 units, so a character outside the BMP may match
  // across two characters.
  CHECK(matcher.match("😀", "\U0001F601\U0001FA00"));
  CHECK(!matcher.match("aA", "aŁ"));
}

}  // namespace

void run() {
  a_query_anchors_on_a_word_boundary();
  an_underscore_opens_a_word();
  unicode_characters_are_not_matched_in_separate_byte_fragments();
}

}  // namespace fuzzy_matcher

// A repair near the caret must not empty or widen what completion offers.
namespace caret_repaired {

namespace g = tcaret::generated::Caret;

namespace {

struct World : support::World<cursiv::SharedLspServices> {
  cursiv::LspServices* lang = &support::add_language<cursiv::LspServices>(
      shared, {"caret", {".caret"}, {}}, g::create_parser(),
      g::create_ast_reflection());
};

}  // namespace


namespace {

// The repair skips `emd`, so the leaf before the caret names the wrong lexer
// mode; reading the caret in that mode would reject every candidate.
void a_repair_behind_the_caret_does_not_silence_it(const World& world) {
  for (const char* marked : {"fn f() { emd; }<|>", "fn f() { end; }<|>",
                             "fn f() { emd; } <|>", "fn f() { emd; }<|> "}) {
    bool offered = false;
    (void)cursiv::test::expectCompletion(
        *world.lang, cursiv::test::ExpectedCompletionCallback{
                         .text = marked,
                         .assertion =
                             [&](const lsp::CompletionList& list) {
                               for (const lsp::CompletionItem& it : list.items)
                                 offered = offered || it.label == "fn";
                             },
                         .disposeAfterCheck = true});
    CHECK(offered);
  }
}

// After `@~`, `DeclR` and `UseR` are both still possible. A repair nearby may
// drop one of them, never add another feature.
void a_repair_beside_a_two_token_cell_offers_nothing_new() {
  const std::unique_ptr<cursiv::Parser> engine = g::create_parser();
  const auto after_tilde = [&engine](const std::string& text) {
    const cursiv::ParseResult r = engine->parse(text);
    const auto at = static_cast<cursiv::TextOffset>(text.find('~') + 1);
    std::vector<cursiv::NextFeature> out;
    engine->nextFeaturesAt(r.cst(), at, at, out);
    std::vector<cursiv::ElementId> ids;
    for (const cursiv::NextFeature& f : out) ids.push_back(f.feature);
    std::ranges::sort(ids);
    return ids;
  };
  const std::vector<cursiv::ElementId> clean =
      after_tilde("fn f() { end; @~!; }");
  CHECK(clean.size() == 2);
  for (const char* broken :
       {"fn f() { emd; @~!; }", "fn f() { emd; @~?; }", "fn f() { end; @~!, }",
        "fn f() { end; @~; }", "fn f() { @~!; "}) {
    const std::vector<cursiv::ElementId> got = after_tilde(broken);
    if (!std::ranges::includes(clean, got)) {
      std::printf(
          "FAIL %s: the caret offers a feature the clean reading"
          " does not\n",
          broken);
      ++failures;
    }
  }
}

// Replaying an unwind copies an ancestor frame; at these sizes the copy grows
// the frame arena, and walking that ancestor's parents must still offer `fn`.
void an_unwind_keeps_its_parents_when_the_arena_grows(const World& world) {
  for (unsigned count = 0; count < 8; ++count) {
    std::string text = "fn f() { ";
    for (unsigned i = 0; i < count; ++i) text += "end; ";
    text += "alpha }<|> fn g() { end; }";
    bool offered = false;
    (void)cursiv::test::expectCompletion(
        *world.lang, cursiv::test::ExpectedCompletionCallback{
                         .text = text,
                         .assertion = [&](const lsp::CompletionList& list) {
                           for (const auto& item : list.items)
                             offered = offered || item.label == "fn";
                         },
                         .disposeAfterCheck = true});
    CHECK(offered);
  }
}

}  // namespace

void run() {
  const World world;
  a_repair_behind_the_caret_does_not_silence_it(world);
  a_repair_beside_a_two_token_cell_offers_nothing_new();
  an_unwind_keeps_its_parents_when_the_arena_grows(world);
}

}  // namespace caret_repaired

// The caret tests of `CaretLane.cpp` on the LR engine.
namespace lr_completion_caret {

namespace g = tcaret::generated::Caret;

// The table must look two tokens ahead after `@~`, or
// `a_two_token_cell_offers_both_arms` would pass without testing anything.
static_assert(g::kTable.npeeks != 0);

void run() {
  run_caret_lane(CaretEngine{&g::create_parser, &g::create_ast_reflection});
}

}  // namespace lr_completion_caret

// A word may span lines, while the protocol keeps both ranges of a completion
// edit on one line: each stops at the caret's line.
namespace multiline_word {

namespace g = tblock::generated::Block;

namespace {

struct World : support::World<cursiv::SharedLspServices> {
  cursiv::LspServices* lang = &support::add_language<cursiv::LspServices>(
      shared, {"block", {".block"}, {}}, g::create_parser(),
      g::create_ast_reflection());

  World() { support::take_both_ranges(shared); }
};

// The ranges offered for `entity` at the marked caret, as
// `{insert start, insert end, replace start, replace end}`, line and column
// each.
[[nodiscard]] std::vector<lsp::Uint> ranges_of_entity(const World& world,
                                                      std::string text) {
  std::vector<lsp::Uint> found;
  (void)cursiv::test::expectCompletion(
      *world.lang,
      cursiv::test::ExpectedCompletionCallback{
          .text = std::move(text),
          .assertion =
              [&](const lsp::CompletionList& list) {
                for (const lsp::CompletionItem& item : list.items) {
                  if (item.label != "entity" || !item.textEdit) continue;
                  const auto* edit =
                      std::get_if<lsp::InsertReplaceEdit>(&*item.textEdit);
                  if (edit == nullptr) continue;
                  found = {
                      edit->insert.start.line,  edit->insert.start.character,
                      edit->insert.end.line,    edit->insert.end.character,
                      edit->replace.start.line, edit->replace.start.character,
                      edit->replace.end.line,   edit->replace.end.character};
                }
              },
          .disposeAfterCheck = true});
  return found;
}

// `begin\nend` is one token: the ranges cover only the caret's line of it. At
// its start, `entity` would run into it, so nothing is offered.
void a_word_spanning_lines_is_replaced_on_the_carets_line() {
  const World world;
  CHECK(ranges_of_entity(world, "<|>begin\nend").empty());
  CHECK(ranges_of_entity(world, "begin\nen<|>d") ==
        std::vector<lsp::Uint>{1, 0, 1, 2, 1, 0, 1, 3});
}

void a_word_on_one_line_keeps_its_ranges() {
  const World world;
  CHECK(ranges_of_entity(world, "en<|>tity") ==
        std::vector<lsp::Uint>{0, 0, 0, 2, 0, 0, 0, 6});
}

}  // namespace

void run() {
  a_word_spanning_lines_is_replaced_on_the_carets_line();
  a_word_on_one_line_keeps_its_ranges();
}

}  // namespace multiline_word

namespace word_terminals {

namespace {

// Overrides each hook as a language would: a value the grammar cannot name, a
// candidate filtered out, an item detail, and every way of reading the caret
// tried.
struct Hooked final : cursiv::DefaultCompletionProvider {
  using DefaultCompletionProvider::DefaultCompletionProvider;
  void completionFor(const cursiv::CompletionContext& context,
                     const cursiv::NextFeature& next,
                     cursiv::CompletionAcceptor accept) const override {
    if (next.property == "size" && context.node() != nullptr) {
      cursiv::CompletionValueItem item;
      item.label = "huge";
      (void)accept(context, std::move(item));
      return;
    }
    DefaultCompletionProvider::completionFor(context, next, accept);
  }
  void visitReferenceCandidates(
      const cursiv::ReferenceInfo& reference,
      const cursiv::CompletionContext& context,
      cursiv::function_ref<bool(const cursiv::AstNodeDescription&)> visitor)
      const override {
    DefaultCompletionProvider::visitReferenceCandidates(
        reference, context, [&](const cursiv::AstNodeDescription& candidate) {
          return candidate.name == "Z" || visitor(candidate);
        });
  }
  cursiv::CompletionValueItem createReferenceCompletionItem(
      const cursiv::AstNodeDescription& candidate,
      const cursiv::ReferenceInfo& reference,
      const cursiv::CompletionContext& context) const override {
    cursiv::CompletionValueItem item = DefaultCompletionProvider::
        createReferenceCompletionItem(candidate, reference, context);
    item.detail = "target";
    return item;
  }
  bool continueCompletion(
      const std::vector<lsp::CompletionItem>&) const override {
    return true;
  }
};

// Records what the hooks see: the node of each keyword, the first token's
// span, and the reference's container.
struct SeesNode final : cursiv::DefaultCompletionProvider {
  using DefaultCompletionProvider::DefaultCompletionProvider;
  mutable std::vector<std::string> nodes;
  mutable std::optional<std::pair<unsigned, unsigned>> span;
  mutable std::string container;
  void visitReferenceCandidates(
      const cursiv::ReferenceInfo& reference,
      const cursiv::CompletionContext& context,
      cursiv::function_ref<bool(const cursiv::AstNodeDescription&)> visitor)
      const override {
    const cursiv::AstNode* const node = reference.container;
    container = std::string(node->type->name);
    container += node->cstNodeId == cursiv::kNoNode ? "+new" : "+read";
    if (node->parent != nullptr) {
      container += " in ";
      container += node->parent->type->name;
    }
    DefaultCompletionProvider::visitReferenceCandidates(reference, context,
                                                        visitor);
  }
  void completionFor(const cursiv::CompletionContext& context,
                     const cursiv::NextFeature& next,
                     cursiv::CompletionAcceptor accept) const override {
    if (!span.has_value()) span = {context.tokenOffset, context.tokenEndOffset};
    DefaultCompletionProvider::completionFor(context, next, accept);
  }
  bool filterKeyword(const cursiv::CompletionContext& context,
                     std::string_view) const override {
    const cursiv::AstNode* const node = context.node();
    nodes.emplace_back(node != nullptr ? std::string(node->type->name) : "none");
    return true;
  }
};

struct World : support::World<cursiv::SharedLspServices> {
  const cursiv::LspServices* ll = &support::add_language<cursiv::LspServices>(
      shared, {"words-ll", {".wll"}, {}},
      twords::generated::Ll::create_parser(),
      twords::generated::Ll::create_ast_reflection());
  const cursiv::LspServices* lr = &support::add_language<cursiv::LspServices>(
      shared, {"words-lr", {".wlr"}, {}},
      twords::generated::Lr::create_parser(),
      twords::generated::Lr::create_ast_reflection());
  const cursiv::LspServices* hooked =
      &support::add_language<cursiv::LspServices>(
          shared, {"words-hooked", {".whk"}, {}},
          twords::generated::Lr::create_parser(),
          twords::generated::Lr::create_ast_reflection(),
          [](cursiv::LspServices& language) {
            language.completionProvider = std::make_unique<Hooked>(language);
          });
  const SeesNode* sees = nullptr;
  const cursiv::LspServices* seeing =
      &support::add_language<cursiv::LspServices>(
          shared, {"words-seeing", {".wse"}, {}},
          twords::generated::Lr::create_parser(),
          twords::generated::Lr::create_ast_reflection(),
          [this](cursiv::LspServices& language) {
            auto provider = std::make_unique<SeesNode>(language);
            sees = provider.get();
            language.completionProvider = std::move(provider);
          });
  const SeesNode* seesLl = nullptr;
  const cursiv::LspServices* seeingLl =
      &support::add_language<cursiv::LspServices>(
          shared, {"words-seeing-ll", {".wsl"}, {}},
          twords::generated::Ll::create_parser(),
          twords::generated::Ll::create_ast_reflection(),
          [this](cursiv::LspServices& language) {
            auto provider = std::make_unique<SeesNode>(language);
            seesLl = provider.get();
            language.completionProvider = std::move(provider);
          });
};

// Each item as an editor reads it -- what it shows, how it sorts, what it
// writes and where -- in label order.
std::vector<std::string> items_at(const cursiv::LspServices& language,
                                  std::string text) {
  std::vector<std::string> out;
  (void)cursiv::test::expectCompletion(
      language,
      cursiv::test::ExpectedCompletionCallback{
          .text = std::move(text),
          .assertion =
              [&](const lsp::CompletionList& list) {
                for (const lsp::CompletionItem& item : list.items) {
                  std::string shown = item.label;
                  shown +=
                      '|' +
                      std::to_string(
                          item.kind ? static_cast<unsigned>(*item.kind) : 0u);
                  shown += '|' + item.detail.value_or("");
                  shown += '|' + item.sortText.value_or("");
                  shown += item.documentation ? "|doc" : "|";
                  if (item.textEdit)
                    if (const auto* edit =
                            std::get_if<lsp::TextEdit>(&*item.textEdit))
                      shown += '|' + edit->newText + '@' +
                               std::to_string(edit->range.start.line) + ':' +
                               std::to_string(edit->range.start.character) +
                               '-' + std::to_string(edit->range.end.line) +
                               ':' + std::to_string(edit->range.end.character);
                  out.push_back(std::move(shown));
                }
              },
          .disposeAfterCheck = true});
  std::ranges::sort(out);
  return out;
}

std::vector<std::string> labels_at(const cursiv::LspServices& language,
                                   std::string text) {
  std::vector<std::string> out = items_at(language, std::move(text));
  for (std::string& item : out) item.resize(item.find('|'));
  std::ranges::sort(out);
  return out;
}

// Opening the rule that writes a reference, a scope sees a new node of its
// class under the node at the caret, even one of that class. Past a token of
// the rule, it sees the node read.
void a_scope_sees_the_node_of_the_rule_entered() {
  const World world;
  const std::pair<const cursiv::LspServices*, const SeesNode*> engines[] = {
      {world.seeing, world.sees}, {world.seeingLl, world.seesLl}};
  for (const auto& [language, sees] : engines) {
    sees->container.clear();
    (void)items_at(*language, "new:\n<|>");
    CHECK(sees->container == "Item+new in Items");
    sees->container.clear();
    (void)items_at(*language, "new:\na; <|>");
    CHECK(sees->container == "Item+new in Item");
    sees->container.clear();
    (void)items_at(*language, "cap:\nz: <|>");
    CHECK(sees->container == "Item+read in Items");
  }
}

// A data rule, or a terminal whose alternatives each convert, offers its words
// filtered on what is typed, but none attached to a name or ending a comment.
// Any other terminal offers nothing.
void a_data_rule_offers_its_words_and_a_terminal_none() {
  const World world;
  using Labels = std::vector<std::string>;
  for (const cursiv::LspServices* language : {world.ll, world.lr}) {
    for (const std::string form : {"rule:\n", "branch:\n"}) {
      CHECK(labels_at(*language, form + "a <|>") == (Labels{"FALSE", "TRUE"}));
      CHECK(labels_at(*language, form + "a t<|>") == Labels{"TRUE"});
      CHECK(labels_at(*language, form + "a<|> TRUE;").empty());
      CHECK(labels_at(*language, form + "a // c<|>").empty());
      CHECK(labels_at(*language, form + "<|>").empty());
    }
    for (const std::string form : {"whole:\n", "size:\n"}) {
      CHECK(labels_at(*language, form + "a <|>").empty());
      CHECK(labels_at(*language, form + "a t<|>").empty());
    }
  }
}

// Reference names stop at 1 000, and the keyword `none` is still offered.
void the_cap_on_names_leaves_the_keywords() {
  const World world;
  std::string text = "cap:\n";
  for (unsigned i = 0; i < 1100; ++i)
    text += "n" + std::to_string(i) + ": none;\n";
  for (const cursiv::LspServices* language : {world.ll, world.lr}) {
    const std::vector<std::string> labels =
        labels_at(*language, text + "z: <|>");
    CHECK(labels.size() == 1001);
    CHECK(std::ranges::find(labels, "none") != labels.end());
  }
}

// A caret inside a data rule is completed from the rule's start; past a
// comment inside it, nothing matches.
void a_data_rule_is_completed_from_its_start() {
  const World world;
  using Labels = std::vector<std::string>;
  const std::string names =
      "path:\nJohn.Miller -> Z;\nJohn.Smith.Junior -> Z;\n"
      "John.Smith.Senior -> Z;\nZ -> ";
  const Labels john{"John.Miller", "John.Smith.Junior", "John.Smith.Senior"};
  const Labels smith{"John.Smith.Junior", "John.Smith.Senior"};
  for (const cursiv::LspServices* language : {world.ll, world.lr}) {
    Labels all = john;
    all.emplace_back("Z");
    CHECK(labels_at(*language, names + "<|>John.Smith.Junior;") == all);
    CHECK(labels_at(*language, names + "John<|>.Smith.Junior;") == john);
    CHECK(labels_at(*language, names + "John.Smi<|>th.Junior;") == smith);
    CHECK(labels_at(*language, names + "John.Smith.Jun<|>ior;") ==
          Labels{"John.Smith.Junior"});
    CHECK(labels_at(*language, names + "John.<|>") == john);
    CHECK(labels_at(*language, names + "<|>John.// c\nMiller;") == all);
    CHECK(labels_at(*language, names + "John.// c\n<|>Miller;").empty());
  }
}

// A data rule of one token is still read whole: a caret anywhere in it
// replaces it all. At the start of a plain terminal, the caret replaces
// nothing.
void a_data_rule_of_one_token_is_read_whole() {
  const World world;
  using Span = std::pair<unsigned, unsigned>;
  const std::pair<const cursiv::LspServices*, const SeesNode*> engines[] = {
      {world.seeing, world.sees}, {world.seeingLl, world.seesLl}};
  for (const auto& [language, sees] : engines) {
    const auto first_span = [&, sees = sees, language = language](std::string text) {
      sees->span.reset();
      (void)items_at(*language, std::move(text));
      return sees->span.value_or(Span{0, 0});
    };
    //                       0123456 7890123456
    const std::string path = "path:\nZ -> ";
    CHECK(first_span(path + "<|>John;") == Span(11, 15));
    CHECK(first_span(path + "Jo<|>hn;") == Span(11, 15));
    CHECK(first_span(path + "John<|>;") == Span(11, 15));
    CHECK(first_span(path + "<|>John.Smith;") == Span(11, 21));
    CHECK(first_span("size:\n<|>abc s;") == Span(6, 6));
    CHECK(labels_at(*language, "rule:\na <|>TRUE;") ==
          (std::vector<std::string>{"FALSE", "TRUE"}));
  }
}

// The node of a context is that of the last token begun before the caret, and
// none where no token stands: nothing else tells a hook it is at the very
// start.
void a_context_names_the_node_of_its_token() {
  const World world;
  const auto nodes_at = [&](std::string text) {
    world.sees->nodes.clear();
    (void)items_at(*world.seeing, std::move(text));
    return world.sees->nodes;
  };
  const std::vector<std::string> atStart = nodes_at("<|>");
  CHECK(!atStart.empty());
  for (const std::string& node : atStart) CHECK(node == "none");
  const std::vector<std::string> behind = nodes_at("path:\na <|>");
  CHECK(!behind.empty());
  for (const std::string& node : behind) CHECK(node == "Item");
}

// A language overrides what one feature offers, which candidates a reference
// has and their form, and whether the next way of reading the caret is tried.
void the_hooks_shape_what_is_offered() {
  const World world;
  using Labels = std::vector<std::string>;
  CHECK(labels_at(*world.hooked, "size:\na <|>") == Labels{"huge"});
  CHECK(labels_at(*world.hooked, "size:\na h<|>") == Labels{"huge"});
  const std::string names = "path:\nJohn.Miller -> Z;\nZ -> ";
  const std::vector<std::string> items =
      items_at(*world.hooked, names + "<|>");
  CHECK(items.size() == 1);
  CHECK(items.front().starts_with("John.Miller|") &&
        items.front().find("|target|") != std::string::npos);
  CHECK(labels_at(*world.hooked, names + "John.<|>") ==
        (Labels{"John.Miller", "in"}));
  CHECK(labels_at(*world.lr, names + "John.<|>") == Labels{"John.Miller"});
}

}  // namespace

void run() {
  a_data_rule_offers_its_words_and_a_terminal_none();
  the_cap_on_names_leaves_the_keywords();
  a_scope_sees_the_node_of_the_rule_entered();
  a_data_rule_is_completed_from_its_start();
  a_data_rule_of_one_token_is_read_whole();
  the_hooks_shape_what_is_offered();
  a_context_names_the_node_of_its_token();
}

}  // namespace word_terminals

int main() {
  cursiv::test::count_expectations();
  default_completion_provider::run();
  fuzzy_matcher::run();
  caret_repaired::run();
  lr_completion_caret::run();
  multiline_word::run();
  word_terminals::run();
  return cursiv::test::report("completion");
}
