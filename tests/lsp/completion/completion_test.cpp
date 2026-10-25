// What a caret is offered: the word boundary a literal may not be inserted
// against, the filter's one rule, the answer behind a repair, and the caret
// against every position of the table.

#include <lsp/types.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "caret/generated/Caret/AstReflection.hpp"
#include "caret/generated/Caret/Module.hpp"
#include "caret/generated/Caret/Table.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/references/DefaultNameProvider.hpp"
#include "cursiv/core/references/DefaultScopeProvider.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/lsp/completion/DefaultCompletionProvider.hpp"
#include "cursiv/lsp/completion/DefaultFuzzyMatcher.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/test/Check.hpp"
#include "cursiv/test/ExpectedCompletion.hpp"
#include "lsp/completion/CaretLane.hpp"
#include "workspace/generated/Workspace/Module.hpp"

// What a word boundary forbids at a caret: a literal made of word characters
// cannot be inserted against a name, since the two would read as one word. The
// lexer is what judges the splice, so the two cases are told apart at the
// caret itself.
namespace default_completion_provider {

namespace {

namespace ct = cursiv::test;

struct World {
  struct Shared : cursiv::SharedLspServices {};
  struct Language : cursiv::LspServices {
    explicit Language(Shared& s)
        : cursiv::CoreServices(s), cursiv::LspServices(s) {}
  };

  Shared shared;
  Language* ws = nullptr;

  World() {
    shared.fileSystemProvider =
        std::make_unique<cursiv::EmptyFileSystemProvider>();
    cursiv::install_default_shared_lsp_services(shared);
    auto language = std::make_unique<Language>(shared);
    language->languageMetaData = {"workspace", {".ws"}, {}};
    language->parser = ws::generated::Workspace::create_parser();
    language->astReflection = ws::generated::Workspace::create_ast_reflection();
    language->nameProvider =
        std::make_unique<cursiv::DefaultNameProvider>(*language);
    language->completionProvider =
        std::make_unique<cursiv::DefaultCompletionProvider>(*language);
    cursiv::install_default_core_services(*language);
    cursiv::install_default_lsp_services(*language);
    ws = language.get();
    shared.serviceRegistry->registerServices(std::move(language));
  }
};

[[nodiscard]] bool offers(const lsp::CompletionList& list,
                          std::string_view label) {
  return std::ranges::any_of(list.items, [label](const lsp::CompletionItem& i) {
    return i.label == label;
  });
}

void offered(const World& world, std::string text,
             std::function<void(const lsp::CompletionList&)> assertion) {
  (void)ct::expectCompletion(*world.ws, ct::ExpectedCompletionCallback{
                                            .text = std::move(text),
                                            .assertion = std::move(assertion),
                                            .disposeAfterCheck = true});
}

// `extends` glued to `E` spells `Eextends`, which is not the keyword: only a
// literal with no word boundary of its own may be inserted there.
void a_caret_glued_to_a_name_is_offered_no_word() {
  const World world;
  offered(world, "entity E<|>", [](const lsp::CompletionList& list) {
    CHECK(offers(list, "{"));
    CHECK(!offers(list, "extends"));
  });
}

// One space later the boundary is back, and both offers stand.
void a_caret_past_the_space_is_offered_the_word() {
  const World world;
  offered(world, "entity E <|>", [](const lsp::CompletionList& list) {
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
  offered(world, "entity Base {} entity E extends <|>",
          [](const lsp::CompletionList& list) { CHECK(offers(list, "Base")); });
  CHECK(observed.asked);
}

}  // namespace

void run() {
  a_caret_glued_to_a_name_is_offered_no_word();
  a_caret_past_the_space_is_offered_the_word();
  a_reference_context_names_the_member_being_completed();
}

}  // namespace default_completion_provider

// Pins the rule, with the case that makes it a rule rather than a
// subsequence: `ram` must not answer `Grammar`.
namespace fuzzy_matcher {

namespace {

void a_query_anchors_on_a_word_boundary() {
  const cursiv::DefaultFuzzyMatcher matcher;

  CHECK(matcher.match("grammar", "Grammar"));    // whole, case folded
  CHECK(matcher.match("gr", "Grammar"));         // prefix
  CHECK(matcher.match("gmr", "Grammar"));        // subsequence from the start
  CHECK(matcher.match("GK", "GrammarKeyword"));  // the humps
  CHECK(matcher.match("dsp", "documentSymbolProvider"));
  CHECK(matcher.match("", "Grammar"));  // nothing typed answers all

  // The one that makes it a rule: `r`, `a`, `m` do sit in order inside
  // `Grammar`, but none of them starts a word -- a plain subsequence would let
  // almost every candidate answer almost every query.
  CHECK(!matcher.match("ram", "Grammar"));
  CHECK(!matcher.match("xyz", "Grammar"));
}

// A snake_case name breaks into words too, and the anchor may be any of them.
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
  // Matching proceeds by surrogate units for characters outside the BMP.
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

// A repair between the caret and the token before it must not silence the
// answer: what the editor's own provider hands back at a marked caret.
namespace caret_repaired {

namespace g = tcaret::generated::Caret;

namespace {

struct World {
  struct Shared : cursiv::SharedLspServices {};
  struct Language : cursiv::LspServices {
    explicit Language(Shared& s)
        : cursiv::CoreServices(s), cursiv::LspServices(s) {}
  };
  Shared shared;
  Language* lang = nullptr;

