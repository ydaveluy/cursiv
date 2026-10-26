// The dangling-else grammar through the erased door its generated module
// hands out: corrupted documents read without a crash or a silent swallow, and
// completion.

#include <lsp/types.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <memory>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/lsp/runtime/LanguageServer.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/test/Check.hpp"
#include "cursiv/test/ExpectedCompletion.hpp"
#include "election/generated/Election/Module.hpp"

namespace {

namespace ct = cursiv::test;

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
    language->languageMetaData = {"dangling", {".dangling"}, {}};
    language->parser = telect::generated::Election::create_parser();
    language->astReflection = telect::generated::Election::create_ast_reflection();
    cursiv::install_default_core_services(*language);
    cursiv::install_default_lsp_services(*language);
    lang = language.get();
    shared.serviceRegistry->registerServices(std::move(language));
    // An editor that chooses between inserting and replacing a word.
    lsp::InitializeParams params;
    params.capabilities.textDocument.emplace()
        .completion.emplace()
        .completionItem.emplace()
        .insertReplaceSupport = true;
    (void)shared.languageServer->initialize(params);
  }
};

bool offers(const lsp::CompletionList& list, std::string_view label) {
  return std::ranges::any_of(list.items, [label](const lsp::CompletionItem& i) {
    return i.label == label;
  });
}

// No offer may spell a raw terminal name -- every label a plain keyword
// spelling (this grammar's `kw<"if">`/`kw<"then">`/`kw<"else">`) or the
// grammar's own terminal names for `ID`, never an internal identifier like
// "KW_IF" or a mangled rule tag.
bool labels_are_sane(const lsp::CompletionList& list) {
  for (const lsp::CompletionItem& i : list.items) {
    if (i.label.empty()) return false;
    if (i.label == "ID" || i.label == "Stmt") return false;
  }
  return true;
}

void offered(const World& world, std::string text,
             std::function<void(const lsp::CompletionList&)> assertion) {
  try {
    (void)ct::expectCompletion(
        *world.lang,
        ct::ExpectedCompletionCallback{.text = text,
                                       .assertion = std::move(assertion),
                                       .disposeAfterCheck = true});
  } catch (const std::exception& e) {
    std::printf("    offered(\"%s\") threw: %s\n", text.c_str(), e.what());
    ++cursiv::test::failures;
  }
}

struct Case {
  const char* label;
  std::string_view text;
};

const Case kCases[] = {
    {"missing then", "if a x"},
    {"missing else body", "if a then x else"},
    {"stray else (nothing left to bind)", "if a then x else e else e2"},
    {"truncated keyword", "if a th"},
    {"empty document", ""},
    {"if with no condition", "if then x"},
    {"deep nesting, one corrupted level", "if a then if then x else e"},
};

// A document that cannot read clean carries at least one diagnostic whose
// span sits inside the text.
void recovery() {
  const std::unique_ptr<cursiv::Parser> engine =
      telect::generated::Election::create_parser();
  for (const Case& c : kCases) {
    const cursiv::ParseResult r = engine->parse(c.text);  // must not crash
    const cursiv::Cst& cst = r.cst();

    CHECK(!(cst.accepted() && !cst.recovered()));  // broken by construction

    // The recovery's contract: an accepted reading has a value.
    if (cst.accepted()) CHECK(r.value != nullptr);
    for (const cursiv::ParseDiagnostic& d : r.diagnostics) {
      CHECK(d.begin <= d.end);
      CHECK(d.end <= c.text.size());
    }
    // A repaired reading owes at least one diagnostic -- silence would mean
    // the corruption was swallowed rather than reported.
    if (cst.recovered()) CHECK(!r.diagnostics.empty());
  }
}

void completion() {
  const World world;

  // The very start of a document (a trailing " x" keeps it parseable): the
  // only thing writable there is the entry rule's own opener, "if" or an
  // identifier -- never both a keyword and its internal name.
  offered(world, "<|> x", [](const lsp::CompletionList& list) {
    CHECK(labels_are_sane(list));
    CHECK(offers(list, "if"));
  });

  // Right after "if ": a condition is owed, so "if"/"then"/"else" must not
  // be offered here -- only what starts an ID-shaped condition would be,
  // and this grammar spells no keyword for that.
  offered(world, "if <|>a then x", [](const lsp::CompletionList& list) {
    CHECK(labels_are_sane(list));
    CHECK(!offers(list, "then"));
    CHECK(!offers(list, "else"));
  });

  // Mid-word: the caret sits inside "th" of "then" -- completing "then"
  // must replace the whole word, not insert past the caret leaving "thenen".
  offered(world, "if a th<|>en x", [](const lsp::CompletionList& list) {
    CHECK(labels_are_sane(list));
    const lsp::CompletionItem* item = nullptr;
    for (const lsp::CompletionItem& i : list.items)
      if (i.label == "then") item = &i;
    if (item != nullptr && item->textEdit.has_value()) {
      const auto& edit = std::get<lsp::InsertReplaceEdit>(*item->textEdit);
      CHECK(edit.replace.start.character == 5);  // start of "then"
      CHECK(edit.replace.end.character == 9);    // end of "then"
    }
  });

  // Right after "then" with nothing following yet, DeepIf's grammar opens a
  // whole Stmt next, so "if" is offered -- but a raw "e" glued with no
  // separating space must not merge into one word with any keyword offer.
  offered(world, "if a then<|> x", [](const lsp::CompletionList& list) {
    CHECK(labels_are_sane(list));
  });

  // Past a genuine parse error (a truncated "th" that can never complete to
  // a keyword at this position because a full identifier already sits
  // before it), the caret is in a rejected zone: nothing here spells a
  // full keyword name to insert blindly.
  offered(world, "if a x th<|>", [](const lsp::CompletionList& list) {
    CHECK(labels_are_sane(list));
  });
}

}  // namespace

void torture_dangling() {
  recovery();
  completion();
}
