// The four checks of the two-language example, two of which read across
// documents: the world here holds `.req` and `.tst` files, built together.

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(__wasi__) || defined(_REENTRANT)
#include <atomic>
#include <barrier>
#include <chrono>
#include <thread>
#endif

#include "cursiv/Check.hpp"
#include "cursiv/ExpectDiagnosticOptions.hpp"
#include "cursiv/ExpectFormatting.hpp"
#include "cursiv/ValidationResult.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "cursiv/workspace/BuildOptions.hpp"
#include "cursiv/workspace/DefaultDocuments.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentFactory.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/MemoryFileSystemProvider.hpp"
#include "requirements/CoreModule.hpp"
#include "requirements/Generator.hpp"
#include "requirements/LspModule.hpp"
#include "requirements/ast.hpp"
#include "requirements/validation/RequirementsValidator.hpp"

namespace {

namespace ct = cursiv::test;

// One `.req` and the `.tst` files that cover it, seen at once: the coverage
// answer is the workspace's, so a document built alone would answer a different
// one.
struct World {
  req::RequirementsCoreLanguage services = req::create_core_services();
  req::RequirementsSharedCoreServices& shared = *services.shared;
  std::vector<std::shared_ptr<cursiv::Document>> opened;

  void add(const std::string& uri, std::string text) {
    auto document = shared.documentFactory->fromString(std::move(text), uri);
    shared.documents->addDocument(document);
    opened.push_back(std::move(document));
  }

  void build(const cursiv::BuildOptions& options = {}) const {
    std::vector<cursiv::Document*> set;
    set.reserve(opened.size());
    for (const std::shared_ptr<cursiv::Document>& document : opened)
      set.push_back(document.get());
    shared.documentBuilder->build(set, options);
  }

