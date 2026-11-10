// Completion items and formatting edits on the workspace language;
// `edits_narrow_test` runs the same tests on a parser with narrow table rows.

#include <lsp/types.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/completion/CompletionContext.hpp"
#include "cursiv/completion/DefaultCompletionProvider.hpp"
#include "cursiv/formatting/AbstractFormatter.hpp"
#include "cursiv/formatting/Formatter.hpp"
#include "cursiv/formatting/FormattingEngine.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/ExpectFormatting.hpp"
#include "cursiv/ExpectedCompletion.hpp"
#include "cursiv/ParseHelper.hpp"
#include "support/OffersEveryKeyword.hpp"
#include "support/Throws.hpp"
#include "support/Editor.hpp"
#include "support/WorkspaceWorld.hpp"
#include "workspace/lsp/LspModule.hpp"
#include "workspace/lsp/LspServices.hpp"
#include "workspace/lsp/WorkspaceFormatter.hpp"

namespace completion_provider {

namespace {

namespace ct = cursiv::test;

// The workspace language rather than a bare grammar: its members hold
// references, which completion offers from the scope.
struct World : support::LspWorkspace {
  explicit World(bool everyKeyword = false)
      : WorkspaceWorld([everyKeyword](cursiv::LspServices& language) {
          if (everyKeyword) support::offer_every_keyword(language);
        }) {
    support::take_both_ranges(shared);
  }
};

using support::offered;
using support::offers;

[[nodiscard]] const lsp::CompletionItem* find(const lsp::CompletionList& list,
                                              std::string_view label) {
  const auto at = std::ranges::find_if(
      list.items,
      [label](const lsp::CompletionItem& i) { return i.label == label; });
  return at == list.items.end() ? nullptr : &*at;
}

// `marked` without its `<|>` caret marker.
[[nodiscard]] std::string text_of(std::string_view marked) {
  std::string out(marked);
  const std::size_t at = out.find("<|>");
  if (at != std::string::npos) out.erase(at, 3);
  return out;
}

// Converts an LSP position to an offset; the texts here are ASCII, so a
// character is a byte.
[[nodiscard]] std::size_t offset_of(std::string_view text,
                                    const lsp::Position& at) {
  std::size_t off = 0;
  for (lsp::Uint line = 0; line < at.line; ++line) {
    const std::size_t nl = text.find('\n', off);
    if (nl == std::string_view::npos) return text.size();
    off = nl + 1;
  }
  return std::min(off + at.character, text.size());
}

[[nodiscard]] std::string apply(std::string text, const lsp::Range& range,
                                std::string_view newText) {
  const std::size_t from = offset_of(text, range.start);
  const std::size_t to = offset_of(text, range.end);
  return text.replace(from, to - from, newText);
}

// Completion at the end of a one-line `text`, broken or not.
[[nodiscard]] lsp::CompletionList offeredWithoutATree(const World& world,
                                                      std::string text) {
  const auto caret = static_cast<lsp::Uint>(text.size());
  const std::shared_ptr<cursiv::Document> document =
      ct::parseHelper(*world.ws, std::move(text));
  lsp::CompletionParams params;
  params.textDocument.uri = lsp::Uri::parse(document->uri);
  params.position = lsp::Position{0, caret};
  return world.ws->completionProvider->getCompletion(*document, params,
                                                     cursiv::kNoCancel);
}

void an_empty_document_offers_what_may_begin_it() {
  const World world;
  const lsp::CompletionList list = offeredWithoutATree(world, "");

  CHECK(offers(list, "datatype"));
  CHECK(offers(list, "entity"));
  CHECK(offers(list, "package"));
}

void a_half_typed_word_is_filtered_not_followed() {
  const World world;
  const lsp::CompletionList list = offeredWithoutATree(world, "ent");

  CHECK(offers(list, "entity"));
  CHECK(!offers(list, "datatype"));
  CHECK(!offers(list, "package"));
}

// Without the edit, an editor would write `ententity`.
void the_item_replaces_the_word_being_typed() {
  const World world;
  const lsp::CompletionList list = offeredWithoutATree(world, "ent");

  const lsp::CompletionItem* const entity = find(list, "entity");
  CHECK(entity != nullptr);
  if (entity == nullptr) return;
  CHECK(entity->textEdit.has_value());
  if (!entity->textEdit.has_value()) return;
  const auto& edit = std::get<lsp::InsertReplaceEdit>(*entity->textEdit);
  CHECK(edit.newText == "entity");
  CHECK(edit.insert.start.character == 0);
  CHECK(edit.insert.end.character == 3);
  // Nothing follows the caret, so both ranges are the same.
  CHECK(edit.replace.start.character == 0);
  CHECK(edit.replace.end.character == 3);
}

// The insert range stops at the caret, the replace range at the end of the
// word.
void an_item_covers_the_word_the_caret_sits_in() {
  const World world;
  offered(*world.ws, "datatype String\nentity E { f : Str<|>ng }",
          [](const lsp::CompletionList& list) {
            const lsp::CompletionItem* const item = find(list, "String");
            CHECK(item != nullptr);
            if (item == nullptr) return;
            CHECK(item->textEdit.has_value());
            if (!item->textEdit.has_value()) return;
            const auto& edit =
                std::get<lsp::InsertReplaceEdit>(*item->textEdit);
            CHECK(edit.newText == "String");
            CHECK(edit.insert.start.character == 15);
            CHECK(edit.insert.end.character == 18);
            CHECK(edit.replace.start.character == 15);
            CHECK(edit.replace.end.character == 20);
          });
}

// Applies each range and checks the document it gives: with the caret
// mid-word, the insert and replace ranges give different documents.
void an_applied_edit_leaves_the_word_the_author_meant() {
  const auto check = [](std::string source, std::string_view label,
                        std::string_view wantReplace,
                        std::string_view wantInsert) {
    const World world;
    const std::string document = text_of(source);
    offered(*world.ws, std::move(source), [&](const lsp::CompletionList& list) {
      const lsp::CompletionItem* const item = find(list, label);
      CHECK(item != nullptr);
      if (item == nullptr || !item->textEdit.has_value()) return;
      const auto& edit = std::get<lsp::InsertReplaceEdit>(*item->textEdit);
      CHECK(apply(document, edit.replace, edit.newText) == wantReplace);
      CHECK(apply(document, edit.insert, edit.newText) == wantInsert);
    });
  };
  // A keyword around the caret: replace gives the word, insert repeats its
  // tail.
  check("ent<|>ity E { }", "entity", "entity E { }", "entityity E { }");
  // A cross-reference candidate.
  check("datatype String\nentity E { f : Str<|>ng }", "String",
        "datatype String\nentity E { f : String }",
        "datatype String\nentity E { f : Stringng }");
}

// The keyword boundary is checked at the end of the word, not at the caret.
void a_keyword_is_offered_from_inside_a_word() {
  const World world;
  offered(*world.ws, "ent<|>ity E {}", [](const lsp::CompletionList& list) {
    CHECK(offers(list, "entity"));
  });
  offered(*world.ws, "ent<|>ity E {}", [](const lsp::CompletionList& list) {
    const lsp::CompletionItem* const item = find(list, "entity");
    CHECK(item != nullptr);
    if (item == nullptr || !item->textEdit.has_value()) return;
    const auto& edit = std::get<lsp::InsertReplaceEdit>(*item->textEdit);
    CHECK(edit.insert.start.character == 0 && edit.insert.end.character == 3);
    CHECK(edit.replace.start.character == 0 && edit.replace.end.character == 6);
  });
}

void a_word_no_keyword_begins_offers_no_keyword() {
  const World world;
  offered(*world.ws, "zq<|>xw E {}", [](const lsp::CompletionList& list) {
    CHECK(!offers(list, "entity"));
    CHECK(!offers(list, "datatype"));
    CHECK(!offers(list, "package"));
  });
}

// A keyword with no letter, such as `{`, is offered only when a language asks,
// as `offer_every_keyword` does.
void a_keyword_with_no_letter_is_left_out() {
  const World world;
  offered(*world.ws, "entity E <|>", [](const lsp::CompletionList& list) {
    CHECK(offers(list, "extends"));
    CHECK(!offers(list, "{"));
  });
  const World every(true);
  offered(*every.ws, "entity E <|>", [](const lsp::CompletionList& list) {
    CHECK(offers(list, "extends"));
    CHECK(offers(list, "{"));
  });
}

// `pkg` matches `package` as a subsequence from the start of the word.
void a_keyword_is_matched_like_a_name() {
  const World world;
  const lsp::CompletionList list = offeredWithoutATree(world, "pkg");
  CHECK(offers(list, "package"));
  CHECK(!offers(list, "entity"));
  CHECK(!offers(list, "datatype"));
}

void a_keyword_says_what_it_is_and_sorts_after_the_names() {
  const World world;
  offered(*world.ws, "entity E <|>", [](const lsp::CompletionList& list) {
    const lsp::CompletionItem* const keyword = find(list, "extends");
    CHECK(keyword != nullptr);
    if (keyword == nullptr) return;
    CHECK(keyword->detail == "Keyword");
    CHECK(keyword->sortText == "1");
  });
  offered(*world.ws, "datatype String\nentity E { f : <|>",
          [](const lsp::CompletionList& list) {
            const lsp::CompletionItem* const name = find(list, "String");
            CHECK(name != nullptr);
            if (name == nullptr) return;
            CHECK(name->sortText == "0");
          });
}

// After `entity E`, the name is still being typed: nothing is offered, even
// with every keyword enabled.
void a_word_that_answers_nothing_offers_nothing() {
  const World world(true);
  offered(*world.ws, "entity E<|>", [](const lsp::CompletionList& list) {
    CHECK(list.items.empty());
  });
}

// `Feature::type` is a reference: the scope offers the visible declarations,
// not the terminal name `ID`.
void a_reference_offers_the_names_in_scope() {
  const World world;
  offered(*world.ws, "datatype String\ndatatype Int\nentity E { f : <|>",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "String"));
            CHECK(offers(list, "Int"));
            CHECK(!offers(list, "ID"));
          });
}

