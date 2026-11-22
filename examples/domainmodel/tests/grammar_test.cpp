// The domain model's three checks, each reporting and each silent: the naming
// rule over the whole `Type` family (one message for a datatype and an entity)
// and the cycle in `extends`, which is the one check no single node can answer.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "cursiv/Check.hpp"
#include "cursiv/ExpectDiagnosticOptions.hpp"
#include "cursiv/ValidationHelper.hpp"
#include "cursiv/ValidationResult.hpp"
#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/references/ScopeComputation.hpp"
#include "cursiv/services/CoreLanguage.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "cursiv/workspace/DefaultDocuments.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/MemoryFileSystemProvider.hpp"
#include "domainmodel/CoreServices.hpp"
#include "domainmodel/Generator.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"
#include "domainmodel/validation/DomainModelValidator.hpp"

namespace {

namespace ct = cursiv::test;

using Services = dm::DomainModelCoreServices;
using Shared = dm::DomainModelSharedCoreServices;

constexpr std::string_view kCapital = "Type name should start with a capital.";
constexpr std::string_view kCycle =
    "Entity extends itself, directly or through a cycle.";

struct World {
  cursiv::CoreLanguage<Services, Shared> services =
      cursiv::createLanguage<Services, Shared, dm::install_core_module>();

  [[nodiscard]] ct::ValidationResult validate(std::string text) const {
    return ct::validationHelper(services.language, std::move(text));
  }
};

// How many findings carry that message, whatever else they carry.
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

// The second method, and the same message: the rule is over `Type`, not over
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

void a_cycle_through_a_third_entity_is_an_error_on_each_level() {
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

// A workspace on a disk the test writes: each write is reported as an editor
// saving those files would report it.
struct Disk {
  cursiv::MemoryFileSystemProvider* files = nullptr;
  cursiv::CoreLanguage<Services, Shared> services = make(files);

  static cursiv::CoreLanguage<Services, Shared> make(
      cursiv::MemoryFileSystemProvider*& files) {
    auto disk = std::make_unique<cursiv::MemoryFileSystemProvider>();
    files = disk.get();
    return cursiv::createLanguage<Services, Shared, dm::install_core_module>(
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

// The walk crosses documents, so an edit in one changes the answer in the
// other: after the edit, both report what a fresh build reports.
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

// The same for `a`, which the edit does not touch: its result changes when the
// loop closes in `c`, and again when it opens. `E` is on no loop, but its walk
// never ends either.
void a_cycle_through_an_untouched_document_is_an_error() {
  Disk disk;
  disk.write({{std::string(kA), "entity A extends B {\n}\n"},
              {std::string(kB), "entity B extends C {\n}\n"},
              {std::string(kC), "entity C {\n}\n"}});
  CHECK(disk.cycles(kA) == 0);
  CHECK(disk.cycles(kB) == 0);
  CHECK(disk.cycles(kC) == 0);
  disk.write({{std::string(kC),
               "entity C extends A {\n}\n"
               "entity E extends C {\n}\n"}});
  CHECK(disk.cycles(kA) == 1);
  CHECK(disk.cycles(kB) == 1);
  CHECK(disk.cycles(kC) == 2);
  disk.write({{std::string(kC), "entity C {\n}\n"}});
  CHECK(disk.cycles(kA) == 0);
  CHECK(disk.cycles(kB) == 0);
  CHECK(disk.cycles(kC) == 0);
}

// A loop closed two documents away: `a` names only `b`, and the edit is in
// `c`, which `b` names.
void a_cycle_closed_two_documents_away_is_an_error_at_the_start() {
  Disk disk;
  disk.write({{std::string(kA), "entity A extends B {\n}\n"},
              {std::string(kB), "entity B extends C {\n}\n"},
              {std::string(kC), "entity C {\n}\n"}});
  CHECK(disk.cycles(kA) == 0);
  disk.write({{std::string(kC), "entity C extends C {\n}\n"}});
  CHECK(disk.cycles(kA) == 1);
  CHECK(disk.cycles(kB) == 1);
  CHECK(disk.cycles(kC) == 1);
}

// A new `B` in `a` takes over the reference of `c`, which the edit does not
// touch: the loop it closes is an error, and it goes when `d` opens it.
void a_loop_closed_through_a_homonym_comes_and_goes() {
  Disk disk;
  const std::string kD = "file:///d.dmodel";
  disk.write({{std::string(kA), "entity C {\n}\n"},
              {std::string(kB), "entity B {\n}\n"},
              {std::string(kC), "entity A extends B {\n}\n"},
              {kD, "entity Z extends A {\n}\n"}});
  disk.write({{std::string(kA), "entity B extends Z {\n}\n"}});
  CHECK(disk.cycles(kA) == 1);
  CHECK(disk.cycles(kC) == 1);
  disk.write({{kD, "entity Z {\n}\n"}});
  CHECK(disk.cycles(kA) == 0);
  CHECK(disk.cycles(kC) == 0);
  CHECK(disk.cycles(kD) == 0);
}

// A chain over many documents, each entity extending the next one's. An edit
// looks up the document of each step the walks took once, not once per pair
// of documents, and a loop closed at the far end reaches every document.
void a_chain_over_many_documents_is_looked_up_once_per_edit() {
  struct CountingDocuments : cursiv::DefaultDocuments {
    using cursiv::DefaultDocuments::DefaultDocuments;
    using cursiv::DefaultDocuments::getDocument;
    mutable std::atomic<std::size_t> lookups{0};

    std::shared_ptr<cursiv::Document> getDocument(
        cursiv::DocumentId id) const override {
      ++lookups;
      return cursiv::DefaultDocuments::getDocument(id);
    }
  };
  constexpr std::size_t kLength = 200;
  Disk disk;
  auto store = std::make_unique<CountingDocuments>(*disk.services.shared);
  const CountingDocuments& counted = *store;
  disk.services.shared->documents = std::move(store);

  const auto uri = [](std::size_t i) {
    return "file:///f" + std::to_string(i) + ".dmodel";
  };
  const auto extending = [](std::size_t i, std::size_t super) {
    return "entity E" + std::to_string(i) + " extends E" + std::to_string(super)
           + " {\n}\n";
  };
  std::vector<std::pair<std::string, std::string>> chain;
  for (std::size_t i = 0; i + 1 < kLength; ++i)
    chain.emplace_back(uri(i), extending(i, i + 1));
  chain.emplace_back(uri(kLength - 1),
                     "entity E" + std::to_string(kLength - 1) + " {\n}\n");
  disk.write(chain);
  CHECK(disk.cycles(uri(0)) == 0);

  counted.lookups = 0;
  disk.write({{uri(0), extending(0, 1)}});
  CHECK(counted.lookups.load() < 4 * kLength);

  disk.write({{uri(kLength - 1), extending(kLength - 1, 0)}});
  std::size_t cycles = 0;
  for (std::size_t i = 0; i < kLength; ++i) cycles += disk.cycles(uri(i));
  CHECK(cycles == kLength);
}

// An edit sends back to validation the documents whose walks go through it,
// not those that only share a document with such a walk, nor one whose walks
// no longer go through it.
void an_edit_revalidates_only_the_walks_through_it() {
  struct Validated : cursiv::DocumentPhaseListener {
    std::atomic<std::size_t> count{0};
    void onDocumentPhase(cursiv::Document&, cursiv::DocumentState reached,
                         const cursiv::CancellationToken&) override {
      if (reached == cursiv::DocumentState::Validated) ++count;
    }
  };
  constexpr std::size_t kSharing = 20;
  Disk disk;
  Validated validated;
  disk.services.shared->documentBuilder->addDocumentPhaseListener(validated);
  const std::string lib = "file:///lib.dmodel";
  const std::string mid = "file:///mid.dmodel";
  const std::string base = "file:///base.dmodel";
  std::vector<std::pair<std::string, std::string>> all{
      {lib, "entity Named {\n}\n"},
      {mid, "entity Mid extends Named {\n}\n"},
      {base, "entity Person extends Mid {\n}\nentity Thing {\n}\n"}};
  for (std::size_t i = 0; i < kSharing; ++i)
    all.emplace_back("file:///x" + std::to_string(i) + ".dmodel",
                     "entity X" + std::to_string(i) + " extends Thing {\n}\n");
  disk.write(all);

  validated.count = 0;
  disk.write({{lib, "entity Named {\n}\n"}});
  CHECK(validated.count.load() == 3);

  // `x0` no longer goes on into `base`: an edit of `base` revalidates it and
  // the other `x` documents.
  disk.write({{"file:///x0.dmodel", "entity X0 {\n}\n"}});
  validated.count = 0;
  disk.write({{base, "entity Person extends Mid {\n}\nentity Thing {\n}\n"}});
  CHECK(validated.count.load() == kSharing);
  disk.services.shared->documentBuilder->removeDocumentPhaseListener(validated);
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  CHECK(in.is_open());
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

#if defined(__linux__)
// A worker's stack can be small -- half a mebibyte on macOS -- so a walk
// holds no frame per entity.
#ifdef NDEBUG
constexpr std::size_t kSmallStack = 64 * 1024;
#else
constexpr std::size_t kSmallStack = 256 * 1024;
#endif

struct Climb {
  const dm::DomainModelValidator* validator = nullptr;
  const cursiv::Document* document = nullptr;
  const dm::ast::Entity* head = nullptr;
  std::size_t cycles = 0;
};

void* climb(void* arg) {
  auto* job = static_cast<Climb*>(arg);
  std::vector<cursiv::Diagnostic> found;
  const auto collect = [&found](cursiv::Diagnostic&& diagnostic) {
    found.push_back(std::move(diagnostic));
  };
  cursiv::ValidationAcceptor accept(cursiv::ValidationAcceptor::Sink(collect),
                                    *job->document, "domainmodel");
  job->validator->checkEntityDoesNotExtendItself(*job->head, accept);
  job->cycles = static_cast<std::size_t>(std::ranges::count_if(
      found, [](const cursiv::Diagnostic& d) { return d.message == kCycle; }));
  return nullptr;
}

// A chain written head first, each entity extending the next, once ending and
// once looping back to its middle. The walk on the small stack runs on a fresh
// validator.
void a_long_chain_written_head_first_fits_a_small_stack() {
  constexpr std::size_t kLevels = 4000;
  for (const bool loops : {false, true}) {
    std::string text;
    for (std::size_t i = 0; i < kLevels; ++i) {
      text += "entity E" + std::to_string(i);
      if (i + 1 < kLevels)
        text += " extends E" + std::to_string(i + 1);
      else if (loops)
        text += " extends E" + std::to_string(kLevels / 2);
      text += " {\n}\n";
    }
    const World world;
    const ct::ValidationResult said = world.validate(text);
    CHECK(count(said, kCycle) == (loops ? kLevels : 0));

    const dm::DomainModelValidator fresh(world.services.language);
    Climb job{.validator = &fresh, .document = said.document.get()};
    for (const cursiv::AstNode* node : said.document->parseResult.nodes())
      if (node->type == &cursiv::kAstTypeTag<dm::ast::Entity>
          && static_cast<const dm::ast::Entity*>(node)->name == "E0")
        job.head = static_cast<const dm::ast::Entity*>(node);
    CHECK(job.head != nullptr);
    if (job.head == nullptr) continue;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kSmallStack);
    pthread_t th;
    const bool started = pthread_create(&th, &attr, &climb, &job) == 0;
    pthread_attr_destroy(&attr);
    CHECK(started);
    if (!started) continue;
    pthread_join(th, nullptr);
    CHECK(job.cycles == (loops ? 1 : 0));
  }
}

struct Scopes {
  const dm::DomainModelCoreServices* services = nullptr;
  const cursiv::Document* document = nullptr;
  std::filesystem::path out;
  std::size_t local = 0;
  std::size_t exported = 0;
  bool generated = false;
};

void* scope(void* arg) {
  auto* job = static_cast<Scopes*>(arg);
  job->local =
      job->services->scopeComputation->collectLocalSymbols(*job->document)
          .size();
  job->exported =
      job->services->scopeComputation->collectExportedSymbols(*job->document)
          .size();
  const auto* const model = cursiv::ast_cast<const dm::ast::DomainModel>(
      job->document->parseResult.value);
  try {
    (void)dm::generate_java(*model, "deep.dmodel", job->out.string());
    job->generated = true;
  } catch (const std::exception&) {
  }
  return nullptr;
}

// Packages nested deep, walked by the scopes and the generator with no native
// frame per package. One-letter names keep the deepest folder a valid path.
void deep_packages_fit_a_small_stack() {
  constexpr std::size_t kLevels = 1500;
  constexpr std::size_t kHalfway = kLevels / 2;
  std::string text;
  std::string qualified;
  for (std::size_t i = 0; i < kLevels; ++i) {
    text += "package p {\n";
    qualified += "p.";
    if (i + 1 == kHalfway) {
      text += "entity M extends ";
      for (std::size_t below = kHalfway; below < kLevels; ++below) text += "p.";
      text += "E {\n}\n";
    }
  }
  text += "entity E {\n}\n";
  for (std::size_t i = 0; i < kLevels; ++i) text += "}\n";
  text += "entity R extends " + qualified + "E {\n}\n";
  const World world;
  const ct::ValidationResult said = world.validate(text);
  ct::expectNoIssues(said);

  const std::filesystem::path out =
      std::filesystem::path(CURSIV_TEST_SCRATCH_DIR) / "generated-deep";
  std::filesystem::remove_all(out);
  Scopes job{.services = &world.services.language,
             .document = said.document.get(),
             .out = out};
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, kSmallStack);
  pthread_t th;
  const bool started = pthread_create(&th, &attr, &scope, &job) == 0;
  pthread_attr_destroy(&attr);
  CHECK(started);
  if (!started) return;
  pthread_join(th, nullptr);
  // `E` and `M` at their level and each one below it, and `R` at the root.
  CHECK(job.local == (kLevels + 1) + (kHalfway + 1) + 1);
  CHECK(job.exported == 3);
  CHECK(job.generated);
  std::string folder = "deep";
  for (std::size_t i = 0; i < kLevels; ++i) folder += "/p";
  qualified.pop_back();
  CHECK(read_file(out / folder / "E.java")
            .starts_with("package deep." + qualified + ";\n"));
  std::filesystem::remove_all(out);
}
#endif

// Every file under `root`, by its path below it.
std::map<std::string, std::string> tree_of(const std::filesystem::path& root) {
  std::map<std::string, std::string> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    if (entry.is_regular_file())
      files[std::filesystem::relative(entry.path(), root).generic_string()] =
          read_file(entry.path());
  return files;
}

// The Java each model becomes, byte for byte. The second model names its
// types in another case than their declarations, which Java would refuse.
void models_generate_their_golden_java() {
  const std::filesystem::path root(CURSIV_EXAMPLE_DIR);
  const std::filesystem::path out =
      std::filesystem::path(CURSIV_TEST_SCRATCH_DIR) / "generated";
  const std::pair<std::string, std::string> models[] = {
      {"blog", read_file(root / "example" / "blog.dmodel")},
      {"cased",
       "datatype String\n"
       "package p {\n"
       "    entity E {\n"
       "        name: STRING\n"
       "    }\n"
       "}\n"
       "entity F extends P.e {\n"
       "    e: P.E\n"
       "}\n"}};
  for (const auto& [name, text] : models) {
    const World world;
    const ct::ValidationResult said = world.validate(text);
    const auto* const model = cursiv::ast_cast<const dm::ast::DomainModel>(
        said.document->parseResult.value);
    CHECK(model != nullptr);
    if (model == nullptr) continue;
    std::filesystem::remove_all(out);
    (void)dm::generate_java(*model, name + ".dmodel", out.string());
    CHECK(tree_of(out / name) == tree_of(root / "tests" / "golden" / name));
  }
  std::filesystem::remove_all(out);
}

}  // namespace

// A token that opens an element names the member it fills, `elements`, since
// every rule that holds elements uses that one name.
void a_token_that_opens_an_element_names_the_member_it_fills() {
  const std::unique_ptr<cursiv::Parser> engine =
      dm::generated::DomainModel::createParser();
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

// Descriptions keep their names after the caller's strings disappear and
// additional names make the document's pool grow.
void qualified_names_borrow_the_documents_own_text() {
  const World world;
  const ct::ValidationResult said =
      world.validate("package p { package q { entity E {} } }");
  ct::expectNoIssues(said);
  const auto* const model = cursiv::ast_cast<const dm::ast::DomainModel>(
      said.document->parseResult.value);
  CHECK(model != nullptr && model->elements.size() == 1);
  if (model == nullptr || model->elements.size() != 1) return;
  const auto* const outer = cursiv::ast_cast<const dm::ast::PackageDeclaration>(
      model->elements.front());
  CHECK(outer != nullptr && outer->elements.size() == 1);
  if (outer == nullptr || outer->elements.size() != 1) return;
  const auto* const inner = cursiv::ast_cast<const dm::ast::PackageDeclaration>(
      outer->elements.front());
  CHECK(inner != nullptr);
  if (inner == nullptr) return;

  const auto& names = *world.services.language.qualifiedNameProvider;
  const auto& document = *said.document;
  std::string_view flat, single, nested;
  {
    std::string qualifier("a\0b", 3), own("E\0F", 3);
    flat = names.keepQualifiedName(document, std::string_view(qualifier), own);
    single = names.keepQualifiedName(document, *outer, own);
    nested = names.keepQualifiedName(document, *inner, own);
    std::fill(qualifier.begin(), qualifier.end(), 'x');
    std::fill(own.begin(), own.end(), 'y');
  }
  const auto empty = names.keepQualifiedName(
      document, std::string_view{}, std::string_view{});
  CHECK(empty.empty() && empty.data() != nullptr);
  for (int i = 0; i < 32; ++i) (void)document.keepText(std::string(4096, 'x'));
  CHECK(flat == std::string_view("a\0b.E\0F", 7));
  CHECK(single == std::string_view("p.E\0F", 5));
  CHECK(nested == std::string_view("p.q.E\0F", 7));
}

int main() {
  cursiv::test::countExpectations();
  a_lowercase_entity_is_a_warning_on_its_name();
  a_lowercase_datatype_says_the_same_sentence();
  capitalised_names_say_nothing();
  an_entity_extending_itself_is_an_error();
  a_cycle_through_a_third_entity_is_an_error_on_each_level();
  a_chain_that_ends_says_nothing();
  a_cycle_closed_in_another_document_is_an_error_in_both();
  a_cycle_through_an_untouched_document_is_an_error();
  a_cycle_closed_two_documents_away_is_an_error_at_the_start();
  a_loop_closed_through_a_homonym_comes_and_goes();
  a_chain_over_many_documents_is_looked_up_once_per_edit();
  an_edit_revalidates_only_the_walks_through_it();
  models_generate_their_golden_java();
  qualified_names_borrow_the_documents_own_text();
#if defined(__linux__)
  a_long_chain_written_head_first_fits_a_small_stack();
  deep_packages_fit_a_small_stack();
#endif
  a_token_that_opens_an_element_names_the_member_it_fills();
  return cursiv::test::report("domainmodel_grammar_test");
}
