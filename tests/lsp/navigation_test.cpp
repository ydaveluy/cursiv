#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "cursiv/parser/Parser.hpp"
#include "cursiv/references/NameProvider.hpp"
#include "cursiv/references/References.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/hierarchy/HierarchyProviderSupport.hpp"
#include "cursiv/hover/HoverProvider.hpp"
#include "cursiv/navigation/DefinitionProvider.hpp"
#include "cursiv/navigation/DocumentHighlightProvider.hpp"
#include "cursiv/navigation/ReferencesProvider.hpp"
#include "cursiv/navigation/RenameProvider.hpp"
#include "cursiv/runtime/LanguageServer.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/symbols/DefaultWorkspaceSymbolProvider.hpp"
#include "cursiv/symbols/DocumentSymbolProvider.hpp"
#include "cursiv/symbols/WorkspaceSymbolProvider.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/ExpectedHover.hpp"
#include "cursiv/ExpectedRename.hpp"
#include "cursiv/ExpectedSymbols.hpp"
#include "dualll/generated/Ll/Module.hpp"
#include "duallr/generated/Lr/Module.hpp"
#include "multiref/generated/MultiG/Module.hpp"
#include "support/WorkspaceWorld.hpp"
#include "workspace/lsp/LspModule.hpp"
#include "workspace/lsp/LspServices.hpp"
#include "workspace/generated/Workspace/Module.hpp"

namespace {

// The workspace language as its module installs it, with qualified names.
struct ModuleWorld {
  ws::WorkspaceLanguage services = ws::create_lsp_services();
  ws::WorkspaceSharedServices& shared = *services.shared;

  std::vector<cursiv::Document*> built(
      std::vector<std::pair<std::string, std::string>> sources) const {
    return support::built(shared, std::move(sources));
  }
};

}  // namespace

namespace definition_provider {

namespace {

template <std::unique_ptr<cursiv::Parser> (*MakeParser)(),
          std::unique_ptr<cursiv::AstReflection> (*MakeReflection)()>
struct World : support::World<cursiv::SharedLspServices> {
  cursiv::LspServices* language;

  World(std::string id, std::string extension)
      : language(&support::add_language<cursiv::LspServices>(
            shared, {std::move(id), {std::move(extension)}, {}}, MakeParser(),
            MakeReflection())) {}

  [[nodiscard]] std::optional<std::vector<lsp::LocationLink>> at(
      const cursiv::Document& document, lsp::Uint line,
      lsp::Uint character) const {
    return language->definitionProvider->getDefinition(
        document, lsp::Position{line, character}, cursiv::kNoCancel);
  }
};

using Workspace = World<&ws::generated::Workspace::create_parser,
                        &ws::generated::Workspace::create_ast_reflection>;
using MultiRef = World<&fmulti::generated::MultiG::create_parser,
                       &fmulti::generated::MultiG::create_ast_reflection>;

//  0         1         2
//  012345678901234567890123
constexpr std::string_view kLib = "datatype String\n";
constexpr std::string_view kUse = "entity E { f : String }\n";

[[nodiscard]] bool spans(const lsp::Range& range, lsp::Uint line,
                         lsp::Uint from, lsp::Uint to) {
  return range.start.line == line && range.start.character == from &&
         range.end.line == line && range.end.character == to;
}

// The three ranges of a link: the declaration a peek previews, the name the
// caret goes to inside it, and the origin underlined under the mouse.
void a_link_previews_the_declaration_and_selects_its_name() {
  const Workspace world("workspace", ".ws");
  const std::vector<cursiv::Document*> documents =
      world.built({{"file:///w/a.ws", std::string(kLib) + std::string(kUse)}});
  const std::optional<std::vector<lsp::LocationLink>> links =
      world.at(*documents[0], 1, 17);
  CHECK(links.has_value() && links->size() == 1);
  if (!links.has_value() || links->size() != 1) return;
  const lsp::LocationLink& link = links->front();
  CHECK(link.targetUri.toString() == "file:///w/a.ws");
  CHECK(spans(link.targetRange, 0, 0, 15));
  CHECK(spans(link.targetSelectionRange, 0, 9, 15));
  CHECK(link.originSelectionRange.has_value() &&
        spans(*link.originSelectionRange, 1, 15, 21));
}

void a_link_reaches_into_another_document() {
  const Workspace world("workspace", ".ws");
  const std::vector<cursiv::Document*> documents =
      world.built({{"file:///w/a.ws", std::string(kLib)},
                   {"file:///w/b.ws", std::string(kUse)}});
  CHECK(documents.size() == 2);
  if (documents.size() != 2) return;
  const std::optional<std::vector<lsp::LocationLink>> links =
      world.at(*documents[1], 0, 17);
  CHECK(links.has_value() && links->size() == 1);
  if (!links.has_value() || links->size() != 1) return;
  const lsp::LocationLink& link = links->front();
  CHECK(link.targetUri.toString() == "file:///w/a.ws");
  CHECK(spans(link.targetRange, 0, 0, 15));
  CHECK(spans(link.targetSelectionRange, 0, 9, 15));
  CHECK(link.originSelectionRange.has_value() &&
        spans(*link.originSelectionRange, 0, 15, 21));
}

// One link per member of the group, all from the same origin.
void a_multi_reference_links_every_member() {
  const MultiRef world("multiref", ".mref");
  const std::vector<cursiv::Document*> documents =
      world.built({{"file:///w/q.mref", "SELECT t FROM t, t"}});
  //                                     0123456789012345678
  std::optional<std::vector<lsp::LocationLink>> links =
      world.at(*documents[0], 0, 7);
  CHECK(links.has_value() && links->size() == 2);
  if (!links.has_value() || links->size() != 2) return;
  std::ranges::sort(*links, {}, [](const lsp::LocationLink& link) {
    return link.targetSelectionRange.start.character;
  });
  CHECK(spans((*links)[0].targetSelectionRange, 0, 14, 15));
  CHECK(spans((*links)[1].targetSelectionRange, 0, 17, 18));
  for (const lsp::LocationLink& link : *links)
    CHECK(link.originSelectionRange.has_value() &&
          spans(*link.originSelectionRange, 0, 7, 8));
}

void a_hierarchy_item_keeps_the_selected_overload() {
  const MultiRef world("multiref", ".mref");
  const auto documents =
      world.built({{"file:///w/q.mref", "SELECT t FROM t, t"}});
  const auto targets = cursiv::hierarchy_nodes_at(
      *world.language->references, *documents[0], {0, 7}, cursiv::kNoCancel);
  CHECK(targets.size() == 2);
  if (targets.size() != 2) return;
  for (std::size_t index = 0; index < targets.size(); ++index) {
    const auto name =
        world.language->nameProvider->getNameNode(*targets[index]);
    CHECK(name.has_value());
    if (!name) continue;
    const auto position =
        documents[0]->textDocument().positionAt(name->begin());
    const auto* selected = cursiv::hierarchy_node_of_item(
        *world.shared.documents, *world.language->references,
        lsp::Uri::parse(documents[0]->uri),
        {static_cast<lsp::Uint>(position.line),
         static_cast<lsp::Uint>(position.character)},
        cursiv::kNoCancel);
    CHECK(selected == targets[index]);
  }
}

// Nothing linked is `nullopt`, never an empty list: a keyword denotes nothing.
void nothing_is_linked_from_a_keyword() {
  const Workspace world("workspace", ".ws");
  const std::vector<cursiv::Document*> documents =
      world.built({{"file:///w/a.ws", std::string(kLib)}});
  CHECK(!world.at(*documents[0], 0, 2).has_value());
}

}  // namespace

void run() {
  a_link_previews_the_declaration_and_selects_its_name();
  a_link_reaches_into_another_document();
  a_multi_reference_links_every_member();
  a_hierarchy_item_keeps_the_selected_overload();
  nothing_is_linked_from_a_keyword();
}

}  // namespace definition_provider