  // What one of the built documents reported, in the shape the checks read it.
  [[nodiscard]] ct::ValidationResult said(const std::string& uri) const {
    std::shared_ptr<cursiv::Document> document =
        shared.documents->getDocument(uri);
    return ct::ValidationResult{{},
                                document->diagnostics,
                                std::move(document),
                                &shared.serviceRegistry->getServices(uri)};
  }
};

// The findings with one message, as the filter every check states it in.
ct::ExpectDiagnosticOptions ofMessage(std::string_view message) {
  return {.message = std::string(message)};
}

std::size_t count(const ct::ValidationResult& said, std::string_view message) {
  return ct::filterByOptions(said, ofMessage(message)).size();
}

// The span a finding must cover.
ct::ExpectDiagnosticOptionsWithoutContent at(std::size_t offset,
                                             std::size_t length) {
  return {.offset = static_cast<cursiv::TextOffset>(offset),
          .length = static_cast<cursiv::TextOffset>(length)};
}

constexpr std::string_view kRequirements =
    "environment Win: \"a pc\"\n"
    "environment Linux: \"the ci\"\n"
    "req Req1 \"covered\" applicable for Win\n"
    "req Alpha \"neither numbered nor covered\"\n";

// The `.req` half: a name with no digit, and a requirement no test names.
void a_requirement_says_what_it_lacks() {
  World world;
  world.add("file:///w/a.req", std::string(kRequirements));
  world.add("file:///w/a.tst", "tst T1 tests Req1 applicable for Win\n");
  world.build();
  const ct::ValidationResult said = world.said("file:///w/a.req");

  constexpr std::string_view kUnnumbered =
      "Requirement name Alpha should contain a number.";
  CHECK(count(said, kUnnumbered) == 1);
  ct::expectWarning(said, kUnnumbered, at(kRequirements.find("Alpha"), 5));
  CHECK(count(said, "Requirement Alpha not covered by a test.") == 1);

  // ...and the one the test does name reports neither.
  ct::expectNoIssues(
      said, ofMessage("Requirement name Req1 should contain a number."));
  ct::expectNoIssues(said,
                     ofMessage("Requirement Req1 not covered by a test."));
}

// The same `.req`, with nothing beside it: coverage is a fact about the
// workspace, so removing the test changes the answer for a requirement the
// document itself did not touch.
void an_uncovered_requirement_is_the_workspace_speaking() {
  World world;
  world.add("file:///w/a.req", std::string(kRequirements));
  world.build();
  CHECK(count(world.said("file:///w/a.req"),
              "Requirement Req1 not covered by a test.")
        == 1);
}

// The `.tst` half: a name with no digit, and an environment none of the
// requirements it names is applicable for.
void a_test_says_what_it_lacks() {
  World world;
  world.add("file:///w/a.req", std::string(kRequirements));
  constexpr std::string_view kTests =
      "tst Beta tests Req1 applicable for Linux\n";
  world.add("file:///w/a.tst", std::string(kTests));
  world.build();
  const ct::ValidationResult said = world.said("file:///w/a.tst");

  constexpr std::string_view kUnnumbered =
      "Test name Beta should contain a number.";
  CHECK(count(said, kUnnumbered) == 1);
  ct::expectWarning(said, kUnnumbered, at(kTests.find("Beta"), 4));

  constexpr std::string_view kUnusedEnvironment =
      "Test Beta references environment Linux which is not used by any "
      "referenced requirement.";
  CHECK(count(said, kUnusedEnvironment) == 1);
  ct::expectWarning(said, kUnusedEnvironment, at(kTests.find("Linux"), 5));

  // A long list reports the same, at each unused entry.
  World many;
  many.add("file:///w/a.req", std::string(kRequirements));
  std::string list = "tst T2 tests Req1 applicable for Win";
  for (int entry = 0; entry < 10; ++entry)
    list += entry % 3 == 0 ? ", Linux" : ", Win";
  many.add("file:///w/a.tst", list + "\n");
  many.build();
  const ct::ValidationResult manySaid = many.said("file:///w/a.tst");
  constexpr std::string_view kUnusedInList =
      "Test T2 references environment Linux which is not used by any "
      "referenced requirement.";
  CHECK(count(manySaid, kUnusedInList) == 4);
  for (std::size_t pos = list.find("Linux"); pos != std::string::npos;
       pos = list.find("Linux", pos + 1))
    ct::expectWarning(manySaid, kUnusedInList, at(pos, 5));
}

// The numbered test naming an environment its requirement declares: nothing to
// report, on either side.
void a_covered_and_numbered_pair_says_nothing() {
  World world;
  world.add("file:///w/a.req",
            "environment Win: \"a pc\"\n"
            "req Req1 \"covered\" applicable for Win\n");
  world.add("file:///w/a.tst", "tst T1 tests Req1 applicable for Win\n");
  world.build();

  ct::expectNoIssues(world.said("file:///w/a.req"));
  ct::expectNoIssues(world.said("file:///w/a.tst"));
}

// A workspace on a disk the test writes: each write is reported as an editor
// saving those files would report it.
struct Disk {
  cursiv::MemoryFileSystemProvider* files = nullptr;
  req::RequirementsCoreLanguage services = make(files);

  static req::RequirementsCoreLanguage make(
      cursiv::MemoryFileSystemProvider*& files) {
    auto disk = std::make_unique<cursiv::MemoryFileSystemProvider>();
    files = disk.get();
    return req::create_core_services(std::move(disk));
  }

  void write(std::string_view uri, std::string text) {
    files->addFile(uri, std::move(text));
    const std::string_view changed[] = {uri};
    services.shared->documentBuilder->update(changed, {});
  }

  void remove(std::string_view uri) {
    const std::string_view deleted[] = {uri};
    services.shared->documentBuilder->update({}, deleted);
  }

