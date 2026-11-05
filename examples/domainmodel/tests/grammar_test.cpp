// The domain model's three checks, each said and each silent: the naming rule
// over the whole `Type` family -- one sentence for a datatype and an entity --
// and the cycle in `extends`, which is the one check no single node can answer.

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/services/CoreLanguage.hpp"
#include "cursiv/services/Diagnostic.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/MemoryFileSystemProvider.hpp"
#include "cursiv/Check.hpp"
#include "cursiv/ExpectDiagnosticOptions.hpp"
#include "cursiv/ValidationHelper.hpp"
#include "cursiv/ValidationResult.hpp"
#include "domainmodel/CoreServices.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"

namespace {

namespace ct = cursiv::test;

using Services = dm::DomainModelCoreServices;
using Shared = dm::DomainModelSharedCoreServices;

constexpr std::string_view kCapital = "Type name should start with a capital.";
constexpr std::string_view kCycle =
    "Entity extends itself, directly or through a cycle.";

struct World {
  cursiv::CoreLanguage<Services, Shared> services =
      cursiv::create_language<Services, Shared, dm::install_core_module>();

  [[nodiscard]] ct::ValidationResult validate(std::string text) const {
    return ct::validationHelper(services.language, std::move(text));
  }
};

// How many findings say that sentence, whatever else they carry.
std::size_t count(const ct::ValidationResult& said, std::string_view message) {
  return ct::filterByOptions(said, {.message = std::string(message)}).size();
}

// The span a finding must cover, as the filter every check below states it in.
ct::ExpectDiagnosticOptionsWithoutContent at(std::size_t offset,
                                             std::size_t length) {
  return {.offset = static_cast<cursiv::TextOffset>(offset),
          .length = static_cast<cursiv::TextOffset>(length)};
}

void a_lowercase_entity_is_a_warning_on_its_name() {
  const World world;
  const std::string text = "entity blog {\n}\n";
  ct::expectWarning(world.validate(text), kCapital, at(text.find("blog"), 4));
}

// The second method, and the same sentence: the rule is over `Type`, not over
// one of its leaves.
void a_lowercase_datatype_says_the_same_sentence() {
  const World world;
  const std::string text = "datatype string\n";
  ct::expectWarning(world.validate(text), kCapital, at(text.find("string"), 6));
}

void capitalised_names_say_nothing() {
  const World world;
  ct::expectNoIssues(
      world.validate("datatype String\n"
                     "entity Blog {\n"
                     "  title: String\n"
                     "}\n"));
}

void an_entity_extending_itself_is_an_error() {
  const World world;
  const std::string text = "entity A extends A {\n}\n";
  ct::expectError(world.validate(text), kCycle, at(text.rfind('A'), 1));
}

void a_cycle_through_a_third_entity_is_an_error_on_each_storey() {
  const World world;
  const ct::ValidationResult said = world.validate(
      "entity A extends B {\n}\n"
      "entity B extends C {\n}\n"
      "entity C extends B {\n}\n");
  CHECK(count(said, kCycle) == 3);
}

void a_chain_that_ends_says_nothing() {
  const World world;
  ct::expectNoIssues(
      world.validate("entity A {\n}\n"
                     "entity B extends A {\n}\n"
                     "entity C extends B {\n}\n"));
}

// A workspace on a disk the test writes: each write is announced as an editor
// saving those files would announce it.
struct Disk {
  cursiv::MemoryFileSystemProvider* files = nullptr;
  cursiv::CoreLanguage<Services, Shared> services = make(files);

  static cursiv::CoreLanguage<Services, Shared> make(
      cursiv::MemoryFileSystemProvider*& files) {
    auto disk = std::make_unique<cursiv::MemoryFileSystemProvider>();
    files = disk.get();
    return cursiv::create_language<Services, Shared, dm::install_core_module>(
        std::move(disk));
  }

  void write(const std::vector<std::pair<std::string, std::string>>& written) {
    std::vector<std::string_view> uris;
    for (const auto& [uri, text] : written) {
      files->addFile(uri, text);
      uris.emplace_back(uri);
    }
    services.shared->documentBuilder->update(uris, {});
  }

