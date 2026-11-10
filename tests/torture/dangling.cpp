// The dangling-else grammar through its generated module's type-erased
// parser: corrupted documents parse without a crash or an unreported error,
// and completion.

#include <lsp/types.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <string_view>

#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/ExpectedCompletion.hpp"
#include "election/generated/Election/Module.hpp"
#include "support/Editor.hpp"
#include "support/World.hpp"

namespace {

namespace ct = cursiv::test;

struct World : support::World<cursiv::SharedLspServices> {
  cursiv::LspServices* lang = &support::add_language<cursiv::LspServices>(
      shared, {"dangling", {".dangling"}, {}},
      telect::generated::Election::create_parser(),
      telect::generated::Election::create_ast_reflection());

  World() { support::take_both_ranges(shared); }
};

using support::offers;

// No label is empty or names a terminal or a rule (`ID`, `Stmt`).
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
    {"extra else (nothing left to bind)", "if a then x else e else e2"},
    {"truncated keyword", "if a th"},
    {"empty document", ""},
    {"if with no condition", "if then x"},
    {"deep nesting, one corrupted level", "if a then if then x else e"},
};

// A broken document parses with diagnostics whose spans sit inside the text.
void recovery() {
  const std::unique_ptr<cursiv::Parser> engine =
      telect::generated::Election::create_parser();
  for (const Case& c : kCases) {
    const cursiv::ParseResult r = engine->parse(c.text);  // must not crash
    const cursiv::Cst& cst = r.cst();

    CHECK(!(cst.accepted() && !cst.recovered()));  // broken by construction

    // An accepted parse has a value.
    if (cst.accepted()) CHECK(r.value != nullptr);
    for (const cursiv::ParseDiagnostic& d : r.diagnostics) {
      CHECK(d.begin <= d.end);
      CHECK(d.end <= c.text.size());
    }
    // A recovered parse reports at least one diagnostic.
    if (cst.recovered()) CHECK(!r.diagnostics.empty());
  }
}

void completion() {
  const World world;

  // At the start of a document (the trailing ` x` keeps it parseable), the
  // entry's opener `if` is offered.
  offered(world, "<|> x", [](const lsp::CompletionList& list) {
    CHECK(labels_are_sane(list));
    CHECK(offers(list, "if"));
  });

  // After `if `, a condition is expected: neither `then` nor `else` is
  // offered.
  offered(world, "if <|>a then x", [](const lsp::CompletionList& list) {
    CHECK(labels_are_sane(list));
    CHECK(!offers(list, "then"));
    CHECK(!offers(list, "else"));
  });

  // Inside `then`: the replace range covers the whole word, so accepting it
  // does not leave `thenen`.
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

  // Right after `then`, with no space before the caret.
  offered(world, "if a then<|> x", [](const lsp::CompletionList& list) {
    CHECK(labels_are_sane(list));
  });

  // Past a parse error: an identifier sits where `then` is expected.
  offered(world, "if a x th<|>", [](const lsp::CompletionList& list) {
    CHECK(labels_are_sane(list));
  });
}

}  // namespace

void torture_dangling() {
  recovery();
  completion();
}