namespace document_highlight_provider {

namespace {

struct World : ModuleWorld {
  [[nodiscard]] std::vector<lsp::DocumentHighlight> at(
      const cursiv::Document& document, lsp::Uint line,
      lsp::Uint character) const {
    return services.language.documentHighlightProvider->getDocumentHighlight(
        document, lsp::Position{line, character}, cursiv::kNoCancel);
  }
};

//  0         1         2
//  012345678901234567890123
constexpr std::string_view kLib = "datatype String\n";
constexpr std::string_view kUse =
    "entity E { f : String }\n"
    "entity F { g : String }\n";

[[nodiscard]] bool spans(const lsp::DocumentHighlight& highlight,
                         lsp::Uint line, lsp::Uint from, lsp::Uint to) {
  return highlight.range.start.line == line &&
         highlight.range.start.character == from &&
         highlight.range.end.line == line &&
         highlight.range.end.character == to;
}

// No kind is set, so a client draws the same highlight on the declaration and
// the reference.
void a_highlight_is_a_range_and_nothing_more() {
  const World world;
  const std::vector<cursiv::Document*> documents =
      world.built({{"file:///w/a.ws", std::string(kLib) + std::string(kUse)}});
  const std::vector<lsp::DocumentHighlight> painted =
      world.at(*documents[0], 0, 10);
  CHECK(painted.size() == 3);
  if (painted.size() != 3) return;
  CHECK(spans(painted[0], 0, 9, 15));
  CHECK(spans(painted[1], 1, 15, 21));
  CHECK(spans(painted[2], 2, 15, 21));
  for (const lsp::DocumentHighlight& highlight : painted)
    CHECK(!highlight.kind.has_value());
}

// Only this document's occurrences, in reading order.
void a_declaration_elsewhere_is_not_painted_here() {
  const World world;
  const std::vector<cursiv::Document*> documents =
      world.built({{"file:///w/a.ws", std::string(kLib)},
                   {"file:///w/b.ws", std::string(kUse)}});
  CHECK(documents.size() == 2);
  if (documents.size() != 2) return;
  const std::vector<lsp::DocumentHighlight> painted =
      world.at(*documents[1], 1, 16);
  CHECK(painted.size() == 2);
  if (painted.size() != 2) return;
  CHECK(spans(painted[0], 0, 15, 21));
  CHECK(spans(painted[1], 1, 15, 21));
}

}  // namespace

void run() {
  a_highlight_is_a_range_and_nothing_more();
  a_declaration_elsewhere_is_not_painted_here();
}

}  // namespace document_highlight_provider

