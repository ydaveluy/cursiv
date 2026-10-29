// What the editor is handed to write -- completion items and formatting edits --
// over the workspace fixture, which a second lane builds with its stamp's row
// forced narrow.

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

#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/completion/CompletionContext.hpp"
#include "cursiv/lsp/completion/DefaultCompletionProvider.hpp"
#include "cursiv/lsp/formatting/Formatter.hpp"
#include "cursiv/lsp/formatting/FormattingEngine.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "cursiv/test/Check.hpp"
#include "cursiv/test/ExpectFormatting.hpp"
#include "cursiv/test/ExpectedCompletion.hpp"
#include "cursiv/test/ParseHelper.hpp"
#include "support/Editor.hpp"
#include "support/WorkspaceWorld.hpp"
#include "workspace/lsp/LspModule.hpp"
#include "workspace/lsp/LspServices.hpp"
#include "workspace/lsp/WorkspaceFormatter.hpp"

// What the editor is handed at a caret. `follow_test` pins the grammar's
// answer; this pins the storey above it -- where the caret can sit inside a
// half-typed word, what the item replaces, what is filtered out, and the one
// answer the grammar alone cannot give: the names a reference may hold.
namespace completion_provider {

namespace {

namespace ct = cursiv::test;

// A language that wants its punctuation back out of the list, which is the
// only thing `filterKeyword` is there for.
struct OffersOnlyWords final : cursiv::DefaultCompletionProvider {
  using DefaultCompletionProvider::DefaultCompletionProvider;
  bool filterKeyword(const cursiv::CompletionContext&,
                     std::string_view keyword) const override {
    return keyword.find_first_not_of("{}()[],;:=<>") != std::string_view::npos;
  }
};

// The workspace fixture and not a bare grammar: its members hold a cross
// reference, and the scope's answer is half of what this door owes an editor.
struct World : support::LspWorkspace {
  explicit World(bool onlyWords = false)
      : WorkspaceWorld([onlyWords](cursiv::LspServices& language) {
          if (onlyWords)
            language.completionProvider =
                std::make_unique<OffersOnlyWords>(language);
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

// The document without its caret marker: what an editor actually holds, and
// what an edit is applied to.
[[nodiscard]] std::string text_of(std::string_view marked) {
  std::string out(marked);
  const std::size_t at = out.find("<|>");
  if (at != std::string::npos) out.erase(at, 3);
  return out;
}

// The editor's own gesture, done here: a range is a line/character pair, so it
// is walked to an offset the same way a client walks it.
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

// The same, at the end of a broken text: the recovery always leaves a tree
// now, and the answer is asked of it.
[[nodiscard]] lsp::CompletionList offeredWithoutATree(const World& world,
                                                      std::string text) {
  const auto caret = static_cast<lsp::Uint>(text.size());
  const std::shared_ptr<cursiv::Document> document =
      ct::parseHelper(*world.ws, std::move(text));
  return world.ws->completionProvider->getCompletion(
      *document, lsp::Position{0, caret}, cursiv::kNoCancel);
}

// The empty document offers the entry rule's first words, and nothing else.
void an_empty_document_offers_what_may_begin_it() {
  const World world;
  const lsp::CompletionList list = offeredWithoutATree(world, "");

  CHECK(offers(list, "datatype"));
  CHECK(offers(list, "entity"));
  CHECK(offers(list, "package"));
}

// The question is not asked at the caret. A caret inside a half-typed word sits
// in the middle of a token the author is still writing: asked there, the answer
// would be what follows that token. It is asked at the word's start, and what
// has been typed becomes the filter.
void a_half_typed_word_is_filtered_not_followed() {
  const World world;
  const lsp::CompletionList list = offeredWithoutATree(world, "ent");

  CHECK(offers(list, "entity"));
  // ...and the words that do not begin with what was typed are gone.
  CHECK(!offers(list, "datatype"));
  CHECK(!offers(list, "package"));
}

// ...and the item replaces the word, rather than appending to it. Without the
// edit an editor would leave `ententity`.
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
  // Nothing is typed past the caret here, so both ranges say the same thing.
  CHECK(edit.replace.start.character == 0);
  CHECK(edit.replace.end.character == 3);
}

// A caret in the middle of a word covers the whole word. `Str|ng` accepted as
// `String` must leave `String`, not `Stringng`: the insert range stops at the
// caret and keeps the tail, the replace range takes the word's own end -- the
// editor picks via `editor.suggest.insertMode`.
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
            // What is typed past the caret is kept...
            CHECK(edit.insert.start.character == 15);
            CHECK(edit.insert.end.character == 18);
            // ...or taken, and the whole word goes.
            CHECK(edit.replace.start.character == 15);
            CHECK(edit.replace.end.character == 20);
          });
}

// The edit is applied, and the document that comes out is the assertion. Two
// ranges mean two possible documents, and only one of them is what an author
// meant: the caret in the middle of a word is exactly where they part.
void an_applied_edit_leaves_the_word_the_author_meant() {
  const auto check = [](std::string source, std::string_view label,
                        std::string_view wantReplace,
                        std::string_view wantInsert) {
    const World world;
    // Taken before the marked text is handed over: what the editor holds.
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
  // A keyword the caret sits inside: replace gives the word, insert doubles
  // its tail -- which is the `publicblic` an editor in insert mode shows.
  check("ent<|>ity E { }", "entity", "entity E { }", "entityity E { }");
  // ...and a cross-reference candidate says the same.
  check("datatype String\nentity E { f : Str<|>ng }", "String",
        "datatype String\nentity E { f : String }",
        "datatype String\nentity E { f : Stringng }");
}

// A keyword is offered mid-word too, and the boundary is asked at the word's
// end rather than at the caret: an accepted item replaces the whole word, so
// what the lexer must still read the candidate against is the text past the
// word, not the tail the edit is about to take.
void a_keyword_is_offered_from_inside_a_word() {
  const World world;
  offered(*world.ws, "ent<|>ity E {}", [](const lsp::CompletionList& list) {
    CHECK(offers(list, "entity"));
  });
  // ...and the edit takes the word whole, so accepting leaves `entity`.
  offered(*world.ws, "ent<|>ity E {}", [](const lsp::CompletionList& list) {
    const lsp::CompletionItem* const item = find(list, "entity");
    CHECK(item != nullptr);
    if (item == nullptr || !item->textEdit.has_value()) return;
    const auto& edit = std::get<lsp::InsertReplaceEdit>(*item->textEdit);
    CHECK(edit.insert.start.character == 0 && edit.insert.end.character == 3);
    CHECK(edit.replace.start.character == 0 && edit.replace.end.character == 6);
  });
}

// ...and a word no keyword begins stays silent: widening the boundary must not
// turn the mid-word caret into noise.
void a_word_no_keyword_begins_offers_no_keyword() {
  const World world;
  offered(*world.ws, "zq<|>xw E {}", [](const lsp::CompletionList& list) {
    CHECK(!offers(list, "entity"));
    CHECK(!offers(list, "datatype"));
    CHECK(!offers(list, "package"));
  });
}

// A keyword spelling no letter is offered like any other: the grammar states
// it there, and only a language says otherwise, per keyword.
void a_keyword_with_no_letter_is_offered() {
  const World world;
  offered(*world.ws, "entity E <|>", [](const lsp::CompletionList& list) {
    CHECK(offers(list, "extends"));
    CHECK(offers(list, "{"));
  });
  const World words(true);
  offered(*words.ws, "entity E <|>", [](const lsp::CompletionList& list) {
    CHECK(offers(list, "extends"));
    CHECK(!offers(list, "{"));
  });
}

// A keyword answers what has been typed through the same matcher as a name:
// a subsequence anchored on a word start is enough.
void a_keyword_is_matched_like_a_name() {
  const World world;
  const lsp::CompletionList list = offeredWithoutATree(world, "pkg");
  CHECK(offers(list, "package"));
  CHECK(!offers(list, "entity"));
  CHECK(!offers(list, "datatype"));
}

// What an item says of itself: a keyword is a `Keyword`, and it sorts after
// every name.
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

// A word that answers nothing falls back to the caret. After `entity E` the
// anchor sits on `E`, and what may stand there is the name -- never offered
// once something is typed, since the author is writing it, not choosing it.
// The anchored question is then empty, so the caret's own is asked instead.
void a_word_that_answers_nothing_asks_at_the_caret() {
  // What may follow the name spells no letter; a word glued to it is
  // `completion_boundary_test`'s.
  const World world;
  offered(*world.ws, "entity E<|>", [](const lsp::CompletionList& list) {
    CHECK(offers(list, "{"));
    // ...and it replaces nothing: the name it sits behind is not being
    // rewritten.
    const lsp::CompletionItem* const brace = find(list, "{");
    CHECK(brace != nullptr);
    if (brace == nullptr) return;
    CHECK(brace->textEdit.has_value());
    if (!brace->textEdit.has_value()) return;
    const auto& edit = std::get<lsp::InsertReplaceEdit>(*brace->textEdit);
    CHECK(edit.insert.start.character == edit.insert.end.character);
    CHECK(edit.replace.start.character == edit.replace.end.character);
  });
}

// The scope answers where a member holds a reference, which is the one thing
// the grammar alone cannot say: `Feature::type` names a type, and the offer is
// the visible declarations rather than the word "ID".
void a_reference_offers_the_names_in_scope() {
  const World world;
  offered(*world.ws, "datatype String\ndatatype Int\nentity E { f : <|>",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "String"));
            CHECK(offers(list, "Int"));
            // ...and the token's own name is not what an editor wants there.
            CHECK(!offers(list, "ID"));
          });
}