  World() {
    shared.fileSystemProvider =
        std::make_unique<cursiv::EmptyFileSystemProvider>();
    cursiv::install_default_shared_lsp_services(shared);
    auto language = std::make_unique<Language>(shared);
    language->languageMetaData = {"caret", {".caret"}, {}};
    language->parser = g::create_parser();
    language->astReflection = g::create_ast_reflection();
    language->nameProvider =
        std::make_unique<cursiv::DefaultNameProvider>(*language);
    cursiv::install_default_core_services(*language);
    cursiv::install_default_lsp_services(*language);
    lang = language.get();
    shared.serviceRegistry->registerServices(std::move(language));
  }
};

}  // namespace


namespace {

// The token before `}` was given up on by the repair, so the mode it was read
// in is not the one the leaf before it names. Judging the splice in that mode
// refuses every candidate there is, and the editor is handed nothing.
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

// `@ ~` is a k=2 cell: the engine elects an arm on the token after `~`, so a
// caret there has both open. The peek is keyed on the terminal the leaf's
// BYTES lex to, and a repair beside the cell puts bytes there that are not
// the cell's -- what it may cost is an arm, never an arm that is not one.
void a_repair_beside_a_k2_cell_offers_nothing_new() {
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
  const std::vector<cursiv::ElementId> settled =
      after_tilde("fn f() { end; @~!; }");
  CHECK(settled.size() == 2);
  for (const char* broken :
       {"fn f() { emd; @~!; }", "fn f() { emd; @~?; }", "fn f() { end; @~!, }",
        "fn f() { end; @~; }", "fn f() { @~!; "}) {
    const std::vector<cursiv::ElementId> got = after_tilde(broken);
    if (!std::ranges::includes(settled, got)) {
      std::printf("FAIL %s: the caret offers a feature the settled reading"
                  " does not\n",
                  broken);
      ++failures;
    }
  }
}

// Replaying an unwind copies an ancestor into the next
// stage. At these sizes the copy grows the frame arena; walking that
// ancestor's parents afterwards must still offer the next function.
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
  a_repair_beside_a_k2_cell_offers_nothing_new();
  an_unwind_keeps_its_parents_when_the_arena_grows(world);
}

}  // namespace caret_repaired

// What the caret offers at every position of one grammar, and where it stops.
namespace lr_completion_caret {

namespace g = tcaret::generated::Caret;

// The shape this lane stands on. Lose the k=2 cell and the arm test would
// pass on a grammar that never had two arms to lose.
static_assert(g::kTable.npeeks != 0);

void run() {
  run_caret_lane(CaretDoor{&g::create_parser, &g::create_ast_reflection});
}

}  // namespace lr_completion_caret

int main() {
  cursiv::test::count_expectations();
  default_completion_provider::run();
  fuzzy_matcher::run();
  caret_repaired::run();
  lr_completion_caret::run();
  return cursiv::test::report("completion");
}