namespace references_provider {

namespace {

struct World : ModuleWorld {
  [[nodiscard]] std::vector<lsp::Location> at(const cursiv::Document& document,
                                              lsp::Uint line,
                                              lsp::Uint character,
                                              bool includeDeclaration) const {
    return services.language.referencesProvider->findReferences(
        document, lsp::Position{line, character}, includeDeclaration,
        cursiv::kNoCancel);
  }
};

//  0         1         2
//  012345678901234567890123
constexpr std::string_view kLib = "datatype String\n";
constexpr std::string_view kUse =
    "entity E { f : String }\n"
    "entity F { g : String }\n";

[[nodiscard]] bool names(const lsp::Location& location, std::string_view uri,
                         lsp::Uint line, lsp::Uint from, lsp::Uint to) {
  return location.uri.toString() == uri && location.range.start.line == line &&
         location.range.start.character == from &&
         location.range.end.line == line && location.range.end.character == to;
}

// The declaring document first, then each referencing one, each by offset,
// whichever document asked.
void the_answer_crosses_documents_in_reading_order() {
  const World world;
  const std::vector<cursiv::Document*> documents =
      world.built({{"file:///w/a.ws", std::string(kLib)},
                   {"file:///w/b.ws", std::string(kUse)}});
  CHECK(documents.size() == 2);
  if (documents.size() != 2) return;

  const std::vector<lsp::Location> fromDeclaration =
      world.at(*documents[0], 0, 10, true);
  const std::vector<lsp::Location> fromReference =
      world.at(*documents[1], 1, 16, true);
  for (const std::vector<lsp::Location>* found :
       {&fromDeclaration, &fromReference}) {
    CHECK(found->size() == 3);
    if (found->size() != 3) continue;
    CHECK(names((*found)[0], "file:///w/a.ws", 0, 9, 15));
    CHECK(names((*found)[1], "file:///w/b.ws", 0, 15, 21));
    CHECK(names((*found)[2], "file:///w/b.ws", 1, 15, 21));
  }
}

// Without `includeDeclaration`, only the referencing sites, even when asked
// from the declaration's own name.
void the_declaration_joins_only_when_asked() {
  const World world;
  const std::vector<cursiv::Document*> documents =
      world.built({{"file:///w/a.ws", std::string(kLib)},
                   {"file:///w/b.ws", std::string(kUse)}});
  CHECK(documents.size() == 2);
  if (documents.size() != 2) return;

  const std::vector<lsp::Location> found =
      world.at(*documents[0], 0, 10, false);
  CHECK(found.size() == 2);
  if (found.size() != 2) return;
  CHECK(names(found[0], "file:///w/b.ws", 0, 15, 21));
  CHECK(names(found[1], "file:///w/b.ws", 1, 15, 21));
}

void distinct_ranges_at_one_start_are_preserved() {
  World world;
  const auto documents = world.built({{"file:///w/a.ws", std::string(kLib)}});
  const auto declaration = world.services.language.references->findDeclaration(
      *documents[0], 10, cursiv::kNoCancel);
  CHECK(declaration.targets.size() == 1);
  if (declaration.targets.size() != 1) return;
  struct SharedStartReferences final : cursiv::References {
    cursiv::Declaration declaration;
    cursiv::DocumentId document;
    SharedStartReferences(cursiv::Declaration selected,
                          cursiv::DocumentId source)
        : declaration(std::move(selected)), document(source) {}
    cursiv::Declaration findDeclaration(
        const cursiv::Document&, cursiv::TextOffset,
        const cursiv::CancellationToken&) const override {
      return declaration;
    }
    std::vector<cursiv::ReferenceDescription> findReferences(
        const cursiv::AstNode&, const cursiv::FindReferencesOptions&,
        const cursiv::CancellationToken&) const override {
      return {
          {.sourceDocumentId = document, .sourceBegin = 9, .sourceEnd = 15},
          {.sourceDocumentId = document, .sourceBegin = 9, .sourceEnd = 12},
          {.sourceDocumentId = document, .sourceBegin = 9, .sourceEnd = 15}};
    }
  };
  world.services.language.references =
      std::make_unique<SharedStartReferences>(declaration, documents[0]->id);
  const auto found = world.at(*documents[0], 0, 10, false);
  CHECK(found.size() == 2);
  if (found.size() != 2) return;
  CHECK(names(found[0], "file:///w/a.ws", 0, 9, 12));
  CHECK(names(found[1], "file:///w/a.ws", 0, 9, 15));
}

}  // namespace

void run() {
  the_answer_crosses_documents_in_reading_order();
  the_declaration_joins_only_when_asked();
  distinct_ranges_at_one_start_are_preserved();
}

}  // namespace references_provider

