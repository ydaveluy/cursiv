// LSP feature tests using the reusable `pegium::testing` harness. It builds
// a marked-up source string through the
// whole pipeline and drives the LSP services through pegium's public API, so it
// is only built when the language server is scaffolded (LSP=ON).
//
// Markers in the source: `<|>` is a cursor index, `<| … |>` marks a range.
#include <@PEGIUM_NEW_LANGUAGE_ID@/lsp/LspModule.hpp>

#include <pegium/testing/Testing.hpp>

#include <algorithm>
#include <string_view>

#include <gtest/gtest.h>
#include <lsp/types.h>

namespace {

// Builds a workspace with the @PEGIUM_NEW_CLASS@ LSP services registered.
pegium::testing::TestWorkspace make@PEGIUM_NEW_CLASS@Workspace() {
  auto ws = pegium::testing::TestWorkspace::withShared<
      @PEGIUM_NEW_LANGUAGE_ID@::@PEGIUM_NEW_CLASS@SharedServices>();
  ws.registerLanguage(@PEGIUM_NEW_LANGUAGE_ID@::create@PEGIUM_NEW_CLASS@LspServices(
      ws.shared<@PEGIUM_NEW_LANGUAGE_ID@::@PEGIUM_NEW_CLASS@SharedServices>()));
  return ws;
}

// Validation: an unresolved cross-reference is reported as a diagnostic. The
// message is matched as a substring, so the assertion stays readable.
TEST(@PEGIUM_NEW_CLASS@Lsp, ReportsUnresolvedReference) {
  auto ws = make@PEGIUM_NEW_CLASS@Workspace();
  pegium::testing::expectValidation(
      ws, "@PEGIUM_NEW_LANGUAGE_ID@",
      {.text = "Hello Ghost!\n",
       .diagnostics = {{.message = "Could not resolve reference"}}});
}

// Completion: at the cursor of a greeting, the names of the declared persons are
// proposed. `.check` lets us assert a subset without pinning the full list.
TEST(@PEGIUM_NEW_CLASS@Lsp, CompletesDeclaredNames) {
  auto ws = make@PEGIUM_NEW_CLASS@Workspace();
  pegium::testing::expectCompletion(
      ws, "@PEGIUM_NEW_LANGUAGE_ID@",
      {.text = "person John\nperson Jane\nHello <|>",
       .check = [](const ::lsp::CompletionList &list) {
         const auto proposes = [&](std::string_view label) {
           return std::ranges::any_of(list.items, [&](const auto &item) {
             return item.label == label;
           });
         };
         EXPECT_TRUE(proposes("John"));
         EXPECT_TRUE(proposes("Jane"));
       }});
}

} // namespace