  [[nodiscard]] std::size_t cycles(std::string_view uri) const {
    const auto document = services.shared->documents->getDocument(uri);
    CHECK(document != nullptr);
    if (document == nullptr) return 0;
    return static_cast<std::size_t>(std::ranges::count_if(
        document->diagnostics,
        [](const cursiv::Diagnostic& d) { return d.message == kCycle; }));
  }
};

constexpr std::string_view kA = "file:///a.dmodel";
constexpr std::string_view kB = "file:///b.dmodel";
constexpr std::string_view kC = "file:///c.dmodel";

// The climb crosses documents, so an edit in one changes the answer in the
// other: after the edit, both say what a fresh build says.
void a_cycle_closed_in_another_document_is_an_error_in_both() {
  Disk disk;
  disk.write({{std::string(kA), "entity A extends B {\n}\n"},
              {std::string(kB), "entity B {\n}\n"}});
  CHECK(disk.cycles(kA) == 0);
  CHECK(disk.cycles(kB) == 0);
  disk.write({{std::string(kB), "entity B extends A {\n}\n"}});
  CHECK(disk.cycles(kA) == 1);
  CHECK(disk.cycles(kB) == 1);
}

// The same through a document the edit does not touch: `a` names nothing
// that changed, but the climbs from `b` and `c` go through it. `E` is on no
// loop, but its climb never ends either.
void a_cycle_through_an_untouched_document_is_an_error() {
  Disk disk;
  disk.write({{std::string(kA), "entity A extends B {\n}\n"},
              {std::string(kB), "entity B extends C {\n}\n"},
              {std::string(kC), "entity C {\n}\n"}});
  CHECK(disk.cycles(kB) == 0);
  CHECK(disk.cycles(kC) == 0);
  disk.write({{std::string(kC),
               "entity C extends A {\n}\n"
               "entity E extends C {\n}\n"}});
  CHECK(disk.cycles(kB) == 1);
  CHECK(disk.cycles(kC) == 2);
}

}  // namespace

// A token that opens a rule reached through a chain: the state has no shift
// for it -- an element must be reduced first -- so the grammar names the member
// instead, and only because every call site of that rule spells the same one.
// `Model.elements` and `Package.elements` are two rows and one name.
void a_token_that_opens_an_element_names_the_member_it_fills() {
  const std::unique_ptr<cursiv::Parser> engine =
      dm::generated::DomainModel::create_parser();
  const std::string src = "datatype String\n";
  const cursiv::ParseResult r = engine->parse(src);
  std::vector<cursiv::NextFeature> out;
  const auto at = static_cast<cursiv::TextOffset>(src.size());
  engine->nextFeaturesAt(r.cst(), at, at, out);
  CHECK(!out.empty());
  std::size_t named = 0, opens = 0;
  for (const cursiv::NextFeature& f : out) {
    const bool opener = std::ranges::any_of(
        engine->getGrammar().terminalSpellings(f.feature),
        [](std::string_view text) {
          return text == "entity" || text == "datatype" || text == "package";
        });
    opens += opener ? 1u : 0u;
    if (opener) CHECK(f.property == "elements");
    named += f.property.empty() ? 0u : 1u;
  }
  CHECK(opens == 3);
  // ...and a token that closes one opens nothing, so it names nothing.
  CHECK(named == opens);
}

int main() {
  cursiv::test::count_expectations();
  a_lowercase_entity_is_a_warning_on_its_name();
  a_lowercase_datatype_says_the_same_sentence();
  capitalised_names_say_nothing();
  an_entity_extending_itself_is_an_error();
  a_cycle_through_a_third_entity_is_an_error_on_each_storey();
  a_chain_that_ends_says_nothing();
  a_cycle_closed_in_another_document_is_an_error_in_both();
  a_cycle_through_an_untouched_document_is_an_error();
  a_token_that_opens_an_element_names_the_member_it_fills();
  return cursiv::test::report("domainmodel_grammar_test");
}