namespace rename_provider {

namespace {

namespace ct = cursiv::test;

// The workspace language: references cross documents, and identifiers exclude
// the keywords.
struct World : support::LspWorkspace {
  [[nodiscard]] std::optional<lsp::WorkspaceEdit> rename(
      const cursiv::Document& document, lsp::Uint line, lsp::Uint character,
      std::string_view newName) const {
    return ws->renameProvider->rename(
        document, lsp::Position{line, character}, newName, cursiv::kNoCancel);
  }
};

// The workspace module's rename, where a package's members have qualified
// names: renaming the package rewrites their sites too.
struct Module : ModuleWorld {
  [[nodiscard]] std::optional<lsp::WorkspaceEdit> rename(
      const cursiv::Document& document, lsp::Uint line, lsp::Uint character,
      std::string_view newName) const {
    return services.language.renameProvider->rename(
        document, lsp::Position{line, character}, newName, cursiv::kNoCancel);
  }
};

// The edits filed under one URI, or nothing.
[[nodiscard]] std::vector<lsp::TextEdit> editsFor(const lsp::WorkspaceEdit& all,
                                                  std::string_view uri) {
  if (!all.changes.has_value()) return {};
  for (const auto& [key, edits] : *all.changes)
    if (key.toString() == uri) return edits;
  return {};
}

//  0         1         2
//  0123456789012345678901
constexpr std::string_view kLib = "datatype String\n";
constexpr std::string_view kUse = "entity E { f : String }\n";

// A keyword names nothing: `nullopt` tells the client not to open a rename
// box.
void a_keyword_is_not_renamable() {
  const World world;
  (void)ct::expectRename(*world.ws, {.text = "dat<|>atype String\n",
                                           .newName = "Text",
                                           .nothingUnderCaret = true,
                                           .disposeAfterCheck = true});
  // The name beside it is renamable.
  (void)ct::expectRename(*world.ws, {.text = "datatype <|S<|>tring|>\n",
                                           .newName = "Text",
                                           .disposeAfterCheck = true});
}

void a_rename_crosses_the_documents_that_name_it() {
  const World world;
  const std::vector<cursiv::Document*> documents =
      world.built({{"file:///w/a.ws", std::string(kLib)},
                   {"file:///w/b.ws", std::string(kUse)}});
  CHECK(documents.size() == 2);
  if (documents.size() != 2) return;

  // Asked from the reference, in the document that does not declare it.
  const std::optional<lsp::WorkspaceEdit> edit =
      world.rename(*documents[1], 0, 15, "Text");
  CHECK(edit.has_value());
  if (!edit.has_value()) return;
  CHECK(edit->changes.has_value() && edit->changes->size() == 2);

  const std::vector<lsp::TextEdit> declaring =
      editsFor(*edit, "file:///w/a.ws");
  CHECK(declaring.size() == 1);
  if (declaring.size() == 1) {
    CHECK(declaring[0].newText == "Text");
    CHECK(declaring[0].range.start.character == 9);
    CHECK(declaring[0].range.end.character == 15);
  }

  const std::vector<lsp::TextEdit> naming = editsFor(*edit, "file:///w/b.ws");
  CHECK(naming.size() == 1);
  if (naming.size() == 1) {
    CHECK(naming[0].newText == "Text");
    CHECK(naming[0].range.start.character == 15);
    CHECK(naming[0].range.end.character == 21);
  }
}

// An unresolved name is often a typo: only that site is rewritten.
void an_unresolved_reference_renames_its_own_site() {
  const World world;
  (void)ct::expectRename(*world.ws,
                         {.text = "entity E { f : <|M<|>issing|> }\n",
                          .newName = "String",
                          .disposeAfterCheck = true});
}

// The new name is checked by the language that declares the symbol: the
// document is read back with it, and a name that lexes as two words, or an
// empty name, is refused.
void a_new_name_the_language_would_not_read_back_is_refused() {
  const World world;
  for (const std::string_view refused : {"Two Words", ""})
    (void)ct::expectRename(*world.ws,
                           {.text = "datatype <|>String\n"
                                    "entity E { f : String }\n",
                            .newName = std::string(refused),
                            .nameRefused = true,
                            .disposeAfterCheck = true});
}

// In a repaired document, a new name that is not read back as the name is
// refused; a valid one is accepted.
void a_repaired_document_refuses_what_is_not_read_as_the_name() {
  const World world;
  for (const std::string_view refused : {"1", "{", ":", "/* c */"})
    (void)ct::expectRename(*world.ws,
                           {.text = "datatype <|>String :\n"
                                    "entity E { f : String }\n",
                            .newName = std::string(refused),
                            .nameRefused = true,
                            .disposeAfterCheck = true});
  (void)ct::expectRename(*world.ws,
                         {.text = "datatype <|<|>String|> :\n"
                                  "entity E { f : <|String|> }\n",
                          .newName = "Text",
                          .disposeAfterCheck = true});
  // At a reference: `a.` reads as a qualified name only once the repair
  // inserts its last segment.
  for (const std::string_view refused : {"1", "{", "a."})
    (void)ct::expectRename(*world.ws,
                           {.text = "entity E { f : <|>Missing :\n",
                            .newName = std::string(refused),
                            .nameRefused = true,
                            .disposeAfterCheck = true});
  (void)ct::expectRename(*world.ws,
                         {.text = "entity E { f : <|<|>Missing|> :\n",
                          .newName = "a.b",
                          .disposeAfterCheck = true});
  // The old name may be one the repair completed; renaming it to a whole name
  // is how the author fixes it.
  (void)ct::expectRename(*world.ws, {.text = "package <|<|>a.|> { }\n",
                                           .newName = "a.b",
                                           .disposeAfterCheck = true});
}

// Every document the edits touch is read back with its own changes, a
// repaired one too: its reference still reads as the name it renames.
void a_rename_reads_back_every_document_it_edits() {
  const World world;
  const std::vector<cursiv::Document*> documents =
      world.built({{"file:///w/a.ws", std::string(kLib)},
                   {"file:///w/b.ws", "entity E { f : String :\n"}});
  CHECK(documents.size() == 2);
  if (documents.size() != 2) return;
  const std::optional<lsp::WorkspaceEdit> edit =
      world.rename(*documents[0], 0, 9, "Text");
  CHECK(edit.has_value() && edit->changes.has_value() &&
        edit->changes->size() == 2);
  if (edit.has_value()) CHECK(editsFor(*edit, "file:///w/b.ws").size() == 1);
  CHECK(!world.rename(*documents[0], 0, 9, "1").has_value());
}

void name_validation_observes_the_requests_cancellation() {
  World world;
  const auto documents = world.built({{"file:///w/a.ws", std::string(kLib)}});
  cursiv::CancellationTokenSource cancel;
  struct CancellingParser final : cursiv::Parser {
    std::unique_ptr<const cursiv::Parser> inner;
    cursiv::CancellationTokenSource& cancel;
    mutable bool called = false;
    CancellingParser(std::unique_ptr<const cursiv::Parser> parser,
                     cursiv::CancellationTokenSource& source)
        : inner(std::move(parser)), cancel(source) {}
    const cursiv::Grammar& getGrammar() const noexcept override {
      return inner->getGrammar();
    }
    cursiv::ParseResult parse(
        std::string_view text, const cursiv::CancellationToken& token) const override {
      called = true;
      CHECK(token == cancel.get_token());
      cancel.request_stop();
      return inner->parse(text, token);
    }
  };
  auto parser = std::make_unique<CancellingParser>(
      std::move(world.ws->parser), cancel);
  const auto* observer = parser.get();
  world.ws->parser = std::move(parser);
  bool cancelled = false;
  try {
    (void)world.ws->renameProvider->rename(
        *documents[0], {0, 9}, "Text", cancel.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(observer->called);
  CHECK(cancelled);
}

void an_accepted_rename_rewrites_every_site() {
  const World world;
  (void)ct::expectRename(*world.ws,
                         {.text = "datatype <|<|>String|>\n"
                                  "entity E { f : <|String|> }\n",
                          .newName = "Text",
                          .disposeAfterCheck = true});
}

//  0         1         2
//  01234567890123456789012345
constexpr std::string_view kPackage = "package p { entity E {} }\n";
constexpr std::string_view kQualified = "entity F { f : p.E }\n";

// Renaming a package requalifies what it contains: a site written under the
// old qualifier is rewritten whole, to the new qualified name.
void a_package_rename_requalifies_its_members_sites() {
  const Module world;
  const std::vector<cursiv::Document*> documents =
      world.built({{"file:///w/a.ws", std::string(kPackage)},
                   {"file:///w/b.ws", std::string(kQualified)}});
  CHECK(documents.size() == 2);
  if (documents.size() != 2) return;

  const std::optional<lsp::WorkspaceEdit> edit =
      world.rename(*documents[0], 0, 8, "q");
  CHECK(edit.has_value());
  if (!edit.has_value()) return;

  const std::vector<lsp::TextEdit> declaring =
      editsFor(*edit, "file:///w/a.ws");
  CHECK(declaring.size() == 1);
  if (declaring.size() == 1) {
    CHECK(declaring[0].newText == "q");
    CHECK(declaring[0].range.start.character == 8);
    CHECK(declaring[0].range.end.character == 9);
  }

  const std::vector<lsp::TextEdit> naming = editsFor(*edit, "file:///w/b.ws");
  CHECK(naming.size() == 1);
  if (naming.size() == 1) {
    CHECK(naming[0].newText == "q.E");
    CHECK(naming[0].range.start.character == 15);
    CHECK(naming[0].range.end.character == 18);
  }
}

// A member renamed through a qualified site keeps the qualifier: `p.G`, not
// `G`.
void a_qualified_site_keeps_its_qualifier() {
  const Module world;
  const std::vector<cursiv::Document*> documents =
      world.built({{"file:///w/a.ws", std::string(kPackage)},
                   {"file:///w/b.ws", std::string(kQualified)}});
  CHECK(documents.size() == 2);
  if (documents.size() != 2) return;

  const std::optional<lsp::WorkspaceEdit> edit =
      world.rename(*documents[1], 0, 17, "G");
  CHECK(edit.has_value());
  if (!edit.has_value()) return;

  const std::vector<lsp::TextEdit> declaring =
      editsFor(*edit, "file:///w/a.ws");
  CHECK(declaring.size() == 1);
  if (declaring.size() == 1) {
    CHECK(declaring[0].newText == "G");
    CHECK(declaring[0].range.start.character == 19);
    CHECK(declaring[0].range.end.character == 20);
  }

  const std::vector<lsp::TextEdit> naming = editsFor(*edit, "file:///w/b.ws");
  CHECK(naming.size() == 1);
  if (naming.size() == 1) {
    CHECK(naming[0].newText == "p.G");
    CHECK(naming[0].range.start.character == 15);
    CHECK(naming[0].range.end.character == 18);
  }
}

}  // namespace

void run() {
  a_keyword_is_not_renamable();
  a_rename_crosses_the_documents_that_name_it();
  an_unresolved_reference_renames_its_own_site();
  a_new_name_the_language_would_not_read_back_is_refused();
  a_repaired_document_refuses_what_is_not_read_as_the_name();
  a_rename_reads_back_every_document_it_edits();
  name_validation_observes_the_requests_cancellation();
  an_accepted_rename_rewrites_every_site();
  a_package_rename_requalifies_its_members_sites();
  a_qualified_site_keeps_its_qualifier();
}

}  // namespace rename_provider

namespace hover_provider {

namespace {

namespace ct = cursiv::test;

// The workspace language: its keywords carry `.doc<>()` and its members hold
// references.
struct World : support::LspWorkspace {
  [[nodiscard]] std::optional<lsp::Hover> at(const cursiv::Document& document,
                                             lsp::Uint line,
                                             lsp::Uint character) const {
    return ws->hoverProvider->getHoverContent(
        document, lsp::Position{line, character}, cursiv::kNoCancel);
  }
};

constexpr std::string_view kKeywordModel =
    "datatype String\n"
    "entity E { f : String }\n";

// The range is the keyword alone, not the declaration it starts.
void a_documented_keyword_answers_the_grammars_prose() {
  const World world;
  (void)ct::expectHover(*world.ws,
                        {.text = "datatype String\n"
                                 "e<|>ntity E { f : String }\n",
                         .hover = "Declares an entity, closed by `}`.",
                         .disposeAfterCheck = true});

  // `expectHover` does not check the range, so it is checked here.
  const cursiv::Document* const document =
      world.built("file:///w/a.ws", std::string(kKeywordModel));
  const std::optional<lsp::Hover> hover = world.at(*document, 1, 1);
  CHECK(hover.has_value() && hover->range.has_value());
  if (!hover.has_value() || !hover->range.has_value()) return;
  CHECK(hover->range->start.line == 1 && hover->range->start.character == 0);
  CHECK(hover->range->end.line == 1 && hover->range->end.character == 6);
}

// A word with a text per use answers with the text of the use under the caret,
// even where two rules start the same. A word with one text answers it
// anywhere.
void a_word_answers_the_text_of_its_use(
    std::unique_ptr<cursiv::Parser> parser,
    std::unique_ptr<cursiv::AstReflection> reflection) {
  support::World<cursiv::SharedLspServices> world;
  const cursiv::LspServices* const language =
      &support::add_language<cursiv::LspServices>(
          world.shared, {"dual", {".dual"}, {}}, std::move(parser),
          std::move(reflection));
  const auto expect = [&](std::string text, std::string hover) {
    (void)ct::expectHover(*language, {.text = std::move(text),
                                      .hover = std::move(hover),
                                      .disposeAfterCheck = true});
  };
  expect("t<|>win a!", "First twin.");
  expect("t<|>win a?", "Second twin.");
  expect("tw<|>ice a twice", "Opens.");
  expect("twice a tw<|>ice", "Closes.");
  expect("st<|>ate a", "Names a state.");
}

// A CST span is half-open, so a caret right after a word is outside it: hover
// retries one byte back, so a double-click is on the selected word.
void a_caret_one_byte_past_the_word_is_still_on_it() {
  const World world;
  (void)ct::expectHover(*world.ws,
                        {.text = "datatype String\n"
                                 "entity<|> E { f : String }\n",
                         .hover = "Declares an entity, closed by `}`.",
                         .disposeAfterCheck = true});
}

//  0         1         2
//  012345678901234567890
constexpr std::string_view kModel =
    "/** The string. */\n"
    "datatype String\n"
    "entity E { f : String }\n";

// The range is the name as written here, not the target's.
void a_resolved_reference_answers_its_declarations_documentation() {
  const World world;
  (void)ct::expectHover(*world.ws,
                        {.text = "/** The string. */\n"
                                 "datatype String\n"
                                 "entity E { f : S<|>tring }\n",
                         .hover = "The string.",
                         .disposeAfterCheck = true});

  const cursiv::Document* const document =
      world.built("file:///w/a.ws", std::string(kModel));
  const std::optional<lsp::Hover> hover = world.at(*document, 2, 17);
  CHECK(hover.has_value() && hover->range.has_value());
  if (!hover.has_value() || !hover->range.has_value()) return;
  CHECK(hover->range->start.line == 2 && hover->range->start.character == 15);
  CHECK(hover->range->end.line == 2 && hover->range->end.character == 21);
}

// `nullopt`, not an empty popup: an editor draws a frame around whatever it
// gets.
void a_caret_on_nothing_answers_nothing() {
  const World world;
  (void)ct::expectHover(*world.ws, {.text = "<|>\n\ndatatype String\n",
                                          .disposeAfterCheck = true});

  // A name with no documentation.
  (void)ct::expectHover(*world.ws, {.text = "datatype St<|>ring\n",
                                          .disposeAfterCheck = true});
}

}  // namespace

void run() {
  a_documented_keyword_answers_the_grammars_prose();
  a_word_answers_the_text_of_its_use(
      tdual::generated::Lr::create_parser(),
      tdual::generated::Lr::create_ast_reflection());
  a_word_answers_the_text_of_its_use(
      tdual::generated::Ll::create_parser(),
      tdual::generated::Ll::create_ast_reflection());
  a_caret_one_byte_past_the_word_is_still_on_it();
  a_resolved_reference_answers_its_declarations_documentation();
  a_caret_on_nothing_answers_nothing();
}

}  // namespace hover_provider

// Document symbols and workspace symbols, tested on one text.
namespace workspace_symbol_provider {

namespace {

namespace ct = cursiv::test;

using World = support::LspWorkspace;

constexpr std::string_view kModel = support::kPackage;

[[nodiscard]] const lsp::DocumentSymbol* find(
    std::span<const lsp::DocumentSymbol> symbols, std::string_view name) {
  for (const lsp::DocumentSymbol& symbol : symbols) {
    if (symbol.name == name) return &symbol;
    if (symbol.children.has_value())
      if (const lsp::DocumentSymbol* const deeper =
              find(*symbol.children, name))
        return deeper;
  }
  return nullptr;
}

// The outline nests by ownership, and the selection range is the name, so a
// jump to a symbol puts the caret on the word.
void the_outline_nests_and_points_at_the_name() {
  const World world;
  const ct::ExpectedSymbolsCallback outline{
      .text = std::string(kModel),
      .assertion =
          [](std::span<const lsp::DocumentSymbol> symbols) {
            // One root, the package, with everything beneath it.
            CHECK(symbols.size() == 1);
            const lsp::DocumentSymbol* const package = find(symbols, "p");
            CHECK(package != nullptr && package->children.has_value());
            if (package == nullptr || !package->children.has_value()) return;
            CHECK(package->children->size() == 2);

            const lsp::DocumentSymbol* const person = find(symbols, "Person");
            CHECK(person != nullptr);
            if (person == nullptr) return;
            CHECK(person->children.has_value() &&
                  person->children->size() == 1);
            const lsp::DocumentSymbol* const feature = find(symbols, "name");
            CHECK(feature != nullptr);
            if (feature == nullptr) return;

            // The default provider gives every symbol the same kind.
            CHECK(person->kind == lsp::SymbolKind::Field);
            CHECK(feature->kind == lsp::SymbolKind::Field);
            // The selection range is the name alone: `entity Person {` starts
            // at column 2 on line 5, the name at column 9.
            CHECK(person->selectionRange.start.line == 5);
            CHECK(person->selectionRange.start.character == 9);
            CHECK(person->selectionRange.end.character == 15);
            // The full range covers the block.
            CHECK(person->range.start.character == 2);
            CHECK(person->range.end.line == 7);
          },
      .disposeAfterCheck = true};
  (void)ct::expectSymbols(*world.ws, outline);
}

void an_unnamed_root_lists_its_named_children() {
  const World world;
  const cursiv::Document* const document =
      world.built("file:///w/a.ws", "datatype a\ndatatype b\n");
  const auto symbols = world.ws->documentSymbolProvider->getSymbols(
      *document, cursiv::kNoCancel);
  CHECK(symbols.size() == 2);
  CHECK(symbols.size() == 2 && symbols[0].name == "a" &&
        symbols[1].name == "b");
}

// A query that matches nothing gives an empty list, not every symbol.
void a_query_that_matches_nothing_answers_nothing() {
  const World world;
  (void)world.built("file:///w/a.ws", std::string(kModel));
  // `kModel` exports only its package, so `Person` is declared at the top of a
  // second document.
  (void)world.built("file:///w/b.ws", "entity Person {\n}\n");

  CHECK(!world.shared.workspaceSymbolProvider
             ->getSymbols("Person", cursiv::kNoCancel)
             .empty());
  CHECK(world.shared.workspaceSymbolProvider
            ->getSymbols("zzz-nothing-declares-this", cursiv::kNoCancel)
            .empty());
}

// Sets a container name on every symbol.
struct ContainerNaming final : cursiv::DefaultWorkspaceSymbolProvider {
  using DefaultWorkspaceSymbolProvider::DefaultWorkspaceSymbolProvider;