  [[nodiscard]] std::size_t said(std::string_view uri,
                                 std::string_view message) const {
    const auto document = services.shared->documents->getDocument(uri);
    CHECK(document != nullptr);
    if (document == nullptr) return 0;
    return static_cast<std::size_t>(std::ranges::count_if(
        document->diagnostics,
        [&](const cursiv::Diagnostic& d) { return d.message == message; }));
  }
};

// A `.req` names no `.tst`, so a test written, edited or deleted relinks no
// requirement document: the coverage it reports follows all the same.
void coverage_follows_the_tests_that_change() {
  constexpr std::string_view kReq = "file:///w/a.req";
  constexpr std::string_view kTst = "file:///w/a.tst";
  constexpr std::string_view kUncovered =
      "Requirement R1 not covered by a test.";
  Disk disk;
  disk.write(kReq, "req R1 \"covered later\"\n");
  CHECK(disk.said(kReq, kUncovered) == 1);
  disk.write(kTst, "tst T1 tests R1\n");
  CHECK(disk.said(kReq, kUncovered) == 0);
  disk.write(kTst, "tst T1 tests R2\n");
  CHECK(disk.said(kReq, kUncovered) == 1);
  disk.write(kTst, "tst T1 tests R1\n");
  CHECK(disk.said(kReq, kUncovered) == 0);
  disk.remove(kTst);
  CHECK(disk.said(kReq, kUncovered) == 1);
}

// The test stays unchanged while a `.req` is reparsed: coverage must follow
// the new requirement nodes, including a renamed target the old test misses.
void coverage_follows_a_requirements_only_update() {
  constexpr std::string_view kReq = "file:///w/a.req";
  constexpr std::string_view kTst = "file:///w/a.tst";
  constexpr std::string_view kR1 = "Requirement R1 not covered by a test.";
  constexpr std::string_view kR2 = "Requirement R2 not covered by a test.";
  Disk disk;
  disk.write(kReq, "req R1 \"original\"\n");
  disk.write(kTst, "tst T1 tests R1\n");
  CHECK(disk.said(kReq, kR1) == 0);
  disk.write(kReq, "req R1 \"reparsed requirement\"\n");
  CHECK(disk.said(kReq, kR1) == 0);
  disk.write(kReq, "req R2 \"renamed\"\n");
  CHECK(disk.said(kReq, kR2) == 1);
  disk.write(kReq, "req R1 \"restored\"\n");
  CHECK(disk.said(kReq, kR1) == 0);
}

// Two `.req` files declare R1, and the test's R1 resolves to the first in URI
// order. Renaming, restoring or deleting that one moves the coverage to the
// other file and back, though the test is never edited.
void coverage_follows_a_test_retargeted_to_a_homonym() {
  constexpr std::string_view kA = "file:///w/a.req";
  constexpr std::string_view kB = "file:///w/b.req";
  constexpr std::string_view kTst = "file:///w/t.tst";
  constexpr std::string_view kR1 = "Requirement R1 not covered by a test.";
  constexpr std::string_view kR2 = "Requirement R2 not covered by a test.";
  Disk disk;
  disk.write(kA, "req R1 \"first\"\n");
  disk.write(kB, "req R1 \"second\"\n");
  disk.write(kTst, "tst T1 tests R1\n");
  // The document the test's one reference resolves into.
  const auto target = [&]() -> std::string {
    const auto test = disk.services.shared->documents->getDocument(kTst);
    const auto* const model =
        test == nullptr ? nullptr
                        : test->parseResult.valueAs<req::ast::TestModel>();
    if (model == nullptr || model->tests.empty()
        || model->tests.front()->requirements.empty())
      return {};
    const cursiv::AstNode* const node =
        model->tests.front()->requirements.front().get();
    const cursiv::Document* const nodeDocument =
        node == nullptr ? nullptr : node->getDocument();
    return nodeDocument == nullptr ? std::string{} : nodeDocument->uri;
  };
  CHECK(target() == kA);
  CHECK(disk.said(kA, kR1) == 0);
  CHECK(disk.said(kB, kR1) == 1);

  disk.write(kA, "req R2 \"renamed\"\n");
  CHECK(target() == kB);
  CHECK(disk.said(kA, kR2) == 1);
  CHECK(disk.said(kB, kR1) == 0);

  disk.write(kA, "req R1 \"restored\"\n");
  CHECK(target() == kA);
  CHECK(disk.said(kA, kR1) == 0);
  CHECK(disk.said(kB, kR1) == 1);

  disk.remove(kA);
  CHECK(target() == kB);
  CHECK(disk.said(kB, kR1) == 0);
}

// A failed scan publishes no answer. The next check retries instead of using
// an incomplete coverage set or treating the failed attempt as a cached empty.
void a_failed_coverage_scan_can_be_retried() {
  struct FailingDocuments : cursiv::DefaultDocuments {
    using cursiv::DefaultDocuments::DefaultDocuments;
    mutable bool failNext = false;
    mutable int scans = 0;
    bool counting = false;

    std::vector<std::shared_ptr<cursiv::Document>> all() const override {
      if (counting) {
        ++scans;
        if (std::exchange(failNext, false))
          throw std::runtime_error("coverage scan failed");
      }
      return cursiv::DefaultDocuments::all();
    }
  };
  World world;
  auto store = std::make_unique<FailingDocuments>(world.shared);
  auto& counted = *store;
  world.shared.documents = std::move(store);
  world.add("file:///w/a.req", "req R1 \"covered\"\n");
  world.add("file:///w/a.tst", "tst T1 tests R1\n");
  cursiv::BuildOptions options;
  options.validation = false;
  world.build(options);
  const auto& document = *world.opened.front();
  const auto& requirement = *static_cast<const req::ast::RequirementModel*>(
                                 document.parseResult.value)
                                 ->requirements[0];
  int diagnostics = 0;
  const auto sink = [&](cursiv::Diagnostic&&) { ++diagnostics; };
  cursiv::ValidationAcceptor accept(sink, document, "requirements");
  counted.counting = true;
  counted.failNext = true;
  bool failed = false;
  try {
    world.services.requirements.requirementsValidator
        ->checkRequirementIsCoveredByATest(requirement, accept);
  } catch (const std::runtime_error&) {
    failed = true;
  }
  CHECK(failed);
  world.services.requirements.requirementsValidator
      ->checkRequirementIsCoveredByATest(requirement, accept);
  CHECK(counted.scans == 2);
  CHECK(diagnostics == 0);
}

// The same through `build`, as a first scan or a command line builds: a test
// built after the `.req` it covers clears the warning, and the `.req` is
// validated again by that build, not left without findings.
void coverage_follows_a_test_built_later() {
  constexpr std::string_view kUncovered =
      "Requirement R1 not covered by a test.";
  constexpr std::string_view kNever = "Requirement R2 not covered by a test.";
  World world;
  world.add("file:///w/a.req",
            "req R1 \"covered later\"\nreq R2 \"never covered\"\n");
  world.build();
  CHECK(count(world.said("file:///w/a.req"), kUncovered) == 1);
  CHECK(count(world.said("file:///w/a.req"), kNever) == 1);

  world.add("file:///w/a.tst", "tst T1 tests R1\n");
  cursiv::Document* const test = world.opened.back().get();
  world.shared.documentBuilder->build(std::span(&test, 1));
  const std::shared_ptr<cursiv::Document> requirements =
      world.shared.documents->getDocument("file:///w/a.req");
  CHECK(requirements->state.load() == cursiv::DocumentState::Validated);
  CHECK(count(world.said("file:///w/a.req"), kUncovered) == 0);
  CHECK(count(world.said("file:///w/a.req"), kNever) == 1);
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  CHECK(in.is_open());
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The summary the shipped pair becomes, whole and byte for byte: generated
// output rarely changes, and a change anywhere in it is one to read.
void the_shipped_pair_generates_its_golden_summary() {
  const std::filesystem::path root(CURSIV_EXAMPLE_DIR);
  World world;
  world.add("file:///requirements.req",
            read_file(root / "example" / "requirements.req"));
  world.add("file:///tests.tst", read_file(root / "example" / "tests.tst"));
  world.build();
  const auto* const model = cursiv::ast_cast<const req::ast::RequirementModel>(
      world.opened[0]->parseResult.value);
  const auto* const tests = cursiv::ast_cast<const req::ast::TestModel>(
      world.opened[1]->parseResult.value);
  CHECK(model != nullptr && tests != nullptr);
  if (model == nullptr || tests == nullptr) return;
  const req::ast::TestModel* const testModels[] = {tests};
  // The golden file spells the sources as a POSIX host does; each host writes
  // its own path for them.
  std::string expected =
      read_file(root / "tests" / "golden" / "requirements.html");
  for (const std::string_view uri :
       {"file:///requirements.req", "file:///tests.tst"}) {
    const std::string posix(uri.substr(std::string_view("file://").size()));
    const std::string native =
        cursiv::file_uri_to_path(uri).value_or(std::string(uri));
    for (std::size_t at = expected.find(posix); at != std::string::npos;
         at = expected.find(posix, at + native.size()))
      expected.replace(at, posix.size(), native);
  }
  CHECK(req::generate_summary_file_html_content(*model, testModels)
        == expected);
}

// A `testfile` clause is spaced whenever it is written, in either case of
// its keyword, and also when its string is empty.
void a_testfile_clause_is_spaced_even_when_empty() {
  const req::RequirementsLanguage editor = req::create_lsp_services();
  for (const std::string_view file : {"\"t.tst\"", "\"\""})
    for (const std::string_view keyword : {"testfile", "TESTFILE"}) {
      const std::string clause(keyword);
      (void)ct::expectFormatting(
          editor.tests, {.before = "tst T1   " + clause + "="
                                   + std::string(file) + "   tests R1\n",
                         .after = "tst T1 " + clause + " = " + std::string(file)
                                  + " tests R1\n",
                         .disposeAfterCheck = true});
    }
}

#if !defined(__wasi__) || defined(_REENTRANT)
void simultaneous_coverage_checks_scan_the_workspace_once() {
  struct CountingDocuments : cursiv::DefaultDocuments {
    using cursiv::DefaultDocuments::DefaultDocuments;
    bool counting = false;
    mutable std::atomic<int> scans{0};

    std::vector<std::shared_ptr<cursiv::Document>> all() const override {
      if (counting) {
        ++scans;
        // Keep the first miss open while the other validators arrive.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      return cursiv::DefaultDocuments::all();
    }
  };

  World world;
  auto store = std::make_unique<CountingDocuments>(world.shared);
  auto& counted = *store;
  world.shared.documents = std::move(store);
  world.add("file:///w/a.req", "req Req1 \"covered\"\n");
  world.add("file:///w/a.tst", "tst T1 tests Req1\n");
  cursiv::BuildOptions options;
  options.validation = false;
  world.build(options);
  const auto& document = *world.opened.front();
  const auto& requirement = *static_cast<const req::ast::RequirementModel*>(
                                 document.parseResult.value)
                                 ->requirements[0];
  counted.counting = true;

  const auto checkTogether = [&] {
    std::barrier start(4);
    std::atomic<int> diagnostics{0};
    std::vector<std::thread> validators;
    for (int i = 0; i < 4; ++i)
      validators.emplace_back([&] {
        const auto sink = [&](cursiv::Diagnostic&&) { ++diagnostics; };
        cursiv::ValidationAcceptor accept(sink, document, "requirements");
        start.arrive_and_wait();
        world.services.requirements.requirementsValidator
            ->checkRequirementIsCoveredByATest(requirement, accept);
      });
    for (auto& validator : validators) validator.join();
    CHECK(counted.scans.load() == 1);
    CHECK(diagnostics.load() == 0);
  };
  checkTogether();

  // An empty build still starts a new workspace generation. It keeps the
  // trees intact but invalidates a workspace answer, just as a changed URI
  // does.
  counted.counting = false;
  world.shared.documentBuilder->build({}, options);
  counted.scans.store(0);
  counted.counting = true;
  checkTogether();
}
#endif

}  // namespace

int main() {
  cursiv::test::count_expectations();
  a_requirement_says_what_it_lacks();
  an_uncovered_requirement_is_the_workspace_speaking();
  a_test_says_what_it_lacks();
  a_covered_and_numbered_pair_says_nothing();
  coverage_follows_the_tests_that_change();
  coverage_follows_a_requirements_only_update();
  coverage_follows_a_test_retargeted_to_a_homonym();
  a_failed_coverage_scan_can_be_retried();
  coverage_follows_a_test_built_later();
  the_shipped_pair_generates_its_golden_summary();
  a_testfile_clause_is_spaced_even_when_empty();
#if !defined(__wasi__) || defined(_REENTRANT)
  simultaneous_coverage_checks_scan_the_workspace_once();
#endif
  return cursiv::test::report("requirements_grammar_test");
}
