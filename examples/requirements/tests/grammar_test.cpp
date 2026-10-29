// The four checks of the two-language example: two of them read across
// documents. Whether a requirement is covered is a fact about the `.tst`
// files beside it, and whether a test's environment is legitimate is a fact
// about the `.req` it names -- so the world here holds both, built together.

#include <cstddef>
#include <memory>
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

#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "cursiv/core/workspace/BuildOptions.hpp"
#include "cursiv/core/workspace/DefaultDocuments.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/test/Check.hpp"
#include "cursiv/test/ExpectDiagnosticOptions.hpp"
#include "cursiv/test/ValidationResult.hpp"
#include "requirements/core/CoreModule.hpp"
#include "requirements/core/ast.hpp"
#include "requirements/core/validation/RequirementsValidator.hpp"

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

// The findings that say one sentence, as the filter every check states it in.
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

  // ...and the one the test does name says neither.
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

  constexpr std::string_view kStrayEnvironment =
      "Test Beta references environment Linux which is not used by any "
      "referenced requirement.";
  CHECK(count(said, kStrayEnvironment) == 1);
  ct::expectWarning(said, kStrayEnvironment, at(kTests.find("Linux"), 5));
}

// The numbered test naming an environment its requirement declares: nothing to
// say, on either side.
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
}
#endif

}  // namespace

int main() {
  cursiv::test::count_expectations();
  a_requirement_says_what_it_lacks();
  an_uncovered_requirement_is_the_workspace_speaking();
  a_test_says_what_it_lacks();
  a_covered_and_numbered_pair_says_nothing();
#if !defined(__wasi__) || defined(_REENTRANT)
  simultaneous_coverage_checks_scan_the_workspace_once();
#endif
  return cursiv::test::report("requirements_grammar_test");
}
