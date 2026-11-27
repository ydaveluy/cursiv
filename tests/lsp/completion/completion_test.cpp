#include <lsp/types.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "block/generated/Block/Module.hpp"
#include "caret/generated/Caret/Module.hpp"
#include "cursiv/engine/completion/Caret.hpp"
#include "cursiv/engine/parser/Explorer.hpp"
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
#include "cursiv/ExpectedHover.hpp"
#include "lsp/completion/CaretLane.hpp"
#include "support/OffersEveryKeyword.hpp"
#include "support/Editor.hpp"
#include "support/WorkspaceWorld.hpp"
#include "wordslr/generated/Suite/Module.hpp"
#include "wordslr/generated/Suite/Table.hpp"
#include "wordscycle/generated/Cyclic/Module.hpp"

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
      cursiv::FunctionRef<bool(const cursiv::AstNodeDescription&)> visitor)
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
      shared, {"caret", {".caret"}, {}}, g::createParser(),
      g::createAstReflection());
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
  const std::unique_ptr<cursiv::Parser> engine = g::createParser();
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

// A keyword typed with an escape is matched by the text the parser read.
void an_escaped_prefix_matches_the_decoded_keyword() {
  // `\\u` and four hex digits spell one ASCII letter.
  const auto escapes = [](std::string_view source,
                          cursiv::SourceTranslation& out) {
    for (auto at = source.find("\\u"); at != std::string_view::npos;
         at = source.find("\\u", at + 6))
      out.replace(at, at + 6,
                  std::string(1, static_cast<char>(std::stoi(
                                     std::string(source.substr(at + 2, 4)),
                                     nullptr, 16))));
  };
  struct Translated : support::World<cursiv::SharedLspServices> {
    cursiv::LspServices* lang = nullptr;
  } world;
  world.lang = &support::add_language<cursiv::LspServices>(
      world.shared, {"caret", {".caret"}, {}}, g::createParser(),
      g::createAstReflection(), [&](cursiv::LspServices& services) {
        services.parser =
            cursiv::translatingParser(std::move(services.parser), escapes);
      });
  bool offered = false;
  (void)cursiv::test::expectCompletion(
      *world.lang, cursiv::test::ExpectedCompletionCallback{
                       .text = "f\\u006e<|>",
                       .assertion =
                           [&](const lsp::CompletionList& list) {
                             for (const lsp::CompletionItem& it : list.items)
                               offered = offered || it.label == "fn";
                           },
                       .disposeAfterCheck = true});
  CHECK(offered);
}

}  // namespace

void run() {
  const World world;
  an_escaped_prefix_matches_the_decoded_keyword();
  a_repair_behind_the_caret_does_not_silence_it(world);
  a_repair_beside_a_two_token_cell_offers_nothing_new();
  an_unwind_keeps_its_parents_when_the_arena_grows(world);
}

}  // namespace caret_repaired

// The caret tests of `CaretLane.cpp` on the LR engine.
namespace lr_completion_caret {

namespace g = tcaret::generated::Caret;

void run() {
  run_caret_lane(CaretEngine{&g::createParser, &g::createAstReflection});
}

}  // namespace lr_completion_caret