 protected:
  [[nodiscard]] std::optional<lsp::WorkspaceSymbol> getWorkspaceSymbol(
      const cursiv::AstNodeDescription& description) const override {
    std::optional<lsp::WorkspaceSymbol> symbol =
        DefaultWorkspaceSymbolProvider::getWorkspaceSymbol(description);
    if (symbol.has_value()) symbol->containerName = std::string("named");
    return symbol;
  }
};

// A language overrides `getWorkspaceSymbol` to change one symbol without
// rewriting the index walk; the symbol keeps its own document's location.
void one_symbol_is_changed_through_get_workspace_symbol() {
  World world;
  world.shared.workspaceSymbolProvider =
      std::make_unique<ContainerNaming>(world.shared);
  (void)world.built("file:///w/b.ws", "entity Person {\n}\n");
  const std::vector<lsp::WorkspaceSymbol> symbols =
      world.shared.workspaceSymbolProvider->getSymbols("Person",
                                                       cursiv::kNoCancel);
  CHECK(!symbols.empty());
  for (const lsp::WorkspaceSymbol& symbol : symbols) {
    CHECK(symbol.containerName.has_value() && *symbol.containerName == "named");
    const auto* located = std::get_if<lsp::Location>(&symbol.location);
    CHECK(located != nullptr && located->uri.toString() == "file:///w/b.ws");
  }
}

// Answers no query, resolves every symbol.
struct ResolvingProvider final : cursiv::WorkspaceSymbolProvider {
  [[nodiscard]] std::vector<lsp::WorkspaceSymbol> getSymbols(
      std::string_view, const cursiv::CancellationToken&) const override {
    return {};
  }
  [[nodiscard]] bool resolvesSymbols() const noexcept override { return true; }
  [[nodiscard]] lsp::WorkspaceSymbol resolveSymbol(
      lsp::WorkspaceSymbol symbol,
      const cursiv::CancellationToken&) const override {
    symbol.containerName = std::string("resolved");
    return symbol;
  }
};

[[nodiscard]] bool announces_resolve(const lsp::InitializeResult& result) {
  if (!result.capabilities.workspaceSymbolProvider.has_value()) return false;
  const auto* options = std::get_if<lsp::WorkspaceSymbolOptions>(
      &*result.capabilities.workspaceSymbolProvider);
  return options != nullptr && options->resolveProvider == true;
}

// A resolving provider is announced and reached through the resolve request;
// the default does not resolve and returns the symbol unchanged.
void a_resolving_provider_is_announced_and_reached() {
  const lsp::InitializeParams handshake;
  lsp::WorkspaceSymbol symbol;
  symbol.name = "Person";

  World resolving;
  resolving.shared.workspaceSymbolProvider =
      std::make_unique<ResolvingProvider>();
  const lsp::InitializeResult announced =
      resolving.shared.languageServer->initialize(handshake);
  CHECK(announces_resolve(announced));
  (void)resolving.built("file:///w/a.ws", std::string(kModel));
  const lsp::WorkspaceSymbol resolved =
      resolving.shared.languageServer->workspaceSymbolResolve(
          symbol, cursiv::kNoCancel);
  CHECK(resolved.containerName.has_value() &&
        *resolved.containerName == "resolved");

  const World plain;
  const lsp::InitializeResult silent =
      plain.shared.languageServer->initialize(handshake);
  CHECK(silent.capabilities.workspaceSymbolProvider.has_value());
  CHECK(!announces_resolve(silent));
  (void)plain.built("file:///w/a.ws", std::string(kModel));
  CHECK(!plain.shared.languageServer
             ->workspaceSymbolResolve(symbol, cursiv::kNoCancel)
             .containerName.has_value());
}

[[nodiscard]] const lsp::Location* location_of(
    std::span<const lsp::WorkspaceSymbol> symbols, std::string_view name) {
  for (const lsp::WorkspaceSymbol& symbol : symbols)
    if (symbol.name == name)
      return std::get_if<lsp::Location>(&symbol.location);
  return nullptr;
}

// A hit selects the name, not the declaration it opens -- whether the name
// is one token or a data-type rule's whole match.
void a_workspace_symbol_ranges_over_its_name() {
  const World world;
  (void)world.built("file:///w/a.ws", std::string(kModel));
  (void)world.built("file:///w/b.ws", "entity Person {\n  name : String\n}\n");

  const std::vector<lsp::WorkspaceSymbol> people =
      world.shared.workspaceSymbolProvider->getSymbols("Person",
                                                       cursiv::kNoCancel);
  CHECK(people.size() == 1);
  const lsp::Location* const person = location_of(people, "Person");
  CHECK(person != nullptr);
  if (person != nullptr) {
    CHECK(person->uri.toString() == "file:///w/b.ws");
    CHECK(person->range.start.line == 0 && person->range.start.character == 7);
    CHECK(person->range.end.line == 0 && person->range.end.character == 13);
  }

  const std::vector<lsp::WorkspaceSymbol> packages =
      world.shared.workspaceSymbolProvider->getSymbols("p", cursiv::kNoCancel);
  const lsp::Location* const package = location_of(packages, "p");
  CHECK(package != nullptr);
  if (package != nullptr) {
    CHECK(package->uri.toString() == "file:///w/a.ws");
    CHECK(package->range.start.line == 0 &&
          package->range.start.character == 8);
    CHECK(package->range.end.line == 0 && package->range.end.character == 9);
  }
}

// A declared name that matched no text still gets a symbol, so its feature
// stays under it, not under the grandparent.
void a_declared_name_that_matched_nothing_keeps_its_row() {
  const World world;
  const cursiv::Document* const document =
      world.built("file:///w/a.ws", "entity { name : String }\n");
  const std::vector<lsp::DocumentSymbol> symbols =
      world.ws->documentSymbolProvider->getSymbols(*document,
                                                   cursiv::kNoCancel);
  CHECK(symbols.size() == 1);
  if (symbols.size() != 1) return;

  const lsp::DocumentSymbol& entity = symbols.front();
  CHECK(entity.name.empty());
  CHECK(entity.range.start.character == 0 && entity.range.end.character == 24);
  CHECK(entity.selectionRange.start.character == 6 &&
        entity.selectionRange.end.character == 6);
  CHECK(entity.children.has_value() && entity.children->size() == 1);
  if (entity.children.has_value() && entity.children->size() == 1)
    CHECK(entity.children->front().name == "name");
}

}  // namespace

void run() {
  the_outline_nests_and_points_at_the_name();
  an_unnamed_root_lists_its_named_children();
  a_declared_name_that_matched_nothing_keeps_its_row();
  a_query_that_matches_nothing_answers_nothing();
  one_symbol_is_changed_through_get_workspace_symbol();
  a_resolving_provider_is_announced_and_reached();
  a_workspace_symbol_ranges_over_its_name();
}

}  // namespace workspace_symbol_provider

int main() {
  cursiv::test::count_expectations();
  definition_provider::run();
  document_highlight_provider::run();
  references_provider::run();
  rename_provider::run();
  hover_provider::run();
  workspace_symbol_provider::run();
  return cursiv::test::report("navigation");
}
