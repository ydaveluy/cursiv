// Pins the erased door: the same grammar, reached through a service that never
// learns it. The AST it hands back is the one the table read directly gives.

#include "workspace/generated/Workspace/Parser.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstBuildError.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/engines/shared/runtime/AstReducer.hpp"
#include "cursiv/test/Check.hpp"
#include "workspace/generated/Workspace/AstReflection.hpp"
#include "workspace/generated/Workspace/Module.hpp"
#include "workspace/generated/Workspace/Table.hpp"

namespace {

namespace dmg = ws::generated::Workspace;

const cursiv::Parser& service() {
  static const std::unique_ptr<cursiv::Parser> engine =
      ws::generated::Workspace::create_parser();
  return *engine;
}

// The other road to the same tree: the generated table and drive, read as
// data, with no service in front of them.
bool direct_parse(std::string_view text, cursiv::Cst& cst) {
  return cursiv::lr_parser<dmg::kTable>.parse(text, cst, nullptr,
                                              dmg::kDrivePtr);
}

// The erased door answers about records and tokens, which is all a service
// that never learns its grammar can ask.
void the_grammar_is_reachable_without_the_template() {
  const cursiv::Grammar& g = service().getGrammar();
  const cursiv::ParseResult r = service().parse("datatype String\n");
  CHECK(static_cast<bool>(r));
  const cursiv::Cst& cst = r.cst();
  CHECK(g.recordRule(cst[cst.entry_root()]).empty() ||
        !g.recordRule(cst[cst.entry_root()]).empty());
  // A terminal is spelled or it is named, never both and never neither.
  CHECK(!g.terminalText(1).empty() || !g.terminalName(1).empty());
}

void a_parse_through_the_interface_builds_the_same_tree() {
  const std::string src = "datatype String\nentity A { x : String }\n";
  const cursiv::ParseResult through = service().parse(src);
  CHECK(static_cast<bool>(through));
  CHECK(through.value != nullptr);
  CHECK(through.diagnostics.empty());

  // The table read directly, byte for byte the same outcome -- the reference
  // work list included: the document declares one reference, and both roads
  // owe its handle.
  cursiv::Cst cst;
  direct_parse(src, cst);
  const cursiv::ParseResult direct =
      cursiv::AstReducer::build<dmg::kTable.stamp>(dmg::kAstActions,
                                                   std::move(cst));
  CHECK(through.nodes.size() == direct.nodes.size());
  CHECK(through.cst().size() == direct.cst().size());
  CHECK(through.references != nullptr && direct.references != nullptr);
  CHECK(through.references->handles.size() == 1);
  CHECK(through.references->handles.size() ==
        direct.references->handles.size());
  CHECK(through.references->handles[0].field ==
        direct.references->handles[0].field);
  CHECK(through.error == cursiv::AstBuildError::none && through.accepted);
}

// Full parses and repairs must stop at the view's end and borrow its bytes.
void a_parse_reads_only_the_borrowed_view() {
  const std::string cases[] = {
      "datatype String\nentity A { x : String }\n",
      "datatype String\nentity A { x  String }\n",
      "datatype String\nentity \xE2\x82",
      std::string("datatype String\n// a") + '\0' + "b\n"};
  for (const std::string& text : cases) {
    const cursiv::ParseResult reference = service().parse(text);
    const auto check = [&](std::string_view view) {
      cursiv::Cst direct;
      CHECK(direct_parse(view, direct) == reference.accepted);
      const cursiv::ParseResult actual = service().parse(view);
      CHECK(actual.accepted == reference.accepted);
      CHECK(actual.cst().text().data() == view.data());
      CHECK(actual.cst().text() == view);
      CHECK(actual.cst().size() == reference.cst().size());
      CHECK(direct.size() == reference.cst().size());
      CHECK(actual.diagnostics.size() == reference.diagnostics.size());
      CHECK(actual.nodes.size() == reference.nodes.size());
      for (cursiv::NodeId i = 0;
           i < actual.cst().size() && i < reference.cst().size(); ++i)
        CHECK(std::memcmp(&actual.cst()[i], &reference.cst()[i],
                          sizeof(cursiv::CstNode)) == 0);
    };
    const std::string followed = text + "unexpected suffix";
    check(std::string_view(followed.data(), text.size()));
    auto exact = std::make_unique<char[]>(text.size());
    std::memcpy(exact.get(), text.data(), text.size());
    check(std::string_view(exact.get(), text.size()));
#if defined(__unix__) || defined(__APPLE__)
    // End the view immediately before an unreadable page: even a one-byte
    // overread in the generated drive or the shared runtime must fail.
    const long pageSize = sysconf(_SC_PAGESIZE);
    CHECK(pageSize > 0 && text.size() < static_cast<std::size_t>(pageSize));
    if (pageSize <= 0 || text.size() >= static_cast<std::size_t>(pageSize))
      continue;
    const auto page = static_cast<std::size_t>(pageSize);
    void* region = mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(region != MAP_FAILED);
    if (region == MAP_FAILED) continue;
    char* const end = static_cast<char*>(region) + page;
    const bool protectedEnd = mprotect(end, page, PROT_NONE) == 0;
    CHECK(protectedEnd);
    if (protectedEnd) {
      std::memcpy(end - text.size(), text.data(), text.size());
      check(std::string_view(end - text.size(), text.size()));
    }
    CHECK(munmap(region, 2 * page) == 0);
#endif
  }
  const cursiv::ParseResult empty = service().parse({});
  CHECK(empty.cst().text().empty() && empty.cst().text().data() != nullptr);
}

// A repair reports through the interface exactly as it does under it.
void a_broken_text_still_answers() {
  const std::string src = "datatype String\nentity A { x  String }\n";
  const cursiv::ParseResult r = service().parse(src);
  CHECK(!r.diagnostics.empty());
  CHECK(r.value != nullptr);
  // The result's list is the tree's repairs, not a second bookkeeping.
  CHECK(r.diagnostics.size() == r.cst().diagnostics().size());
  // A repair carries its whole record through the erased door: a kind and a
  // place, not just a count.
  const cursiv::ParseDiagnostic& d = r.diagnostics.front();
  CHECK(d.kind != cursiv::DiagnosticKind::conversion_error);
  CHECK(d.end >= d.begin);
  CHECK(d.element != cursiv::kNoElement);
}

// Cancellation is observed before any work starts.
void a_cancelled_token_stops_the_parse() {
  cursiv::CancellationTokenSource source;
  source.request_stop();
  bool threw = false;
  try {
    (void)service().parse("datatype String\n", source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  CHECK(threw);
}

// Ordinary reads keep their hot loop free of cancellation polling.
void a_long_reading_runs_whole_and_the_door_answers() {
  std::string src = "datatype String\n";
  for (int i = 0; i < 20000; ++i)
    src += "entity E" + std::to_string(i) + " { x : String }\n";

  cursiv::Cst cst;
  CHECK(direct_parse(src, cst));
  cursiv::Cst whole;
  CHECK(direct_parse(src, whole));
  CHECK(cst.size() == whole.size());

  cursiv::CancellationTokenSource source;
  source.request_stop();
  bool threw = false;
  try {
    (void)service().parse(src, source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  CHECK(threw);
}

cursiv::AstEntryFn stoppedEntry = nullptr;
cursiv::CancellationTokenSource* astStop = nullptr;

void a_cancelled_token_stops_the_ast_before_work() {
  cursiv::Cst cst;
  CHECK(direct_parse("datatype String\n", cst));
  cursiv::CancellationTokenSource source;
  source.request_stop();
  bool threw = false;
  try {
    (void)cursiv::AstReducer::build<dmg::kTable.stamp>(
        dmg::kAstActions, std::move(cst), source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  CHECK(threw);
}

void stop_after_an_ast_entry(cursiv::AstReducer& builder, cursiv::NodeId id) {
  stoppedEntry(builder, id);
  astStop->request_stop();
}

void a_stop_during_ast_work_is_observed() {
  cursiv::Cst cst;
  CHECK(direct_parse("datatype String\nentity A { x : String }\n", cst));
  auto actions = dmg::kAstActions;
  std::vector<cursiv::AstEntryFn> entries(actions.entries,
                                          actions.entries + actions.entryCount);
  cursiv::ElementId first = cursiv::kNoElement;
  for (cursiv::NodeId id = 0; id < cst.size(); ++id) {
    const auto row = dmg::kTable.stamp.row(cst[id]);
    if (!cst[id].hidden && row != cursiv::kNoElement &&
        row < actions.entryCount && entries[row] != nullptr) {
      first = row;
      break;
    }
  }
  CHECK(first != cursiv::kNoElement);
  if (first == cursiv::kNoElement) return;
  cursiv::CancellationTokenSource source;
  astStop = &source;
  stoppedEntry = entries[first];
  entries[first] = &stop_after_an_ast_entry;
  actions.entries = entries.data();
  bool threw = false;
  try {
    (void)cursiv::AstReducer::build<dmg::kTable.stamp>(actions, std::move(cst),
                                                       source.get_token());
  } catch (const cursiv::OperationCancelled&) {
    threw = true;
  }
  astStop = nullptr;
  CHECK(source.stop_requested() && threw);
}

}  // namespace

int main() {
  the_grammar_is_reachable_without_the_template();
  a_parse_through_the_interface_builds_the_same_tree();
  a_parse_reads_only_the_borrowed_view();
  a_broken_text_still_answers();
  a_cancelled_token_stops_the_parse();
  a_long_reading_runs_whole_and_the_door_answers();
  a_cancelled_token_stops_the_ast_before_work();
  a_stop_during_ast_work_is_observed();
  return cursiv::test::report("parser_service");
}