// A word may span lines, while the protocol keeps both ranges of a completion
// edit on one line: each stops at the caret's line.
namespace multiline_word {

namespace g = tblock::generated::Block;

namespace {

struct World : support::World<cursiv::SharedLspServices> {
  cursiv::LspServices* lang = &support::add_language<cursiv::LspServices>(
      shared, {"block", {".block"}, {}}, g::createParser(),
      g::createAstReflection());

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
      cursiv::FunctionRef<bool(const cursiv::AstNodeDescription&)> visitor)
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
      cursiv::FunctionRef<bool(const cursiv::AstNodeDescription&)> visitor)
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
  const cursiv::LspServices* lr = &support::add_language<cursiv::LspServices>(
      shared, {"words-lr", {".wlr"}, {}},
      twords::generated::Suite::createParser(),
      twords::generated::Suite::createAstReflection());
  const cursiv::LspServices* hooked =
      &support::add_language<cursiv::LspServices>(
          shared, {"words-hooked", {".whk"}, {}},
          twords::generated::Suite::createParser(),
          twords::generated::Suite::createAstReflection(),
          [](cursiv::LspServices& language) {
            language.completionProvider = std::make_unique<Hooked>(language);
          });
  const SeesNode* sees = nullptr;
  const cursiv::LspServices* seeing =
      &support::add_language<cursiv::LspServices>(
          shared, {"words-seeing", {".wse"}, {}},
          twords::generated::Suite::createParser(),
          twords::generated::Suite::createAstReflection(),
          [this](cursiv::LspServices& language) {
            auto provider = std::make_unique<SeesNode>(language);
            sees = provider.get();
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
      {world.seeing, world.sees}};
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

// Word alternatives offer matching spellings at a value, including converted
// branches. Names and comments keep their own completions.
void a_data_rule_offers_its_words_and_a_terminal_none() {
  const World world;
  using Labels = std::vector<std::string>;
  for (const cursiv::LspServices* language : {world.lr}) {
    for (const std::string form : {"rule:\n", "branch:\n"}) {
      CHECK(labels_at(*language, form + "a <|>") == (Labels{"FALSE", "TRUE"}));
      CHECK(labels_at(*language, form + "a t<|>") == Labels{"TRUE"});
      CHECK(labels_at(*language, form + "a<|> TRUE;").empty());
      CHECK(labels_at(*language, form + "a // c<|>").empty());
      CHECK(labels_at(*language, form + "<|>").empty());
    }
    (void)cursiv::test::expectHover(*language, {.text = "branch:\na T<|>rue;",
                                                .hover = "Enables the feature.",
                                                .disposeAfterCheck = true});
    const auto documented = items_at(*language, "branch:\na t<|>");
    CHECK(documented.size() == 1 && documented.front().find("|doc|") != std::string::npos);
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
  for (const cursiv::LspServices* language : {world.lr}) {
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
  for (const cursiv::LspServices* language : {world.lr}) {
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
      {world.seeing, world.sees}};
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

void a_word_terminal_offers_its_property_if_any_spelling_fits() {
  const auto parser = twords::generated::Suite::createParser();
  constexpr std::string_view text = "masked:small:";
  const auto parsed = parser->parse(text);
  CHECK(parsed.accepted && parsed.diagnostics.empty());
  std::vector<cursiv::NextFeature> features;
  parser->nextFeaturesAt(parsed.cst(), 7, 12, features);
  CHECK(std::ranges::any_of(features, [](const auto& feature) { return feature.property == "size"; }));

  cursiv::CancellationTokenSource stop;
  stop.request_stop();
  try {
    parser->nextFeaturesAt(parsed.cst(), text.size(), text.size(), features, stop.get_token());
    CHECK(false);
  } catch (const cursiv::OperationCancelled&) {}
}

// A condition that reads the name with the token before it, or with the two
// tokens before it, asks for more letters than the spellings the lexer tells
// apart from the caret on.
void a_condition_may_ask_for_a_longer_spelling() {
  const auto parser = twords::generated::Suite::createParser();
  for (const std::string_view text : {"dotted:-.", "crossed:x-~"}) {
    const std::string whole = std::string(text) + "abcdefgh";
    const auto read = parser->parse(whole);
    CHECK(read.accepted && read.diagnostics.empty());
    const auto parsed = parser->parse(text);
    std::vector<cursiv::NextFeature> features;
    const auto at = static_cast<cursiv::TextOffset>(text.size());
    parser->nextFeaturesAt(parsed.cst(), at, at, features);
    CHECK(std::ranges::any_of(features, [](const auto& f) { return f.property == "name"; }));
  }
}

// The trivia before the caret stays trivia once the candidate is written: a
// space that `a` would make a token refuses it, a line end does not.
void a_candidate_keeps_the_trivia_before_it() {
  const World world;
  using Labels = std::vector<std::string>;
  CHECK(labels_at(*world.lr, "spaced:\n<|>") == Labels{"a"});
  CHECK(labels_at(*world.lr, "spaced: <|>").empty());
}

// The fastest of five completions at `at` of `text`.
std::chrono::steady_clock::duration fastest(const cursiv::Parser& parser, std::string_view text,
                                            cursiv::TextOffset at) {
  const auto parsed = parser.parse(text);
  auto best = std::chrono::steady_clock::duration::max();
  for (int i = 0; i < 5; ++i) {
    std::vector<cursiv::NextFeature> features;
    const auto begin = std::chrono::steady_clock::now();
    parser.nextFeaturesAt(parsed.cst(), at, at, features);
    best = std::min(best, std::chrono::steady_clock::now() - begin);
  }
  return best;
}

// The run before the caret reads on into every count after it: the search
// ends at the first byte instead of spelling each count.
void a_token_that_reads_on_into_the_spelling_ends_the_search() {
  const auto parser = twords::generated::Suite::createParser();
  CHECK(fastest(*parser, "chain:y", 7) < 50 * fastest(*parser, "chain: ", 7));
}

// EOF counts reject an incompatible suffix or one that fills the count.
// Sixteen times more DFA states must not square the completion work.
void eof_counts_share_their_suffix_search() {
  const auto parser = twords::generated::Suite::createParser();
  CHECK(fastest(*parser, "last:  ;", 6) < 64 * fastest(*parser, "few:  ;", 5));
  CHECK(fastest(*parser, "last:" + std::string(1024, 'w'), 5) <
        32 * fastest(*parser, "few:" + std::string(64, 'w'), 4));
  CHECK(fastest(*parser, "mixedlast:" + std::string(1024, 'w'), 10) <
        64 * fastest(*parser, "mixedfew:" + std::string(64, 'w'), 9));
}

void an_eof_completion_keeps_a_nonempty_return_to_the_start() {
  const auto parser = twords::generated::Cyclic::createParser();
  for (const std::string_view text : {"y", "wxy"}) {
    const auto parsed = parser->parse(text);
    CHECK(parsed.accepted && parsed.diagnostics.empty());
    std::vector<cursiv::NextFeature> features;
    parser->nextFeaturesAt(parsed.cst(), 0, 0, features);
    CHECK(std::ranges::any_of(features, [&](const auto& feature) {
      return parser->getGrammar().terminalName(feature.feature) == "WORD" &&
             feature.property == "name";
    }));
  }
}

void an_eof_guard_reads_the_retained_suffix() {
  const auto parser = twords::generated::Suite::createParser();
  const auto offered = [&](std::string_view text, cursiv::TextOffset at,
                           cursiv::TextOffset resume,
                           std::string_view terminal) {
    const auto parsed = parser->parse(text);
    CHECK(parsed.accepted && parsed.diagnostics.empty());
    std::vector<cursiv::NextFeature> features;
    parser->nextFeaturesAt(parsed.cst(), at, resume, features);
    return std::ranges::any_of(features, [&](const auto& feature) {
      return parser->getGrammar().terminalName(feature.feature) == terminal;
    });
  };
  CHECK(offered("guardend:#;", 9, 10, "END_GUARD"));
  CHECK(offered("guardend:w;", 9, 10, "END_GUARD"));
  CHECK(offered("guardend:ww;", 9, 10, "END_GUARD"));
  CHECK(offered("guardend:ww;", 9, 9, "END_GUARD"));
  CHECK(offered("guardpairs:#;;", 11, 12, "END_PAIRS"));
  CHECK(offered("guardpairs:wxwx;;", 11, 12, "END_PAIRS"));
  CHECK(offered("guardpairs:wxwx;;", 11, 11, "END_PAIRS"));
  CHECK(!offered("guardpairs:wx;;", 11, 14, "END_PAIRS"));
  CHECK(offered("guardutf8:#→", 10, 11, "END_UTF8"));
  CHECK(offered("guardutf8:éé→", 10, 11, "END_UTF8"));
  CHECK(offered("guardutf8:éé→", 10, 10, "END_UTF8"));
  CHECK(!offered("guardutf8:#→", 10, 13, "END_UTF8"));
  CHECK(offered("guardelect:\n#;;", 12, 13, "END_OTHER"));
  CHECK(offered("guardelect:\nwx;;", 12, 13, "END_OTHER"));
  CHECK(offered("guardelect:\nwx;;", 12, 14, "END_OTHER"));
  CHECK(offered("guardelect:\nwx;;", 12, 12, "END_OTHER"));
  CHECK(!offered("guardelect:\nwx;;", 12, 13, "END_PAIRS"));
  CHECK(!offered("guardelect: #;;", 12, 13, "END_OTHER"));
  CHECK(!offered("guardelect: #;;", 12, 13, "END_PAIRS"));
  CHECK(offered("last:ww", 5, 6, "LAST"));
  CHECK(offered("last:ww", 5, 5, "LAST"));
  CHECK(!offered("last:;", 5, 5, "LAST"));
  CHECK(offered("mixedlast:xw", 10, 11, "LAST_MIXED"));
  CHECK(offered("mixedfew:wx", 9, 9, "FEW_MIXED"));
  CHECK(!offered("mixedfew:;", 9, 9, "FEW_MIXED"));
  for (const auto& [prefix, terminal, size] :
       {std::tuple<std::string_view, std::string_view, unsigned>{"few:", "FEW",
                                                                64},
         {"last:", "LAST", 1024},
         {"mixedfew:", "FEW_MIXED", 64},
         {"mixedlast:", "LAST_MIXED", 1024}}) {
    const std::string text = std::string(prefix) + std::string(size, 'w');
    const auto at = static_cast<cursiv::TextOffset>(prefix.size());
    CHECK(!offered(text, at, at, terminal));
    CHECK(offered(text, at, at + 1, terminal));
  }
}

cursiv::engine::Exit (*unguardedExplore)(cursiv::engine::Explorer&);
std::uint16_t guardedTerminal = 0;
std::size_t acceptedLength = 0;
std::map<std::size_t, unsigned> testedLengths;

bool accepts_length(cursiv::engine::Machine& m, const char*, const char*,
                    cursiv::engine::Token tok, cursiv::engine::Indented) {
  const auto length = static_cast<std::size_t>(tok.end - m.src - tok.begin(m));
  ++testedLengths[length];
  return length == acceptedLength;
}

cursiv::engine::Exit with_length_guard(cursiv::engine::Explorer& x) {
  auto& m = x.text;
  if (m.ended == guardedTerminal && x.tok.terminal() == guardedTerminal &&
      x.tok.end == m.end && !m.check(accepts_length, x.at, x.at, x.tok, x.indented))
    return x.refused(x.tok);
  return unguardedExplore(x);
}

// A pending predicate can reject every spelling, or accept only the longest.
// Equal spellings from the two byte preferences ask it only once.
void eof_spellings_share_validation_without_losing_longer_candidates() {
  namespace g = twords::generated::Suite;
  const auto parser = g::createParser();
  auto tables = g::kTables;
  unguardedExplore = tables.explore;
  tables.explore = with_length_guard;
  const cursiv::engine::Language language{tables, g::kAst, g::kGrammar};
  for (const auto& [prefix, terminal, size] :
       {std::tuple<std::string_view, std::string_view, unsigned>{"few:", "FEW", 64},
        {"last:", "LAST", 1024}}) {
    for (std::uint16_t t = 0; t < tables.terminalCount; ++t)
      if (parser->getGrammar().terminalName(t) == terminal) guardedTerminal = t;
    const std::string text = std::string(prefix) + std::string(size / 2, 'w');
    const auto parsed = parser->parse(text);
    CHECK(parsed.accepted && parsed.diagnostics.empty());
    for (const auto length : {0u, size}) {
      acceptedLength = length;
      testedLengths.clear();
      std::vector<cursiv::NextFeature> features;
      const auto at = static_cast<cursiv::TextOffset>(prefix.size());
      cursiv::engine::next_features(language, parsed.cst(), at, at, features,
                                   cursiv::kNoCancel);
      CHECK(std::ranges::any_of(features, [](const auto& feature) {
        return feature.feature == guardedTerminal;
      }) == (length != 0));
      CHECK(testedLengths.size() == size / 2);
      for (const auto& [span, count] : testedLengths) {
        CHECK(span > size / 2 && span <= size);
        CHECK(count == 1);
      }
    }
  }
}

const cursiv::engine::RunScanner* originalScanners = nullptr;
const std::uint8_t* scannerSets = nullptr;
std::uint64_t scannedBytes = 0;

const char* counted_scan(const std::uint8_t* in, const char* from,
                         const char* end) {
  const auto next = originalScanners[(in - scannerSets) / 256](in, from, end);
  scannedBytes += static_cast<std::uint64_t>(next - from);
  return next;
}

void held_tokens_share_their_lexical_suffixes() {
  namespace g = twords::generated::Suite;
  const auto parser = g::createParser();
  auto tables = g::kTables;
  originalScanners = tables.runScanners;
  scannerSets = tables.runSets;
  std::vector<cursiv::engine::RunScanner> scanners;
  for (auto scan = originalScanners; *scan; ++scan)
    scanners.push_back(counted_scan);
  scanners.push_back(nullptr);
  tables.runScanners = scanners.data();
  const cursiv::engine::Language language{tables, g::kAst, g::kGrammar};
  const auto count = [&](unsigned size) {
    const std::string text = "crossing:" + std::string(size, 'a') + "x";
    const auto parsed = parser->parse(text);
    CHECK(parsed.accepted && parsed.diagnostics.empty());
    std::vector<cursiv::NextFeature> features;
    scannedBytes = 0;
    const auto at = static_cast<cursiv::TextOffset>(text.size() - 1);
    cursiv::engine::next_features(language, parsed.cst(), at, at + 1, features,
                                  cursiv::kNoCancel);
    CHECK(std::ranges::any_of(features, [](const auto& feature) {
      return feature.property == "name";
    }));
    return scannedBytes;
  };
  const auto small = count(256);
  const auto large = count(512);
  CHECK(large < 3 * small);
  CHECK(large < 256 * 512);
}

// A literal `a` read only before `;c` turns the name before the caret into it:
// `c` is refused after the name, and offered where the literal's path reads it.
void a_guard_before_the_caret_is_read_again_with_the_candidate() {
  const World world;
  using Labels = std::vector<std::string>;
  CHECK(labels_at(*world.lr, "guard:a;<|>") == Labels{"e"});
  CHECK(labels_at(*world.lr, "guard2:a;:<|>") == (Labels{"c", "e"}));
  const auto parser = twords::generated::Suite::createParser();
  constexpr std::string_view middle = "middle:!@~";
  constexpr std::string_view full = "middle:!@~qqq";
  const auto parsed = parser->parse(full);
  CHECK(parsed.accepted && parsed.diagnostics.empty());
  std::vector<cursiv::NextFeature> features;
  const auto prefix = parser->parse(middle);
  parser->nextFeaturesAt(prefix.cst(), middle.size(), middle.size(), features);
  CHECK(std::ranges::any_of(features, [](const auto& f) { return f.property == "name"; }));
}

// A member hides an inherited one of its name: the text `target` of `Named`
// is no reference, so its keyword is offered.
void a_member_hides_an_inherited_one_of_its_name() {
  const World world;
  using Labels = std::vector<std::string>;
  CHECK(labels_at(*world.lr, "hide:foo:d<|>one") == Labels{"done"});
}

void the_replaced_token_is_a_spelling_only_over_its_whole_span() {
  const auto parser = twords::generated::Suite::createParser();
  const auto parsed = parser->parse("case:ACTIVE:");
  CHECK(parsed.accepted && parsed.diagnostics.empty());
  const auto offers_name = [&](cursiv::TextOffset resume) {
    std::vector<cursiv::NextFeature> features;
    parser->nextFeaturesAt(parsed.cst(), 5, resume, features);
    return std::ranges::any_of(features, [](const auto& f) { return f.property == "name"; });
  };
  CHECK(offers_name(11));
  CHECK(!offers_name(7));
}

// A token is still typed where its last unit is a whole letter: a stray
// continuation byte or a cut sequence after one ends it.
void a_token_ending_in_a_whole_letter_is_still_typed() {
  struct Asks final : cursiv::DefaultCompletionProvider {
    using DefaultCompletionProvider::DefaultCompletionProvider;
    using DefaultCompletionProvider::performNextTokenCompletion;
  };
  const World world;
  const Asks provider(*world.lr);
  const cursiv::Document document(
      cursiv::TextDocument::create("file:///t.wlr", "words-lr", 0, ""));
  for (const auto& [token, next] :
       {std::pair<std::string_view, bool>{"a", false},
        {"\xC3\xA9", false},
        {"9", true},
        {"a;", true},
        {"a\x80", true},
        {"\xC3\xA9\x80", true},
        {"a\xC3", true}})
    CHECK(provider.performNextTokenCompletion(
              document, token, 0,
              static_cast<cursiv::TextOffset>(token.size())) == next);
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
  a_word_terminal_offers_its_property_if_any_spelling_fits();
  a_condition_may_ask_for_a_longer_spelling();
  a_candidate_keeps_the_trivia_before_it();
  a_token_that_reads_on_into_the_spelling_ends_the_search();
  eof_counts_share_their_suffix_search();
  an_eof_completion_keeps_a_nonempty_return_to_the_start();
  an_eof_guard_reads_the_retained_suffix();
  eof_spellings_share_validation_without_losing_longer_candidates();
  held_tokens_share_their_lexical_suffixes();
  a_guard_before_the_caret_is_read_again_with_the_candidate();
  a_member_hides_an_inherited_one_of_its_name();
  the_replaced_token_is_a_spelling_only_over_its_whole_span();
  a_context_names_the_node_of_its_token();
  a_token_ending_in_a_whole_letter_is_still_typed();
}

}  // namespace word_terminals

int main() {
  cursiv::test::countExpectations();
  default_completion_provider::run();
  fuzzy_matcher::run();
  caret_repaired::run();
  lr_completion_caret::run();
  multiline_word::run();
  word_terminals::run();
  return cursiv::test::report("completion");
}