// The same caret with `}` after it: completion reads the state before the gap,
// not the one the repair reached.
void a_reference_offers_the_names_in_scope_mid_document() {
  const World world;
  offered(*world.ws,
          "datatype String\ndatatype Int\nentity E {\n  f : <|>\n}\n",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "String"));
            CHECK(offers(list, "Int"));
          });
}

// Nothing is offered inside a comment, whatever the grammar allows there.
void a_caret_inside_a_comment_is_offered_nothing() {
  const World world;
  // With a word under the caret and without one.
  offered(*world.ws, "entity E {}\n/* aa<|>a */\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(list.items.empty()); });
  offered(*world.ws, "entity E {}\n/* --<|>- */\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(list.items.empty()); });
  // Inside the opening `/*`.
  offered(*world.ws, "entity E {}\n/<|>* aaa */\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(list.items.empty()); });
  offered(*world.ws, "entity E {}\n/*<|> aaa */\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(list.items.empty()); });
  // A line comment.
  offered(*world.ws, "entity E {}\n// aa<|>a\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(list.items.empty()); });
  // An unclosed comment matches no terminal, and the repair skips its text.
  offered(*world.ws, "entity E {}\n/* aa<|>a",
          [](const lsp::CompletionList& list) { CHECK(list.items.empty()); });
}

void the_edges_of_a_comment_still_answer() {
  const World world;
  offered(*world.ws, "entity E {}\n/* aaa */<|>\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(!list.items.empty()); });
  offered(*world.ws, "entity E {}\n<|>/* aaa */\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(!list.items.empty()); });
}

void a_candidate_carries_its_documentation() {
  const World world;
  offered(
      *world.ws,
      "/** The string. */\ndatatype String\ndatatype Int\nentity E { f : <|>",
      [](const lsp::CompletionList& list) {
        const lsp::CompletionItem* const documented = find(list, "String");
        CHECK(documented != nullptr && documented->documentation.has_value());
        if (documented == nullptr || !documented->documentation.has_value())
          return;
        const auto* const markup =
            std::get_if<lsp::MarkupContent>(&*documented->documentation);
        CHECK(markup != nullptr && markup->value == "The string.");
        const lsp::CompletionItem* const bare = find(list, "Int");
        CHECK(bare != nullptr && !bare->documentation.has_value());
      });
}

void a_terminal_is_never_offered_by_name() {
  const World world;
  offered(*world.ws, "entity <|>",
          [](const lsp::CompletionList& list) { CHECK(!offers(list, "ID")); });
  offered(*world.ws, "entity E<|>",
          [](const lsp::CompletionList& list) { CHECK(!offers(list, "ID")); });
}

void a_reference_is_filtered_by_what_is_typed() {
  const World world;
  offered(*world.ws, "datatype String\ndatatype Int\nentity E { f : St<|>",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "String"));
            CHECK(!offers(list, "Int"));
          });
}

// The grammar reaches `datatype` by two paths, inside a package and outside
// one.
void the_same_answer_is_offered_once() {
  const World world;
  const lsp::CompletionList list = offeredWithoutATree(world, "");

  for (const lsp::CompletionItem& item : list.items) {
    const auto same = std::ranges::count_if(
        list.items, [&](const lsp::CompletionItem& other) {
          return other.label == item.label;
        });
    CHECK(same == 1);
  }
}

void two_homonyms_of_different_types_stay_two_offers() {
  const World world;
  offered(*world.ws, "datatype String\nentity String { }\nentity E { f : <|>",
          [](const lsp::CompletionList& list) {
            std::vector<std::string> details;
            for (const lsp::CompletionItem& item : list.items)
              if (item.label == "String" && item.detail.has_value())
                details.push_back(*item.detail);
            std::ranges::sort(details);
            CHECK(details.size() == 2);
            CHECK(details == std::vector<std::string>{"DataType", "Entity"});
          });
}

void a_broken_document_still_answers() {
  const World world(true);
  offered(*world.ws, "entity E { f : String\nentity <|>",
          [](const lsp::CompletionList& list) { CHECK(!list.items.empty()); });
}

void a_cancelled_request_gives_up() {
  const World world;
  const std::shared_ptr<cursiv::Document> document =
      ct::parseDocument(*world.ws, "datatype String\nentity E { f : ");
  std::stop_source source;
  source.request_stop();

  CHECK(support::throws<cursiv::OperationCancelled>([&] {
    (void)world.ws->completionProvider->getCompletion(
        *document, lsp::CompletionParams{}, source.get_token());
  }));
}

}  // namespace

void run() {
  an_empty_document_offers_what_may_begin_it();
  a_half_typed_word_is_filtered_not_followed();
  the_item_replaces_the_word_being_typed();
  an_item_covers_the_word_the_caret_sits_in();
  an_applied_edit_leaves_the_word_the_author_meant();
  a_keyword_is_offered_from_inside_a_word();
  a_word_no_keyword_begins_offers_no_keyword();
  a_keyword_with_no_letter_is_left_out();
  a_keyword_is_matched_like_a_name();
  a_keyword_says_what_it_is_and_sorts_after_the_names();
  a_word_that_answers_nothing_offers_nothing();
  a_terminal_is_never_offered_by_name();
  a_reference_offers_the_names_in_scope();
  a_reference_offers_the_names_in_scope_mid_document();
  a_caret_inside_a_comment_is_offered_nothing();
  the_edges_of_a_comment_still_answer();
  a_candidate_carries_its_documentation();
  a_reference_is_filtered_by_what_is_typed();
  the_same_answer_is_offered_once();
  two_homonyms_of_different_types_stay_two_offers();
  a_broken_document_still_answers();
  a_cancelled_request_gives_up();
}

}  // namespace completion_provider

// The formatter on the workspace language; a test that needs another rule
// installs its own formatter.
namespace formatter {

namespace {

namespace ct = cursiv::test;

struct World {
  ws::WorkspaceLanguage services = ws::create_lsp_services();
  ws::WorkspaceSharedServices& shared = *services.shared;
  int nextUri = 0;

  [[nodiscard]] const cursiv::Formatter& formatter() const {
    return *services.language.formatter;
  }

  // A new URI per text: some tests build the same source twice and need two
  // documents.
  cursiv::Document* built(std::string text) {
    return support::built(shared,
                          "file:///w/d" + std::to_string(nextUri++) + ".ws",
                          std::move(text));
  }
};

// Replaces the workspace formatter with a test formatter, for rules the
// language's own formatter does not use.
template <class FormatterT>
void install(World& world) {
  world.services.language.formatter =
      std::make_unique<const FormatterT>(world.services.language);
}

struct FitFormatter final : ws::WorkspaceFormatter {
  using WorkspaceFormatter::WorkspaceFormatter;
  void formatFeature(cursiv::FormattingBuilder& builder,
                     const ws::ast::Feature* feature) const override {
    auto formatter = builder.getNodeFormatter(feature);
    formatter.keyword(":").prepend(fit(oneSpace({.priority = 5}), newLine));
    WorkspaceFormatter::formatFeature(builder, feature);
  }
};

// A blank line and one indent level: tests prepend it to a selection to see
// which node it reaches.
[[nodiscard]] cursiv::FormattingAction blankLineIndented() {
  return cursiv::FormattingAction{
      .options = {}, .moves = {cursiv::FormattingMove{.tabs = 1, .lines = 2}}};
}

struct DeclarationOrderFormatter final : ws::WorkspaceFormatter {
  using WorkspaceFormatter::WorkspaceFormatter;
  void formatEntity(cursiv::FormattingBuilder& builder,
                    const ws::ast::Entity* entity) const override {
    WorkspaceFormatter::formatEntity(builder, entity);
    auto formatter = builder.getNodeFormatter(entity);
    formatter.properties<&ws::ast::Entity::features, &ws::ast::Entity::name>()
        .slice(0, 1)
        .prepend(blankLineIndented());
    formatter.keywords("}", "{").slice(0, 1).prepend(newLines(2));
  }
};

struct HeldRecordsFormatter final : ws::WorkspaceFormatter {
  using WorkspaceFormatter::WorkspaceFormatter;
  void formatEntity(cursiv::FormattingBuilder& builder,
                    const ws::ast::Entity* entity) const override {
    WorkspaceFormatter::formatEntity(builder, entity);
    auto formatter = builder.getNodeFormatter(entity);
    const std::vector<const cursiv::AstNode*> features(entity->features.begin(),
                                                       entity->features.end());
    formatter.nodes(features).prepend(blankLineIndented());
    formatter.cst(formatter.keyword("}").nodes()).prepend(newLines(2));
  }
};

struct SelfTargetingFormatter final : ws::WorkspaceFormatter {
  using WorkspaceFormatter::WorkspaceFormatter;
  void formatEntity(cursiv::FormattingBuilder& builder,
                    const ws::ast::Entity* entity) const override {
    WorkspaceFormatter::formatEntity(builder, entity);
    builder.getNodeFormatter(entity).node(*entity).prepend(newLines(2));
  }
};

struct AppendIndentFormatter final : ws::WorkspaceFormatter {
  using WorkspaceFormatter::WorkspaceFormatter;
  void formatDataType(cursiv::FormattingBuilder& builder,
                      const ws::ast::DataType* dataType) const override {
    WorkspaceFormatter::formatDataType(builder, dataType);
    builder.getNodeFormatter(dataType)
        .property<&ws::ast::DataType::name>()
        .append(indent);
  }
};

struct AppendLevelFormatter final : ws::WorkspaceFormatter {
  using WorkspaceFormatter::WorkspaceFormatter;
  void formatEntity(cursiv::FormattingBuilder& builder,
                    const ws::ast::Entity* entity) const override {
    auto formatter = builder.getNodeFormatter(entity);
    formatter.keyword("entity").append(oneSpace);
    formatter.keyword("{").prepend(oneSpace).append(indent);
    formatter.keyword("}").prepend(newLine);
  }
};

// An `append` on a node that has children: its gap, after the whole subtree,
// is visited before the gaps inside it. No other rule claims either gap.
struct ContainerAppendFormatter final : ws::WorkspaceFormatter {
  using WorkspaceFormatter::WorkspaceFormatter;
  void formatWorkspace(cursiv::FormattingBuilder&,
                       const ws::ast::Workspace*) const override {}
  void formatEntity(cursiv::FormattingBuilder& builder,
                    const ws::ast::Entity* entity) const override {
    auto formatter = builder.getNodeFormatter(entity);
    formatter.keyword(":").prepend(noSpace);
    formatter.node(*entity).append(newLines(2));
  }
};

struct OnTypeFormatter final : ws::WorkspaceFormatter {
  using WorkspaceFormatter::WorkspaceFormatter;
  [[nodiscard]] std::optional<lsp::DocumentOnTypeFormattingOptions>
  formatOnTypeOptions() const noexcept override {
    return lsp::DocumentOnTypeFormattingOptions{.firstTriggerCharacter = "}"};
  }
};

struct ClampedIndexFormatter final : ws::WorkspaceFormatter {
  using WorkspaceFormatter::WorkspaceFormatter;
  void formatEntity(cursiv::FormattingBuilder& builder,
                    const ws::ast::Entity* entity) const override {
    WorkspaceFormatter::formatEntity(builder, entity);
    builder.getNodeFormatter(entity)
        .property<&ws::ast::Entity::features>(7)
        .prepend(blankLineIndented());
  }
};

// Each gap claimed from both sides: `name`'s append and the colon's prepend,
// `entity`'s append and the name's prepend. The rule of higher priority is
// visited first in one pair and last in the other.
struct SidePriorityFormatter final : ws::WorkspaceFormatter {
  using WorkspaceFormatter::WorkspaceFormatter;
  void formatEntity(cursiv::FormattingBuilder& builder,
                    const ws::ast::Entity* entity) const override {
    WorkspaceFormatter::formatEntity(builder, entity);
    auto formatter = builder.getNodeFormatter(entity);
    formatter.keyword("entity").append(spaces(1, {.priority = 5}));
    formatter.property<&ws::ast::Entity::name>().prepend(spaces(3));
  }
  void formatFeature(cursiv::FormattingBuilder& builder,
                     const ws::ast::Feature* feature) const override {
    WorkspaceFormatter::formatFeature(builder, feature);
    builder.getNodeFormatter(feature)
        .property<&ws::ast::Feature::name>()
        .append(spaces(2, {.priority = 5}));
  }
};

// A gap kept from the token after it, at a priority above the space the
// colon before it asks for.
struct KeepPriorityFormatter final : ws::WorkspaceFormatter {
  using WorkspaceFormatter::WorkspaceFormatter;
  void formatFeature(cursiv::FormattingBuilder& builder,
                     const ws::ast::Feature* feature) const override {
    WorkspaceFormatter::formatFeature(builder, feature);
    builder.getNodeFormatter(feature)
        .property<&ws::ast::Feature::type>()
        .prepend(cursiv::KeepActionBuilder{}({.priority = 9}));
  }
};

struct LeadingSpaceFormatter final : ws::WorkspaceFormatter {
  using WorkspaceFormatter::WorkspaceFormatter;
  void formatWorkspace(cursiv::FormattingBuilder&,
                       const ws::ast::Workspace*) const override {}
  void formatEntity(cursiv::FormattingBuilder& builder,
                    const ws::ast::Entity* entity) const override {
    builder.getNodeFormatter(entity).keyword("entity").prepend(spaces(2));
  }
};

// Two spaces per level, so tests can name the columns they expect.
[[nodiscard]] lsp::FormattingOptions options() {
  lsp::FormattingOptions chosen;
  chosen.tabSize = 2;
  chosen.insertSpaces = true;
  return chosen;
}

template <class W>
[[nodiscard]] std::vector<lsp::TextEdit> format(
    const W& world, const cursiv::Document& document,
    const lsp::FormattingOptions& chosen = options()) {
  lsp::DocumentFormattingParams params;
  params.options = chosen;
  return world.formatter().formatDocument(document, params, cursiv::kNoCancel);
}

template <class W>
[[nodiscard]] std::vector<lsp::TextEdit> formatRange(
    const W& world, const cursiv::Document& document, lsp::Range range) {
  lsp::DocumentRangeFormattingParams params;
  params.options = options();
  params.range = range;
  return world.formatter().formatDocumentRange(document, params,
                                               cursiv::kNoCancel);
}

// Applies the edits to the document's text, as a client does.
[[nodiscard]] std::string applied(const cursiv::Document& document,
                                  const std::vector<lsp::TextEdit>& edits) {
  std::vector<cursiv::TextEdit> converted;
  converted.reserve(edits.size());
  for (const lsp::TextEdit& edit : edits)
    converted.push_back({cursiv::from_lsp(edit.range), edit.newText});
  return cursiv::TextDocument::applyEdits(document.textDocument(), converted);
}

using support::kMessy;

// `kMessy` formatted by the language's rules: no space before a colon, one
// after it.
constexpr std::string_view kShaped =
    "entity Person {\n"
    "  name: String\n"
    "  age: String\n"
    "}\n"
    "datatype String\n";

void formatting_twice_is_formatting_once() {
  World world;
  (void)ct::expectFormatting(world.services.language,
                             {.before = std::string(kMessy),
                              .after = std::string(kShaped),
                              .options = options(),
                              .disposeAfterCheck = true});

  const cursiv::Document* const formatted = world.built(std::string(kShaped));
  CHECK(format(world, *formatted).empty());
}

void a_document_already_in_shape_answers_no_edit() {
  World world;
  const cursiv::Document* const messy = world.built(std::string(kMessy));
  const std::string once = applied(*messy, format(world, *messy));

  const cursiv::Document* const clean = world.built(once);
  CHECK(format(world, *clean).empty());
}

// The workspace language reads its keywords in any case.
void a_keyword_rule_shapes_every_case_of_the_word() {
  World world;
  const cursiv::Document* const messy =
      world.built("ENTITY   Person{}\nDataType     String\n");
  CHECK(applied(*messy, format(world, *messy)) ==
        "ENTITY Person {}\nDataType String\n");
}

void a_rule_can_shape_the_gap_before_the_first_token() {
  World world;
  install<LeadingSpaceFormatter>(world);
  const cursiv::Document* const source = world.built("entity Person {}\n");
  CHECK(applied(*source, format(world, *source)) == "  entity Person {}\n");
  const cursiv::Document* const shaped = world.built("  entity Person {}\n");
  CHECK(format(world, *shaped).empty());
}

void indentation_counts_do_not_wrap() {
  const auto chosen = options();
  bool rejected = false;
  try {
    (void)cursiv::detail::indentation_text(
        std::numeric_limits<std::int32_t>::max(), chosen);
  } catch (const std::length_error&) {
    rejected = true;
  }
  CHECK(rejected);

  const cursiv::Cst cst;
  cursiv::detail::FormattingActions actions(0);
  const cursiv::HiddenNodeFormatter negative(
      cst, cursiv::kNoNode, std::numeric_limits<std::int32_t>::min(), chosen,
      "\n", actions);
  CHECK(negative.indentation(-1).empty());

  const cursiv::HiddenNodeFormatter positive(
      cst, cursiv::kNoNode, std::numeric_limits<std::int32_t>::max(), chosen,
      "\n", actions);
  rejected = false;
  try {
    (void)positive.indentation(1);
  } catch (const std::length_error&) {
    rejected = true;
  }
  CHECK(rejected);
}

// A selection keeps the edits whose lines overlap the selected lines; the gap
// closing the last line is kept whole. No edit begins past the selection.
void a_range_format_keeps_the_edits_on_its_lines() {
  World world;
  const cursiv::Document* const messy = world.built(std::string(kMessy));
  const lsp::Range head{{0, 0}, {0, 16}};
  const std::vector<lsp::TextEdit> ranged = formatRange(world, *messy, head);
  CHECK(!ranged.empty());
  bool pastTheEnd = false;
  for (const lsp::TextEdit& edit : ranged) {
    CHECK(edit.range.start.line >= head.start.line);
    CHECK(edit.range.start.line <= head.end.line);
    pastTheEnd = pastTheEnd || edit.range.end.line > head.end.line;
  }
  CHECK(pastTheEnd);
  CHECK(ranged.size() < format(world, *messy).size());
}

// A selection of several lines: the gap that only reaches its first line stays,
// the gaps within it are formatted -- inside a construction that starts before
// the selection too.
void a_range_format_leaves_what_only_reaches_its_start() {
  World world;
  const cursiv::Document* const messy = world.built(std::string(kMessy));
  CHECK(applied(*messy, formatRange(world, *messy, {{1, 0}, {2, 5}})) ==
        "entity   Person{\n"
        "name: String\n"
        "  age: String\n"
        "}\n"
        "datatype     String\n");
  CHECK(applied(*messy, formatRange(world, *messy, {{2, 0}, {4, 5}})) ==
        "entity   Person{\n"
        "name   :String\n"
        "     age: String\n"
        "}\n"
        "datatype String\n");
}

// The document's own rules apply to any selection, even one that runs past the
// last line of the document's text.
void a_range_format_always_applies_the_root_rules() {
  World world;
  const cursiv::Document* const messy =
      world.built("datatype A\ndatatype B    datatype C\n\n\n");
  CHECK(applied(*messy, formatRange(world, *messy, {{1, 0}, {3, 0}})) ==
        "datatype A\ndatatype B\ndatatype C\n\n\n");
}

// A repaired parse holds text the author did not write, so no edits are
// given.
void a_document_with_a_parse_error_is_not_formatted() {
  World world;
  const cursiv::Document* const broken =
      world.built("entity   Person{\nname   :\n}\n");
  CHECK(!broken->parseResult.diagnostics.empty());
  CHECK(format(world, *broken).empty());
}

// `fit` carries no options of its own: a candidate's priority does not survive
// the combination, so a later rule of priority zero on the same gap wins.
void a_fit_carries_no_options() {
  World world;
  install<FitFormatter>(world);
  (void)ct::expectFormatting(world.services.language,
                             {.before = "entity Person {\n"
                                        "  name   :String\n"
                                        "}\n",
                              .after = "entity Person {\n"
                                       "  name: String\n"
                                       "}\n",
                              .options = options(),
                              .disposeAfterCheck = true});
}

void a_gap_claimed_from_both_sides_keeps_the_higher_priority() {
  World world;
  install<SidePriorityFormatter>(world);
  (void)ct::expectFormatting(world.services.language,
                             {.before = "entity   Person {\n"
                                        "  name:String\n"
                                        "}\n",
                              .after = "entity Person {\n"
                                       "  name  : String\n"
                                       "}\n",
                              .options = options(),
                              .disposeAfterCheck = true});
}

// `keep` claims a gap like any other rule, by priority.
void a_kept_gap_holds_against_the_other_side() {
  World world;
  install<KeepPriorityFormatter>(world);
  (void)ct::expectFormatting(world.services.language,
                             {.before = "entity Person {\n"
                                        "  name :   String\n"
                                        "}\n",
                              .after = "entity Person {\n"
                                       "  name:   String\n"
                                       "}\n",
                              .options = options(),
                              .disposeAfterCheck = true});
}

// `properties` and `keywords` list their matches name by name in the order
// the rule gives them, not in document order: the first of `features, name`
// is the first feature, the first of `}`, `{` is the closing brace.
void a_multi_name_selection_keeps_the_order_given() {
  World world;
  install<DeclarationOrderFormatter>(world);
  (void)ct::expectFormatting(world.services.language,
                             {.before = "entity Person {\n"
                                        "  name: String\n"
                                        "  age: String\n"
                                        "}\n",
                              .after = "entity Person {\n"
                                       "\n"
                                       "  name: String\n"
                                       "  age: String\n"
                                       "\n"
                                       "}\n",
                              .options = options(),
                              .disposeAfterCheck = true});
}

// A rule may target AST nodes it holds and CST nodes it already selected,
// without `property` or `keyword`.
void a_rule_targets_the_records_it_holds() {
  World world;
  install<HeldRecordsFormatter>(world);
  (void)ct::expectFormatting(world.services.language,
                             {.before = "entity Person {\n"
                                        "  name: String\n"
                                        "  age: String\n"
                                        "}\n",
                              .after = "entity Person {\n"
                                       "\n"
                                       "  name: String\n"
                                       "\n"
                                       "  age: String\n"
                                       "\n"
                                       "}\n",
                              .options = options(),
                              .disposeAfterCheck = true});
}

// The root's rule on its elements and an element's rule on itself claim the
// same boundary; the walk visits the root first, so the element's own rule is
// the one that stays.
void a_descendant_rule_wins_over_an_ancestor_rule() {
  World world;
  install<SelfTargetingFormatter>(world);
  (void)ct::expectFormatting(world.services.language,
                             {.before = "datatype String\n"
                                        "entity Person {}\n",
                              .after = "datatype String\n"
                                       "\n"
                                       "entity Person {}\n",
                              .options = options(),
                              .disposeAfterCheck = true});
}

// An `append` carrying a level reaches the comment that follows the token:
// the comment's margin takes the level, and takes it once.
void an_append_shapes_the_margin_of_the_comment_after_it() {
  World world;
  install<AppendIndentFormatter>(world);
  constexpr std::string_view kIndented =
      "datatype String\n"
      "  // c\n"
      "entity Person {}\n";
  (void)ct::expectFormatting(world.services.language,
                             {.before = "datatype String\n"
                                        "// c\n"
                                        "entity Person {}\n",
                              .after = std::string(kIndented),
                              .options = options(),
                              .disposeAfterCheck = true});

  const cursiv::Document* const formatted = world.built(std::string(kIndented));
  CHECK(format(world, *formatted).empty());
}

void a_hidden_callback_can_add_rules_after_a_deferred_append() {
  World world;
  const cursiv::Document& document =
      *world.built("datatype String\n// c\nentity Person {}\n");
  const auto& cst = document.parseResult.cst();
  cursiv::detail::FormattingActions actions(cst.size());
  bool registered = false;
  for (cursiv::NodeId id = 0; id < cst.size(); ++id) {
    if (cst.childless(id) && cst.text(id) == "String") {
      actions.collect(id, cursiv::FormattingMode::Append,
                      cursiv::IndentActionBuilder{}());
      registered = true;
      break;
    }
  }
  CHECK(registered);
  int comments = 0;
  const auto edits = cursiv::detail::run_formatting(
      document, cst, options(), std::nullopt, actions,
      [&](cursiv::NodeId id, std::int32_t level, std::string_view) {
        ++comments;
        CHECK(level == 1);
        // Growing the action pool must preserve the deferred indentation.
        actions.collect(id, cursiv::FormattingMode::Append,
                        cursiv::KeepActionBuilder{}());
      },
      cursiv::kNoCancel);
  CHECK(comments == 1);
  CHECK(applied(document, edits) ==
        "datatype String\n  // c\nentity Person {}\n");
}

void comment_margins_follow_all_text_document_line_breaks() {
  World world;
  for (const std::string newline : {"\n", "\r\n", "\r"}) {
    const std::string prefix = "datatype String" + newline;
    const std::string suffix = newline + "entity Person {}" + newline;
    const cursiv::Document& document = *world.built(
        prefix + "/* c" + newline + " * d" + newline + " */" + suffix);
    const auto& cst = document.parseResult.cst();
    cursiv::detail::FormattingActions actions(cst.size());
    for (cursiv::NodeId id = 0; id < cst.size(); ++id) {
      if (cst.childless(id) && cst.text(id) == "String") {
        actions.collect(id, cursiv::FormattingMode::Append,
                        cursiv::IndentActionBuilder{}());
        break;
      }
    }
    const auto edits = cursiv::detail::run_formatting(
        document, cst, options(), std::nullopt, actions,
        [](cursiv::NodeId, std::int32_t, std::string_view) {},
        cursiv::kNoCancel);
    CHECK(applied(document, edits) ==
          prefix + "  /* c" + newline + "   * d" + newline + "   */" + suffix);
  }
}

// A comment's margin is measured in the unit the options ask for, and what is
// taken off it is taken in that unit: at a tab size of two, "\t " is three
// columns, and the one column too many is not the whole tab.
void a_mixed_comment_margin_is_shaped_in_one_pass() {
  World world;
  constexpr std::string_view kCommented =
      "entity A {\n"
      "  /* one */\n"
      "  name: String\n"
      "}\n";
  (void)ct::expectFormatting(world.services.language,
                             {.before = "entity A {\n"
                                        "\t /* one */\n"
                                        "name:String\n"
                                        "}\n",
                              .after = std::string(kCommented),
                              .options = options(),
                              .disposeAfterCheck = true});

  const cursiv::Document* const formatted =
      world.built(std::string(kCommented));
  CHECK(format(world, *formatted).empty());
}

// A block comment moves as a block over a mixed margin too: every later line
// loses the columns the first one loses, so the inner `*` stays under the
// opening one.
void a_block_comment_keeps_its_alignment_over_a_mixed_margin() {
  World world;
  const cursiv::Document& document = *world.built(
      "datatype String\n\t /* c\n\t  * d\n\t  */\nentity Person {}\n");
  const auto& cst = document.parseResult.cst();
  cursiv::detail::FormattingActions actions(cst.size());
  for (cursiv::NodeId id = 0; id < cst.size(); ++id) {
    if (cst.childless(id) && cst.text(id) == "String") {
      actions.collect(id, cursiv::FormattingMode::Append,
                      cursiv::IndentActionBuilder{}());
      break;
    }
  }
  const auto edits = cursiv::detail::run_formatting(
      document, cst, options(), std::nullopt, actions,
      [](cursiv::NodeId, std::int32_t, std::string_view) {}, cursiv::kNoCancel);
  CHECK(applied(document, edits) ==
        "datatype String\n  /* c\n   * d\n   */\nentity Person {}\n");
}

void mixed_comment_margins_reach_their_shape_in_one_pass() {
  lsp::FormattingOptions tabs;
  tabs.tabSize = 4;
  tabs.insertSpaces = false;
  World world;
  for (const lsp::FormattingOptions& chosen : {options(), tabs})
    for (const std::string_view comment : {"/* one */", "// one"})
      for (const std::string_view margin :
           {"\t ", "\t\t", "    \t", "        ", "     "}) {
        const cursiv::Document* const source =
            world.built("entity A {\n" + std::string(margin) +
                        std::string(comment) + "\nname:String\n}\n");
        CHECK(!source->parseRecovered());
        const cursiv::Document* const once =
            world.built(applied(*source, format(world, *source, chosen)));
        CHECK(format(world, *once, chosen).empty());
      }
}

// An `append` targets the next node, and its level holds until that node's
// parent closes: `{`.append(indent) indents the first feature and the closing
// brace, and leaves the second feature alone.
void an_append_level_stays_while_its_target_is_a_construction() {
  World world;
  install<AppendLevelFormatter>(world);
  (void)ct::expectFormatting(world.services.language,
                             {.before = "entity Person {\n"
                                        "name: String\n"
                                        "age: String\n"
                                        "}\n",
                              .after = "entity Person {\n"
                                       "  name: String\n"
                                       "age: String\n"
                                       "  }\n",
                              .options = options(),
                              .disposeAfterCheck = true});
}

void a_property_index_clamps_to_the_last_occurrence() {
  World world;
  install<ClampedIndexFormatter>(world);
  (void)ct::expectFormatting(world.services.language,
                             {.before = "entity Person {\n"
                                        "  name: String\n"
                                        "  age: String\n"
                                        "}\n",
                              .after = "entity Person {\n"
                                       "  name: String\n"
                                       "\n"
                                       "  age: String\n"
                                       "}\n",
                              .options = options(),
                              .disposeAfterCheck = true});
}

// A keystroke formats the caret's whole line, as a selection would. The
// default formatter names no trigger character, so a language must name one.
void a_format_on_type_stays_on_the_carets_line() {
  World world;
  CHECK(!world.formatter().formatOnTypeOptions().has_value());
  const cursiv::Document* const messy = world.built(std::string(kMessy));
  lsp::DocumentOnTypeFormattingParams params;
  params.options = options();
  params.position = {1, 2};
  params.ch = "m";
  const std::vector<lsp::TextEdit> typed =
      world.formatter().formatDocumentOnType(*messy, params, cursiv::kNoCancel);
  CHECK(!typed.empty());
  bool pastTheCaret = false;
  bool intoTheLine = false;
  for (const lsp::TextEdit& edit : typed) {
    CHECK(edit.range.start.line <= 1);
    CHECK(edit.range.end.line >= 1);
    pastTheCaret = pastTheCaret || (edit.range.start.line == 1 &&
                                    edit.range.start.character > 2);
    intoTheLine = intoTheLine || edit.range.start.line < 1;
  }
  CHECK(pastTheCaret);
  CHECK(intoTheLine);
  // The columns of a range do not narrow it: the caret's range and the whole
  // line answer the same edits.
  const std::string whole =
      applied(*messy, formatRange(world, *messy, {{1, 0}, {1, 14}}));
  CHECK(applied(*messy, typed) == whole);
  CHECK(applied(*messy, formatRange(world, *messy, {{1, 0}, {1, 2}})) == whole);
  CHECK(typed.size() < format(world, *messy).size());

  install<OnTypeFormatter>(world);
  const auto advertised = world.formatter().formatOnTypeOptions();
  CHECK(advertised.has_value() && advertised->firstTriggerCharacter == "}");
}

// A conversion error leaves the tree intact: only a syntax error withholds
// formatting, whole or by range.
void a_conversion_error_does_not_withhold_formatting() {
  World world;
  cursiv::Document* const document =
      world.built("entity   Person{ name   :String }\n");
  // The workspace language has no conversion that can fail, so the test adds
  // the diagnostic itself.
  cursiv::ParseDiagnostic failed;
  failed.kind = cursiv::DiagnosticKind::conversion_error;
  failed.begin = 17;
  failed.end = 21;
  document->parseResult.diagnostics.push_back(failed);
  CHECK(!document->parseRecovered());
  CHECK(!format(world, *document).empty());
  CHECK(!formatRange(world, *document, {{0, 0}, {0, 33}}).empty());
}

// A comment is anchored by the newlines around it and re-indented with its
// block; its text is kept, never reprinted from the tree.
void a_multiline_comment_is_reindented() {
  World world;
  constexpr std::string_view kCommented =
      "entity Person {\n"
      "  /*\n"
      "  a comment\n"
      "  over two lines\n"
      "  */\n"
      "  name: String\n"
      "}\n";
  (void)ct::expectFormatting(world.services.language,
                             {.before = "entity Person {\n"
                                        "/* a comment\n"
                                        "   over two lines */\n"
                                        "name: String\n"
                                        "}\n",
                              .after = std::string(kCommented),
                              .options = options(),
                              .disposeAfterCheck = true});

  const cursiv::Document* const formatted =
      world.built(std::string(kCommented));
  CHECK(format(world, *formatted).empty());
}

// Exposes the base comment formatting to the tests.
struct Comments : cursiv::AbstractFormatter {
  using AbstractFormatter::formatMultilineComment;
};

// Blanks right after the opening marker are not a line of the comment; a
// second pass keeps what the first gives, whitespace normalized or not.
void a_doc_comment_reaches_its_shape_in_one_pass() {
  World world;
  constexpr std::string_view kShapedDoc =
      "/**\n"
      " * Text\n"
      " */\n"
      "entity Person {}\n";
  (void)ct::expectFormatting(world.services.language,
                             {.before = "/** \n"
                                        " * Text\n"
                                        " */\n"
                                        "entity Person {}\n",
                              .after = std::string(kShapedDoc),
                              .options = options(),
                              .disposeAfterCheck = true});
  const cursiv::Document* const formatted =
      world.built(std::string(kShapedDoc));
  CHECK(format(world, *formatted).empty());

  for (const bool normalize : {true, false})
    for (const std::string_view text :
         {"/** \n * Text\n */", "/**  Text\n */", "/**\t\n *  a  b\n */"}) {
      const cursiv::MultilineCommentFormatOptions chosen{.normalizeWhitespace =
                                                             normalize};
      const std::string once = Comments::formatMultilineComment(text, chosen);
      CHECK(Comments::formatMultilineComment(once, chosen) == once);
    }
}

void a_one_line_comment_keeps_the_stars_it_starts_with() {
  World world;
  (void)ct::expectFormatting(world.services.language,
                             {.before = "/** **Person** */\n"
                                        "entity Person {}\n",
                              .after = "/** **Person** */\n"
                                       "entity Person {}\n",
                              .options = options(),
                              .disposeAfterCheck = true});
  for (const auto& [text, shaped] :
       {std::pair<std::string_view, std::string_view>{"/** **bold** */",
                                                      "/** **bold** */"},
        {"/** * item */", "/** * item */"},
        {"/**  *x */", "/** *x */"}}) {
    const cursiv::MultilineCommentFormatOptions chosen;
    const std::string once = Comments::formatMultilineComment(text, chosen);
    CHECK(once == shaped);
    CHECK(Comments::formatMultilineComment(once, chosen) == once);
  }
}

// Line ends follow the document, not the platform: a CRLF file gets CRLF in
// every inserted line break, those of a rewritten comment included.
void a_crlf_document_keeps_its_terminators() {
  World world;
  const cursiv::Document* const messy = world.built(
      "entity   Person{\r\n"
      "/* a comment\r\n"
      "   over two lines */\r\n"
      "name   :String\r\n"
      "}\r\n");
  const std::string once = applied(*messy, format(world, *messy));

  CHECK(once.find("/*\r\n") != std::string::npos);
  CHECK(once.find('\n') != std::string::npos);
  for (std::size_t at = 0; at < once.size(); ++at)
    if (once[at] == '\n') CHECK(at > 0 && once[at - 1] == '\r');
}

}  // namespace

// The edits of a container rule do not come out in document order, and the
// client still gets an ordered list.
void a_rule_on_a_container_is_ordered_before_it_is_answered() {
  World world;
  install<ContainerAppendFormatter>(world);
  (void)ct::expectFormatting(world.services.language,
                             {.before = "entity A { a : String }\n"
                                        "entity B { b : String }\n",
                              .after = "entity A { a: String }\n"
                                       "\n"
                                       "entity B { b: String }\n",
                              .options = options(),
                              .disposeAfterCheck = true});
}

void run() {
  a_rule_on_a_container_is_ordered_before_it_is_answered();
  formatting_twice_is_formatting_once();
  a_document_already_in_shape_answers_no_edit();
  a_keyword_rule_shapes_every_case_of_the_word();
  a_rule_can_shape_the_gap_before_the_first_token();
  indentation_counts_do_not_wrap();
  a_range_format_keeps_the_edits_on_its_lines();
  a_range_format_leaves_what_only_reaches_its_start();
  a_range_format_always_applies_the_root_rules();
  a_format_on_type_stays_on_the_carets_line();
  a_document_with_a_parse_error_is_not_formatted();
  a_conversion_error_does_not_withhold_formatting();
  a_fit_carries_no_options();
  a_gap_claimed_from_both_sides_keeps_the_higher_priority();
  a_kept_gap_holds_against_the_other_side();
  a_multi_name_selection_keeps_the_order_given();
  a_rule_targets_the_records_it_holds();
  a_descendant_rule_wins_over_an_ancestor_rule();
  an_append_shapes_the_margin_of_the_comment_after_it();
  a_hidden_callback_can_add_rules_after_a_deferred_append();
  comment_margins_follow_all_text_document_line_breaks();
  a_mixed_comment_margin_is_shaped_in_one_pass();
  a_block_comment_keeps_its_alignment_over_a_mixed_margin();
  mixed_comment_margins_reach_their_shape_in_one_pass();
  an_append_level_stays_while_its_target_is_a_construction();
  a_property_index_clamps_to_the_last_occurrence();
  a_multiline_comment_is_reindented();
  a_doc_comment_reaches_its_shape_in_one_pass();
  a_one_line_comment_keeps_the_stars_it_starts_with();
  a_crlf_document_keeps_its_terminators();
}

}  // namespace formatter

int main() {
  cursiv::test::count_expectations();
  completion_provider::run();
  formatter::run();
  return cursiv::test::report("edits");
}