// ...and the same caret with the record still closed after it: what follows the
// hole is what a real edit looks like, and the state read there must be the
// one before the hole and not the one the repair walked into.
void a_reference_offers_the_names_in_scope_mid_document() {
  const World world;
  offered(*world.ws,
          "datatype String\ndatatype Int\nentity E {\n  f : <|>\n}\n",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "String"));
            CHECK(offers(list, "Int"));
          });
}

// Nothing is offered inside a comment -- a decree, not something the
// grammar's continuations happen to lack. Every way in is closed: with a
// word under the caret and without, block comment and line comment alike.
void a_caret_inside_a_comment_is_offered_nothing() {
  const World world;
  // ...with a word under the caret, and without one.
  offered(*world.ws, "entity E {}\n/* aa<|>a */\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(list.items.empty()); });
  offered(*world.ws, "entity E {}\n/* --<|>- */\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(list.items.empty()); });
  // ...in the opener itself, which is inside the leaf as much as its body is.
  offered(*world.ws, "entity E {}\n/<|>* aaa */\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(list.items.empty()); });
  offered(*world.ws, "entity E {}\n/*<|> aaa */\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(list.items.empty()); });
  // ...and a line comment says the same.
  offered(*world.ws, "entity E {}\n// aa<|>a\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(list.items.empty()); });
  // A comment the author has not closed matches no terminal at all, so the
  // repair keeps its bytes as spans it threw away -- unread text either way.
  offered(*world.ws, "entity E {}\n/* aa<|>a",
          [](const lsp::CompletionList& list) { CHECK(list.items.empty()); });
}

// ...and the two boundaries are not inside it: what stands right after a
// comment closes, and right before one opens, is the grammar's to answer.
void the_edges_of_a_comment_still_answer() {
  const World world;
  offered(*world.ws, "entity E {}\n/* aaa */<|>\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(!list.items.empty()); });
  offered(*world.ws, "entity E {}\n<|>/* aaa */\nentity F {}",
          [](const lsp::CompletionList& list) { CHECK(!list.items.empty()); });
}

// A candidate carries the documentation of its declaration, rendered; one
// whose author wrote nothing carries nothing.
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

// A terminal is never an offer. Where the grammar wants a fresh name the only
// honest answer is nothing: an item labelled `ID` inserts the two letters `ID`
// when the author accepts it, and the bytes a terminal matches are not ours to
// guess. A keyword is offered because the grammar spells it out.
void a_terminal_is_never_offered_by_name() {
  const World world;
  offered(*world.ws, "entity <|>",
          [](const lsp::CompletionList& list) { CHECK(!offers(list, "ID")); });
  offered(*world.ws, "entity E<|>",
          [](const lsp::CompletionList& list) { CHECK(!offers(list, "ID")); });
}

// ...filtered by what has been typed, through the same anchor as a keyword.
void a_reference_is_filtered_by_what_is_typed() {
  const World world;
  offered(*world.ws, "datatype String\ndatatype Int\nentity E { f : St<|>",
          [](const lsp::CompletionList& list) {
            CHECK(offers(list, "String"));
            CHECK(!offers(list, "Int"));
          });
}

// One answer, once. A grammar reaches the same word by more than one path --
// `datatype` opens a declaration whether or not a package encloses it -- and an
// editor showing it twice is a defect the walk cannot prevent on its own.
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

// Two declarations of the same name are two offers when their types differ,
// and the detail is what makes them distinguishable: the default kind provider
// answers one constant, so without the declared type on the row the dedup key
// would collapse the pair into one entry naming neither.
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

// A caret past a repair still answers: the tree a broken document leaves is
// what the walk reads, and it reaches the caret because an engaged frame closes
// incomplete rather than unwinding.
void a_broken_document_still_answers() {
  const World world;
  offered(*world.ws, "entity E { f : String\nentity <|>",
          [](const lsp::CompletionList& list) { CHECK(!list.items.empty()); });
}

// Cancellation is the caller's, and it is honoured rather than ignored: a
// completion is asked on every keystroke and the one before last is waste.
void a_cancelled_request_gives_up() {
  const World world;
  const std::shared_ptr<cursiv::Document> document =
      ct::parseDocument(*world.ws, "datatype String\nentity E { f : ");
  std::stop_source source;
  source.request_stop();

  bool cancelled = false;
  try {
    (void)world.ws->completionProvider->getCompletion(
        *document, lsp::Position{}, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
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
  a_keyword_with_no_letter_is_offered();
  a_keyword_is_matched_like_a_name();
  a_keyword_says_what_it_is_and_sorts_after_the_names();
  a_word_that_answers_nothing_asks_at_the_caret();
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

// The formatter, exercised through the workspace language: its own rules pin
// the engine's contract, and a lane that needs another rule writes it.
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

  // A fresh URI per text: the lanes below format the same source twice and the
  // second document must be a document of its own.
  cursiv::Document* built(std::string text) {
    return support::built(shared,
                          "file:///w/d" + std::to_string(nextUri++) + ".ws",
                          std::move(text));
  }
};

// The workspace language with its formatter swapped for a test-local one: the
// lanes that pin a contract the shipped rules never exercise write the rule
// they need.
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

// A blank line and one level: the move the lanes below hang on a selection
// to see which record it landed on.
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

// A rule on a record that HAS CHILDREN: the walk reaches the record before it
// descends, so this gap, which sits past the whole subtree, is emitted before
// everything inside it. Nothing else claims either gap, so both edits survive.
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

struct LeadingSpaceFormatter final : ws::WorkspaceFormatter {
  using WorkspaceFormatter::WorkspaceFormatter;
  void formatWorkspace(cursiv::FormattingBuilder&,
                       const ws::ast::Workspace*) const override {}
  void formatEntity(cursiv::FormattingBuilder& builder,
                    const ws::ast::Entity* entity) const override {
    builder.getNodeFormatter(entity).keyword("entity").prepend(spaces(2));
  }
};

// Two spaces per level, spaces not tabs: the indentation the engine writes is
// the client's options, so the lanes below can name the columns they expect.
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

// The client's half: the edits, applied to the text they were measured
// against.
[[nodiscard]] std::string applied(const cursiv::Document& document,
                                  const std::vector<lsp::TextEdit>& edits) {
  std::vector<cursiv::TextEdit> converted;
  converted.reserve(edits.size());
  for (const lsp::TextEdit& edit : edits)
    converted.push_back({cursiv::from_lsp(edit.range), edit.newText});
  return cursiv::TextDocument::applyEdits(document.textDocument(), converted);
}

using support::kMessy;

// ...and the shape it reaches: the language's own rule, no space before the
// colon and one after it.
constexpr std::string_view kShaped =
    "entity Person {\n"
    "  name: String\n"
    "  age: String\n"
    "}\n"
    "datatype String\n";

// The fixed point, which is what makes the answer a shape and not a diff: the
// edits move the document somewhere, and formatting that somewhere moves
// nothing.
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

// The same text, already in shape, answers the empty list -- the contract an
// editor is written against rather than a coincidence of the rules.
void a_document_already_in_shape_answers_no_edit() {
  World world;
  const cursiv::Document* const messy = world.built(std::string(kMessy));
  const std::string once = applied(*messy, format(world, *messy));

  const cursiv::Document* const clean = world.built(once);
  CHECK(format(world, *clean).empty());
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
    (void)cursiv::indentation_text(std::numeric_limits<std::int32_t>::max(),
                                   chosen);
  } catch (const std::length_error&) {
    rejected = true;
  }
  CHECK(rejected);

  const cursiv::Cst cst;
  cursiv::FormattingActions actions(0);
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

// A selection format keeps the edits whose lines overlap the selected lines.
// The gap that closes the last selected line is kept whole, even where it
// reaches the next line; what no edit may do is begin on a line past the
// selection.
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

// A repaired parse describes text the author did not write, so reprinting it
// would discard theirs: the answer is no edits at all.
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

// A rule may target nodes it already holds and records it already selected,
// outside the property and keyword vocabulary.
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
  cursiv::FormattingActions actions(cst.size());
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
  const auto edits = cursiv::run_formatting(
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
    cursiv::FormattingActions actions(cst.size());
    for (cursiv::NodeId id = 0; id < cst.size(); ++id) {
      if (cst.childless(id) && cst.text(id) == "String") {
        actions.collect(id, cursiv::FormattingMode::Append,
                        cursiv::IndentActionBuilder{}());
        break;
      }
    }
    const auto edits = cursiv::run_formatting(
        document, cst, options(), std::nullopt, actions,
        [](cursiv::NodeId, std::int32_t, std::string_view) {}, cursiv::kNoCancel);
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
  cursiv::FormattingActions actions(cst.size());
  for (cursiv::NodeId id = 0; id < cst.size(); ++id) {
    if (cst.childless(id) && cst.text(id) == "String") {
      actions.collect(id, cursiv::FormattingMode::Append,
                      cursiv::IndentActionBuilder{}());
      break;
    }
  }
  const auto edits = cursiv::run_formatting(
      document, cst, options(), std::nullopt, actions,
      [](cursiv::NodeId, std::int32_t, std::string_view) {}, cursiv::kNoCancel);
  CHECK(applied(document, edits) ==
        "datatype String\n  /* c\n   * d\n   */\nentity Person {}\n");
}

// The fixed point over margins mixing tabs and spaces, in both units: one pass
// gives a comment its margin, and a second pass finds nothing to do.
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

// An `append` targets the next node, a whole construction when one follows,
// and a level it carries stays in force until that construction's parent
// closes: `{`.append(indent) lands the first feature and the closing brace
// one level in, and leaves the second feature, which no rule names, alone.
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

// An index past the last occurrence selects the last one, never nothing.
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

// A keystroke formats the caret's line, whole. The range runs from the start
// of the line to the caret, but edits are kept by line, as for a selection:
// text past the caret is formatted too, and so is a gap that crosses into the
// line. The base advertises no trigger character, so no client sends the
// request until a language names one.
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

// A failed value conversion leaves the tree intact, so the document is still
// formatted, whole or by range: only a syntax error withholds the edits.
void a_conversion_error_does_not_withhold_formatting() {
  World world;
  cursiv::Document* const document =
      world.built("entity   Person{ name   :String }\n");
  // The workspace language converts nothing that can fail: the diagnostic is
  // the one a failed conversion lays.
  cursiv::ParseDiagnostic failed;
  failed.kind = cursiv::DiagnosticKind::conversion_error;
  failed.begin = 17;
  failed.end = 21;
  document->parseResult.diagnostics.push_back(failed);
  CHECK(!document->parseRecovered());
  CHECK(!format(world, *document).empty());
  CHECK(!formatRange(world, *document, {{0, 0}, {0, 33}}).empty());
}

// A comment is anchored by the newlines around it, and re-indented with the
// block it sits in -- its text preserved, never reprinted from the tree.
void a_multiline_comment_is_reindented() {
  World world;
  // The author's words, kept; the margin, redrawn at the block's level rather
  // than left at column zero.
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

  // ...and it is a fixed point like everything else.
  const cursiv::Document* const formatted =
      world.built(std::string(kCommented));
  CHECK(format(world, *formatted).empty());
}

// The terminator is the document's, not the platform's: a file written with
// CRLF keeps CRLF in everything the engine inserts, a rewritten comment's own
// line breaks included.
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
// engine owes the client an ordered list all the same.
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
  a_rule_can_shape_the_gap_before_the_first_token();
  indentation_counts_do_not_wrap();
  a_range_format_keeps_the_edits_on_its_lines();
  a_format_on_type_stays_on_the_carets_line();
  a_document_with_a_parse_error_is_not_formatted();
  a_conversion_error_does_not_withhold_formatting();
  a_fit_carries_no_options();
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
  a_crlf_document_keeps_its_terminators();
}

}  // namespace formatter

int main() {
  cursiv::test::count_expectations();
  completion_provider::run();
  formatter::run();
  return cursiv::test::report("edits");
}
