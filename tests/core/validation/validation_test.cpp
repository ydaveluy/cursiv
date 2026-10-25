// The validation pass: dispatch, categories and hooks, the acceptor checks
// report through, and the sentence a repair is said in.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/observability/ObservabilitySink.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/references/DefaultScopeProvider.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/core/text/TextRange.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/validation/DefaultCodeActionProtocol.hpp"
#include "cursiv/core/validation/DefaultDocumentValidator.hpp"
#include "cursiv/core/validation/DocumentValidator.hpp"
#include "cursiv/core/validation/RepairMessage.hpp"
#include "cursiv/core/validation/SpellingActions.hpp"
#include "cursiv/core/validation/ValidateSingleNodeOptions.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "cursiv/core/validation/ValidationCategory.hpp"
#include "cursiv/core/validation/ValidationRegistry.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/actions/DefaultCodeActionProvider.hpp"
#include "cursiv/test/Check.hpp"
#include "repairmsg/generated/MessageG/Module.hpp"
#include "workspace/core/ast.hpp"
#include "workspace/generated/Workspace/Module.hpp"

// Pins the validation pass: who runs (family against exact filing, category
// selection, `built_in` reserved), what runs around the walk (the document
// hooks), what a crashing check costs, and the built-in half end to end -- a
// repair's quick fix really fixes, a broken reference names its member.
namespace document_validator {

namespace {

// Everything the runtime says outside the document, kept for the assertions.
struct CaptureSink final : cursiv::ObservabilitySink {
  std::vector<cursiv::Observation> seen;
  void publish(const cursiv::Observation& observation) noexcept override {
    try {
      seen.push_back(observation);
    } catch (...) {
    }
  }
};

// The container-owned instance every registration binds by address.
struct TestValidator {
  mutable int typeFamily = 0;
  mutable int exactType = 0;
  mutable int everyNode = 0;
  mutable std::vector<std::string> events;

  void onType(const ws::ast::Type&, cursiv::ValidationAcceptor&) const {
    ++typeFamily;
  }
  void onExactType(const ws::ast::Type&, cursiv::ValidationAcceptor&) const {
    ++exactType;
  }
  void onEveryNode(const cursiv::AstNode&, cursiv::ValidationAcceptor&) const {
    ++everyNode;
  }
  void onEntity(const ws::ast::Entity& entity,
                cursiv::ValidationAcceptor& accept) const {
    events.emplace_back("entity");
    accept.warning(entity, "entity seen")
        .property<&ws::ast::AbstractElement::name>();
  }
  void onEntityKeyword(const ws::ast::Entity& entity,
                       cursiv::ValidationAcceptor& accept) const {
    accept.hint(entity, "keyword seen").keyword("entity");
  }
  void onFeature(const ws::ast::Feature& feature,
                 cursiv::ValidationAcceptor& accept) const {
    accept.information(feature, "feature seen");
  }
  // Four findings a check emits backwards, two of them sharing a position:
  // what the pass owes is document order, and no reordering within a position.
  void onEntityUnordered(const ws::ast::Entity&,
                         cursiv::ValidationAcceptor& accept) const {
    accept.warning(cursiv::TextRange{30, 31}, "last");
    accept.warning(cursiv::TextRange{10, 11}, "first");
    accept.warning(cursiv::TextRange{10, 11}, "second");
    accept.warning(cursiv::TextRange{20, 21}, "third");
  }
  void onDataTypeCrash(const ws::ast::DataType&,
                       cursiv::ValidationAcceptor&) const {
    throw std::runtime_error("check crashed");
  }
};

// A language that modulates the walk: what it refuses on the entity, and what
// it refuses under it. Everything else keeps the default.
struct SelectiveValidator final : cursiv::DefaultDocumentValidator {
  using cursiv::DefaultDocumentValidator::DefaultDocumentValidator;

  bool skipEntity = false;
  bool skipUnderEntity = false;
  bool skipUnderPackage = false;
  mutable std::size_t optionsAsked = 0;
  cursiv::CancellationTokenSource* cancelOnOptions = nullptr;

 protected:
  [[nodiscard]] bool asksNodeOptions() const noexcept override { return true; }
  [[nodiscard]] cursiv::ValidateSingleNodeOptions validateSingleNodeOptions(
      const cursiv::AstNode& node,
      std::span<const cursiv::ValidationCategory* const>) const override {
    ++optionsAsked;
    if (cancelOnOptions != nullptr) cancelOnOptions->request_stop();
    if (skipUnderPackage &&
        cursiv::ast_cast<ws::ast::PackageDeclaration>(&node) != nullptr)
      return {.validateNode = true, .validateChildren = false};
    if (cursiv::ast_cast<ws::ast::Entity>(&node) == nullptr)
      return cursiv::kValidateEachNode;
    return {.validateNode = !skipEntity, .validateChildren = !skipUnderEntity};
  }
};

// The validator outlives the registry that holds it by address: declared
// first, destroyed last.
struct World {
  TestValidator validator;
  cursiv::SharedCoreServices shared;
  CaptureSink* sink = nullptr;
  cursiv::CoreServices* language = nullptr;
  cursiv::ValidationRegistry* registry = nullptr;

  World() {
    shared.fileSystemProvider =
        std::make_unique<cursiv::EmptyFileSystemProvider>();
    cursiv::install_default_shared_core_services(shared);
    auto capture = std::make_unique<CaptureSink>();
    sink = capture.get();
    shared.observabilitySink = std::move(capture);
    auto services = std::make_unique<cursiv::CoreServices>(shared);
    services->languageMetaData = {"workspace", {".ws"}, {}};
    services->parser = ws::generated::Workspace::create_parser();
    services->astReflection = ws::generated::Workspace::create_ast_reflection();
    cursiv::install_default_core_services(*services);
    language = services.get();
    registry = services->validationRegistry.get();
    shared.serviceRegistry->registerServices(std::move(services));
  }

  cursiv::Document* add(const std::string& uri, std::string text) const {
    auto document =
        shared.documentFactory->fromString(std::move(text), std::string(uri));
    cursiv::Document* const raw = document.get();
    shared.documents->addDocument(std::move(document));
    return raw;
  }

  cursiv::Document* build(const std::string& uri, std::string text,
                          const cursiv::BuildOptions& options = {}) const {
    cursiv::Document* const document = add(uri, std::move(text));
    cursiv::Document* const set[] = {document};
    shared.documentBuilder->build(set, options);
    return document;
  }

  std::vector<cursiv::Diagnostic> validate(
      const cursiv::Document& document,
      const cursiv::ValidationOptions& options = {}) const {
    std::vector<cursiv::Diagnostic> diagnostics;
    language->documentValidator->validateDocument(document, diagnostics,
                                                  options, {});
    return diagnostics;
  }
};

using Registry = cursiv::ValidationRegistry;

const cursiv::Diagnostic* find_code(
    const std::vector<cursiv::Diagnostic>& diagnostics, std::string_view code) {
  for (const cursiv::Diagnostic& d : diagnostics)
    if (d.code.has_value() && d.code->isString() && d.code->string() == code)
      return &d;
  return nullptr;
}

const cursiv::Diagnostic* find_message(
    const std::vector<cursiv::Diagnostic>& diagnostics,
    std::string_view message) {
  for (const cursiv::Diagnostic& d : diagnostics)
    if (d.message == message) return &d;
  return nullptr;
}

constexpr std::string_view kSource =
    "datatype String\nentity A { x : String }\n";

// A check filed over a base runs for the family; the exact filing over the
// same base runs for no node, because no node is the abstract base. The
// AstNode filing is every node.
void dispatch_follows_the_family_and_exact_stays_exact() {
  World world;
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onType>(world.validator));
  world.registry->registerCheck(
      Registry::makeExactValidationCheck<&TestValidator::onExactType>(
          world.validator));
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEveryNode>(
          world.validator));
  // Built without the validating phase, so the direct pass below is the only
  // one that ran and the counters count once.
  const cursiv::Document* const document = world.build(
      "file:///a.ws", std::string(kSource), cursiv::BuildOptions{false, {}});
  (void)world.validate(*document);
  CHECK(world.validator.typeFamily == 2);  // the DataType and the Entity
  CHECK(world.validator.exactType == 0);
  CHECK(world.validator.everyNode == 4);  // root, two types, one feature
}

void an_unknown_check_target_is_refused() {
  struct Foreign : cursiv::AstNode {};
  World world;
  const cursiv::ValidationCategory category{"foreign"};
  for (bool exact : {false, true}) {
    bool refused = false;
    try {
      world.registry->registerCheck(
          {&cursiv::ast_type_tag<Foreign>, exact,
           [](const cursiv::AstNode&, cursiv::ValidationAcceptor&,
              const cursiv::CancellationToken&) {}},
          &category);
    } catch (const cursiv::ValidationRegistrationError&) {
      refused = true;
    }
    CHECK(refused);
    CHECK(world.registry->checkCount() == 0);
    for (const auto* registered : world.registry->categories())
      CHECK(registered != &category);
  }
}

// The node hook is the second filter, after the categories: a node it refuses
// is checked nowhere, and a node it refuses to enter takes its whole subtree
// out -- the entity holds the one feature of this text.
void the_node_hook_takes_a_node_or_its_subtree_out() {
  const auto count = [](bool skipEntity, bool skipUnderEntity) {
    World world;
    auto validator = std::make_unique<SelectiveValidator>(*world.language);
    validator->skipEntity = skipEntity;
    validator->skipUnderEntity = skipUnderEntity;
    world.language->documentValidator = std::move(validator);
    world.registry->registerCheck(
        Registry::makeValidationCheck<&TestValidator::onEveryNode>(
            world.validator));
    const cursiv::Document* const document = world.build(
        "file:///a.ws", std::string(kSource), cursiv::BuildOptions{false, {}});
    (void)world.validate(*document);
    return world.validator.everyNode;
  };

  CHECK(count(false, false) == 4);  // root, two types, one feature
  CHECK(count(true, false) == 3);   // the entity itself is out
  CHECK(count(false, true) == 3);   // the feature under it is out
  CHECK(count(true, true) == 2);    // both
}

// The categories name who runs: `fast` alone skips the slow check and the
// built-in half, `built_in` alone runs only that half, empty runs everything.
void a_category_names_who_runs() {
  World world;
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEntity>(world.validator),
      &cursiv::fast);
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onFeature>(world.validator),
      &cursiv::slow);
  const cursiv::Document* const document =
      world.build("file:///a.ws", "datatype String\nentity A { x : Broken }\n");

  const cursiv::ValidationCategory* const fastOnly[] = {&cursiv::fast};
  std::vector<cursiv::Diagnostic> diagnostics =
      world.validate(*document, {.categories = fastOnly});
  CHECK(find_message(diagnostics, "entity seen") != nullptr);
  CHECK(find_message(diagnostics, "feature seen") == nullptr);
  CHECK(find_code(diagnostics, cursiv::kLinkingError) == nullptr);  // gated too

  const cursiv::ValidationCategory* const slowOnly[] = {&cursiv::slow};
  diagnostics = world.validate(*document, {.categories = slowOnly});
  CHECK(find_message(diagnostics, "entity seen") == nullptr);
  CHECK(find_message(diagnostics, "feature seen") != nullptr);

  const cursiv::ValidationCategory* const builtInOnly[] = {&cursiv::built_in};
  diagnostics = world.validate(*document, {.categories = builtInOnly});
  CHECK(find_message(diagnostics, "entity seen") == nullptr);
  CHECK(find_code(diagnostics, cursiv::kLinkingError) != nullptr);

  diagnostics = world.validate(*document);
  CHECK(find_message(diagnostics, "entity seen") != nullptr);
  CHECK(find_message(diagnostics, "feature seen") != nullptr);
  CHECK(find_code(diagnostics, cursiv::kLinkingError) != nullptr);

  // What was filed is what is listed, and never the reserved name.
  const std::span<const cursiv::ValidationCategory* const> listed =
      world.registry->categories();
  CHECK(std::ranges::find(listed, &cursiv::fast) != listed.end());
  CHECK(std::ranges::find(listed, &cursiv::slow) != listed.end());
  CHECK(std::ranges::find(listed, &cursiv::built_in) == listed.end());
}

void absent_categories_skip_the_node_walk() {
  World world;
  auto validator = std::make_unique<SelectiveValidator>(*world.language);
  const auto* measured = validator.get();
  world.language->documentValidator = std::move(validator);
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEveryNode>(world.validator),
      &cursiv::slow);
  const auto* document = world.build(
      "file:///a.ws", std::string(kSource), cursiv::BuildOptions{false, {}});
  const cursiv::ValidationCategory* const fastOnly[] = {&cursiv::fast};
  (void)world.validate(*document, {.categories = fastOnly});
  CHECK(measured->optionsAsked == 0);
  CHECK(world.validator.everyNode == 0);
  (void)world.validate(*document);
  CHECK(measured->optionsAsked == document->parseResult.nodes.size());
  CHECK(world.validator.everyNode == 4);
}

void node_options_are_interruptible() {
  World world;
  auto validator = std::make_unique<SelectiveValidator>(*world.language);
  const auto* measured = validator.get();
  cursiv::CancellationTokenSource cancellation;
  validator->cancelOnOptions = &cancellation;
  world.language->documentValidator = std::move(validator);
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEveryNode>(world.validator));
  const auto* document = world.build(
      "file:///a.ws", std::string(kSource), cursiv::BuildOptions{false, {}});
  std::vector<cursiv::Diagnostic> diagnostics;
  bool cancelled = false;
  try {
    world.language->documentValidator->validateDocument(
        *document, diagnostics, cancellation.get_token());
  } catch (const cursiv::OperationCancelled&) {
    cancelled = true;
  }
  CHECK(cancelled);
  CHECK(measured->optionsAsked == 1);
  CHECK(world.validator.everyNode == 0);
}

void nested_pruning_preserves_checks_on_siblings() {
  World world;
  auto validator = std::make_unique<SelectiveValidator>(*world.language);
  validator->skipUnderPackage = true;
  validator->skipEntity = true;
  world.language->documentValidator = std::move(validator);
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEveryNode>(world.validator));
  const auto* document = world.build(
      "file:///a.ws",
      "datatype String package P { package Q { entity A { x : String } } "
      "datatype Hidden } entity B { y : String } package R { datatype T }",
      cursiv::BuildOptions{false, {}});
  (void)world.validate(*document);
  CHECK(world.validator.everyNode == 5);  // root, String, P, y and R
}

// `built_in` names the runtime's own half; a language filing under it is
// refused loudly, for checks and hooks alike.
void built_in_is_refused_at_registration() {
  World world;
  bool refusedCheck = false;
  try {
    world.registry->registerCheck(
        Registry::makeValidationCheck<&TestValidator::onEntity>(
            world.validator),
        &cursiv::built_in);
  } catch (const cursiv::ValidationRegistrationError&) {
    refusedCheck = true;
  }
  CHECK(refusedCheck);

  bool refusedHook = false;
  try {
    world.registry->registerBeforeDocument(
        [](const cursiv::Document&, cursiv::ValidationAcceptor&,
           const cursiv::CancellationToken&) {},
        &cursiv::built_in);
  } catch (const cursiv::ValidationRegistrationError&) {
    refusedHook = true;
  }
  CHECK(refusedHook);
}

// The hooks bracket the walk, and a hook has a category like a check: the
// slow one stays silent in a fast pass.
void hooks_run_around_the_walk() {
  World world;
  world.registry->registerBeforeDocument(
      [&world](const cursiv::Document&, cursiv::ValidationAcceptor&,
               const cursiv::CancellationToken&) {
        world.validator.events.emplace_back("before");
      });
  world.registry->registerAfterDocument(
      [&world](const cursiv::Document&, cursiv::ValidationAcceptor&,
               const cursiv::CancellationToken&) {
        world.validator.events.emplace_back("after");
      });
  world.registry->registerBeforeDocument(
      [&world](const cursiv::Document&, cursiv::ValidationAcceptor&,
               const cursiv::CancellationToken&) {
        world.validator.events.emplace_back("slow-before");
      },
      &cursiv::slow);
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEntity>(world.validator));
  const cursiv::Document* const document =
      world.build("file:///a.ws", std::string(kSource));

  world.validator.events.clear();
  const cursiv::ValidationCategory* const fastOnly[] = {&cursiv::fast};
  (void)world.validate(*document, {.categories = fastOnly});
  const std::vector<std::string> expected = {"before", "entity", "after"};
  CHECK(world.validator.events == expected);
}

// Only the per-node walk is gated on an AST: the document hooks bracket the
// pass even when the parse left none.
void hooks_run_around_the_walk_with_no_ast() {
  World world;
  world.registry->registerBeforeDocument(
      [&world](const cursiv::Document&, cursiv::ValidationAcceptor&,
               const cursiv::CancellationToken&) {
        world.validator.events.emplace_back("before");
      });
  world.registry->registerAfterDocument(
      [&world](const cursiv::Document&, cursiv::ValidationAcceptor&,
               const cursiv::CancellationToken&) {
        world.validator.events.emplace_back("after");
      });
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEntity>(world.validator));
  cursiv::Document* const document =
      world.build("file:///a.ws", std::string(kSource));
  document->parseResult.value = nullptr;

  world.validator.events.clear();
  (void)world.validate(*document);
  const std::vector<std::string> expected = {"before", "after"};
  CHECK(world.validator.events == expected);
}

// A crashing check costs itself: its failure becomes a diagnostic on the node
// and an observation at the sink, and every other check still runs.
void a_crashing_check_costs_itself_not_the_pass() {
  World world;
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onDataTypeCrash>(
          world.validator));
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEntity>(world.validator));
  const cursiv::Document* const document =
      world.build("file:///a.ws", std::string(kSource));
  world.sink->seen.clear();

  const std::vector<cursiv::Diagnostic> diagnostics = world.validate(*document);
  const cursiv::Diagnostic* const crashed =
      find_code(diagnostics, cursiv::kValidationError);
  CHECK(crashed != nullptr);
  CHECK(crashed->message.find("check crashed") != std::string::npos);
  CHECK(find_message(diagnostics, "entity seen") != nullptr);  // pass went on

  bool observed = false;
  for (const cursiv::Observation& o : world.sink->seen)
    if (o.code == cursiv::ObservationCode::ValidationCheckThrew &&
        o.uri == document->uri)
      observed = true;
  CHECK(observed);
}

// Same law for a document hook, anchored on the whole document.
void a_crashing_hook_costs_itself_not_the_pass() {
  World world;
  world.registry->registerBeforeDocument([](const cursiv::Document&,
                                            cursiv::ValidationAcceptor&,
                                            const cursiv::CancellationToken&) {
    throw std::runtime_error("hook crashed");
  });
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEntity>(world.validator));
  const cursiv::Document* const document =
      world.build("file:///a.ws", std::string(kSource));
  world.sink->seen.clear();

  const std::vector<cursiv::Diagnostic> diagnostics = world.validate(*document);
  const cursiv::Diagnostic* const crashed =
      find_code(diagnostics, cursiv::kValidationError);
  CHECK(crashed != nullptr);
  CHECK(crashed->message.find("hook crashed") != std::string::npos);
  CHECK(find_message(diagnostics, "entity seen") != nullptr);

  bool observed = false;
  for (const cursiv::Observation& o : world.sink->seen)
    if (o.code == cursiv::ObservationCode::ValidationPreparationThrew &&
        o.uri == document->uri)
      observed = true;
  CHECK(observed);
}

// The two stops: after a repaired parse, after a broken link -- each keeps
// the built-in half's verdicts and skips the checks.
void the_two_stops_skip_the_checks() {
  World world;
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEntity>(world.validator));

  const cursiv::Document* const repaired =
      world.build("file:///a.ws", "datatype String\nentity A { x  String }\n");
  std::vector<cursiv::Diagnostic> diagnostics =
      world.validate(*repaired, {.stopAfterParsingErrors = true});
  CHECK(find_code(diagnostics, cursiv::kParseRepaired) != nullptr);
  CHECK(find_message(diagnostics, "entity seen") == nullptr);

  const cursiv::Document* const broken = world.build(
      "file:///b.ws", "datatype String\nentity A { x : Missing }\n");
  diagnostics = world.validate(*broken, {.stopAfterLinkingErrors = true});
  CHECK(find_code(diagnostics, cursiv::kLinkingError) != nullptr);
  CHECK(find_message(diagnostics, "entity seen") == nullptr);

  const cursiv::ValidationCategory* const fastOnly[] = {&cursiv::fast};
  for (const auto* document : {repaired, broken}) {
    diagnostics = world.validate(*document, {.categories = fastOnly,
                                            .stopAfterParsingErrors = true,
                                            .stopAfterLinkingErrors = true});
    CHECK(find_message(diagnostics, "entity seen") != nullptr);
    CHECK(find_code(diagnostics, cursiv::kLinkingError) == nullptr);
    CHECK(find_code(diagnostics, cursiv::kParseRepaired) == nullptr);
  }

  // The witness: without the stop, the same document reaches the checks.
  diagnostics = world.validate(*broken);
  CHECK(find_message(diagnostics, "entity seen") != nullptr);
}

// A broken reference names its text, the class that declares the member, and
// the member -- a code action reads which reference broke from `data`, never
// from the prose.
void a_linking_error_names_its_member() {
  World world;
  const cursiv::Document* const document = world.build(
      "file:///a.ws", "datatype String\nentity A { x : Missing }\n");
  const std::vector<cursiv::Diagnostic> diagnostics = world.validate(*document);
  const cursiv::Diagnostic* const error =
      find_code(diagnostics, cursiv::kLinkingError);
  CHECK(error != nullptr);
  CHECK(error->message.find("Missing") != std::string::npos);
  CHECK(error->data.has_value() && error->data->isObject());
  const cursiv::JsonValue::Object& data = error->data->object();
  const auto text = data.find(std::string(cursiv::kLinkingRefText));
  CHECK(text != data.end() && text->second.string() == "Missing");
  const auto owner = data.find(std::string(cursiv::kLinkingContainerType));
  CHECK(owner != data.end() && owner->second.string() == "Feature");
  const auto property = data.find(std::string(cursiv::kLinkingProperty));
  CHECK(property != data.end() && property->second.string() == "type");
}

// One diagnostic's fix rows, asked as a request of one.
cursiv::JsonValue::Array fixes_of(const cursiv::Document& document,
                                  const cursiv::Diagnostic& error) {
  const cursiv::Diagnostic* const one[] = {&error};
  return cursiv::spelling_actions(document, one).front();
}

// The names a row set would write, nearest first.
std::vector<std::string> names_in(const cursiv::JsonValue::Array& rows) {
  std::vector<std::string> names;
  for (const cursiv::JsonValue& row : rows) {
    const auto text = row.object().find(std::string(cursiv::kNewTextField));
    if (text != row.object().end()) names.push_back(text->second.string());
  }
  return names;
}

// What a broken reference's fix would replace it with, nearest first.
std::vector<std::string> spellings_of(const cursiv::Document& document,
                                      const cursiv::Diagnostic* error) {
  if (error == nullptr) return {};
  return names_in(fixes_of(document, *error));
}

// A misspelt reference is offered the names in scope it is near, nearest
// first, as the edit rows a parse repair writes; a case that differs outranks
// an edit; nothing is near a name unlike every other, and nothing is asked of
// a name too short to be unlike anything.
void a_misspelt_reference_offers_its_neighbours() {
  World world;
  const cursiv::Document* const near = world.build(
      "file:///a.ws",
      "datatype String\ndatatype Stringy\ndatatype Far\n"
      "entity A { x : Strng }\n");
  std::vector<cursiv::Diagnostic> diagnostics = world.validate(*near);
  const cursiv::Diagnostic* error =
      find_code(diagnostics, cursiv::kLinkingError);
  CHECK(error != nullptr);
  CHECK((spellings_of(*near, error) ==
         std::vector<std::string>{"String", "Stringy"}));
  if (error != nullptr) {
    const cursiv::JsonValue::Array rows = fixes_of(*near, *error);
    CHECK(!rows.empty());
    if (rows.empty()) return;
    const cursiv::JsonValue& row = rows[0];
    const auto field = [&](std::string_view name) {
      return row.object().find(std::string(name))->second;
    };
    CHECK(field(cursiv::kTitleField).string() == "Replace with 'String'");
    CHECK(field(cursiv::kEditKindField).string() == cursiv::kReplaceEdit);
    CHECK(field(cursiv::kBeginField).integer() ==
          static_cast<std::int64_t>(error->range.begin));
    CHECK(field(cursiv::kEndField).integer() ==
          static_cast<std::int64_t>(error->range.end));
    CHECK(field(cursiv::kRevisionField).integer() ==
          static_cast<std::int64_t>(near->buildRevision));
  }

  const cursiv::Document* const cased = world.build(
      "file:///b.ws",
      "datatype String\ndatatype Strin\nentity B { x : string }\n");
  diagnostics = world.validate(*cased);
  // `Stringy` stands in the other document: at one distance, this one first.
  CHECK((spellings_of(*cased, find_code(diagnostics, cursiv::kLinkingError)) ==
         std::vector<std::string>{"String", "Strin", "Stringy"}));

  const cursiv::Document* const unlike = world.build(
      "file:///c.ws", "datatype String\nentity C { x : Zzzzzzzz }\n");
  diagnostics = world.validate(*unlike);
  error = find_code(diagnostics, cursiv::kLinkingError);
  CHECK(error != nullptr && spellings_of(*unlike, error).empty());

  const cursiv::Document* const tiny = world.build(
      "file:///d.ws", "datatype Ab\nentity D { x : Ac }\n");
  diagnostics = world.validate(*tiny);
  error = find_code(diagnostics, cursiv::kLinkingError);
  CHECK(error != nullptr && spellings_of(*tiny, error).empty());
}

// The lookup is the quick fix's, never the validation's: a pass over a
// misspelt reference walks no scope for neighbours and stamps no row; the
// fix, asked for, walks it once.
void the_validation_looks_up_no_neighbour() {
  struct Counting final : cursiv::DefaultScopeProvider {
    using DefaultScopeProvider::DefaultScopeProvider;
    using DefaultScopeProvider::visitScopeEntries;
    mutable int walks = 0;
    bool visitScopeEntries(
        const cursiv::ReferenceInfo& context,
        cursiv::function_ref<bool(const cursiv::AstNodeDescription&)> visitor)
        const override {
      ++walks;
      return DefaultScopeProvider::visitScopeEntries(context, visitor);
    }
  };
  World world;
  auto counting = std::make_unique<Counting>(*world.language);
  const Counting* const scope = counting.get();
  world.language->scopeProvider = std::move(counting);
  const cursiv::Document* const document = world.build(
      "file:///a.ws", "datatype String\nentity A { x : Strng }\n");
  scope->walks = 0;
  const std::vector<cursiv::Diagnostic> diagnostics = world.validate(*document);
  const cursiv::Diagnostic* const error =
      find_code(diagnostics, cursiv::kLinkingError);
  CHECK(error != nullptr);
  if (error == nullptr) return;
  CHECK(scope->walks == 0);
  CHECK(error->data.has_value() &&
        !error->data->object().contains(
            std::string(cursiv::kDefaultCodeActionsKey)));
  CHECK(fixes_of(*document, *error).size() == 1);
  CHECK(scope->walks == 1);
}

// The lookup stops after the first sites: a document every reference of
// which is broken is not misspelt.
void a_document_broken_everywhere_stops_being_spelt() {
  World world;
  std::string text = "datatype String\nentity E {";
  for (int i = 0; i < 70; ++i) text += " f" + std::to_string(i) + " : Strng";
  text += " }\n";
  const cursiv::Document* const document =
      world.build("file:///e.ws", std::move(text));
  const std::vector<cursiv::Diagnostic> diagnostics = world.validate(*document);
  std::vector<const cursiv::Diagnostic*> broken;
  for (const cursiv::Diagnostic& d : diagnostics)
    if (d.data.has_value() && d.data->isObject() &&
        d.data->object().contains(std::string(cursiv::kLinkingRefText)))
      broken.push_back(&d);
  unsigned spelt = 0;
  for (const cursiv::JsonValue::Array& rows :
       cursiv::spelling_actions(*document, broken))
    if (!names_in(rows).empty()) ++spelt;
  CHECK(broken.size() == 70);
  CHECK(spelt == 64);
}

// The note of a work bound is no repair: beside a clean tree the checks still
// run under the stop, and beside a refusal the refusal still speaks.
void a_bound_alone_is_no_parse_fault() {
  World world;
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEntity>(world.validator));
  const std::string text = "datatype String\nentity A extends String { }\n";
  cursiv::Document* const document = world.build("file:///a.ws", text);
  CHECK(document->parseResult.diagnostics.empty());
  cursiv::ParseDiagnostic bound;
  bound.kind = cursiv::DiagnosticKind::bounded;
  document->parseResult.diagnostics.push_back(bound);
  CHECK(find_message(
            world.validate(*document, {.stopAfterParsingErrors = true}),
            "entity seen") != nullptr);
  document->parseResult.accepted = false;
  CHECK(find_code(world.validate(*document), cursiv::kSyntaxError) != nullptr);
}

// A refusal that says nothing must not read as a clean document: this
// grammar's recovery always leaves a record, so the silent refusal the guard
// exists for is staged by erasing what it said.
void an_unrepaired_parse_is_one_syntax_error() {
  World world;
  cursiv::Document* const document = world.add("file:///a.ws", "!!");
  document->parseResult = world.language->parser->parse("!!");
  // The recovery reads everything, so the silent refusal is staged: the
  // guard exists for a result no engine returns.
  CHECK(document->parseResult.accepted);
  document->parseResult.accepted = false;
  document->parseResult.diagnostics.clear();
  const std::vector<cursiv::Diagnostic> diagnostics = world.validate(*document);
  CHECK(find_code(diagnostics, cursiv::kSyntaxError) != nullptr);
}

// The other branch of the same guard: no entry root at all names the whole
// document, never a tail past it.
void a_never_entered_parse_names_the_whole_document() {
  World world;
  cursiv::Document* const document = world.add("file:///a.ws", "");
  document->parseResult = cursiv::ParseResult{};
  const std::vector<cursiv::Diagnostic> diagnostics = world.validate(*document);
  const cursiv::Diagnostic* const error =
      find_code(diagnostics, cursiv::kSyntaxError);
  CHECK(error != nullptr);
  CHECK(error->message == "Syntax error: the document could not be parsed.");

  int before = 0, after = 0;
  world.registry->registerBeforeDocument(
      [&](const cursiv::Document&, cursiv::ValidationAcceptor&,
          const cursiv::CancellationToken&) { ++before; });
  world.registry->registerAfterDocument(
      [&](const cursiv::Document&, cursiv::ValidationAcceptor&,
          const cursiv::CancellationToken&) { ++after; });
  CHECK(find_code(world.validate(*document, {.stopAfterParsingErrors = true}),
                  cursiv::kSyntaxError) != nullptr);
  CHECK(before == 0 && after == 0);
  (void)world.validate(*document);
  CHECK(before == 1 && after == 1);
}

// The anchors land where the grammar wrote: `.property` on the record that
// wrote the member, `.keyword` on the spelling.
void anchors_land_where_the_grammar_wrote() {
  World world;
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEntity>(world.validator));
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEntityKeyword>(
          world.validator));
  const cursiv::Document* const document =
      world.build("file:///a.ws", std::string(kSource));
  const std::vector<cursiv::Diagnostic> diagnostics = world.validate(*document);

  const cursiv::Diagnostic* const onName =
      find_message(diagnostics, "entity seen");
  CHECK(onName != nullptr);
  const cursiv::TextOffset name =
      static_cast<cursiv::TextOffset>(kSource.find("A {"));
  CHECK(onName->range.begin == name && onName->range.end == name + 1);

  const cursiv::Diagnostic* const onKeyword =
      find_message(diagnostics, "keyword seen");
  CHECK(onKeyword != nullptr);
  const cursiv::TextOffset keyword =
      static_cast<cursiv::TextOffset>(kSource.find("entity"));
  CHECK(onKeyword->range.begin == keyword &&
        onKeyword->range.end == keyword + 6);
}

// End to end: the build publishes a repair with its edit rows, the code-action
// provider turns them into a quick fix, and applying the edit really fixes
// the document.
void a_repairs_quick_fix_really_fixes() {
  World world;
  const std::string text = "datatype String\nentity A { x  String }\n";
  const cursiv::Document* const document = world.build("file:///a.ws", text);
  const cursiv::Diagnostic* const repaired =
      find_code(document->diagnostics, cursiv::kParseRepaired);
  CHECK(repaired != nullptr);
  CHECK(repaired->data.has_value() && repaired->data->isObject());
  const cursiv::JsonValue* const rows = [&]() -> const cursiv::JsonValue* {
    const auto it = repaired->data->object().find(
        std::string(cursiv::kDefaultCodeActionsKey));
    return it == repaired->data->object().end() ? nullptr : &it->second;
  }();
  CHECK(rows != nullptr && rows->isArray() && !rows->array().empty());

  // The row applied to the text is a document that parses clean.
  const cursiv::JsonValue::Object& row = rows->array().front().object();
  const auto begin =
      row.find(std::string(cursiv::kBeginField))->second.integer();
  const auto end = row.find(std::string(cursiv::kEndField))->second.integer();
  const auto newText = row.find(std::string(cursiv::kNewTextField))->second;
  std::string fixed = text;
  fixed.replace(static_cast<std::size_t>(begin),
                static_cast<std::size_t>(end - begin), newText.string());
  const cursiv::ParseResult reparsed = world.language->parser->parse(fixed);
  CHECK(static_cast<bool>(reparsed) && reparsed.diagnostics.empty());

  // And the provider offers it as a quick fix over the published block.
  const cursiv::DefaultCodeActionProvider provider;
  lsp::CodeActionParams params;
  params.textDocument.uri = lsp::Uri::parse(std::string(document->uri));
  params.range = {{0, 0}, {2, 0}};
  const std::vector<lsp::CodeAction> actions =
      provider.getCodeActions(*document, params, {});
  CHECK(!actions.empty());
  const lsp::CodeAction& action = actions.front();
  CHECK(action.kind == lsp::CodeActionKind::QuickFix);
  CHECK(action.edit.has_value() && action.edit->changes.has_value());
}

// Document order on the way out, and stable within one position. The pass
// sorts a permutation rather than the diagnostics themselves, so the order it
// owes is pinned here and not left to the sort it happens to call.
void the_answer_comes_back_in_document_order() {
  World world;
  world.registry->registerCheck(
      Registry::makeValidationCheck<&TestValidator::onEntityUnordered>(
          world.validator));
  const cursiv::Document* const document =
      world.build("file:///a.ws", "datatype String\nentity A { x : String }\n");
  const std::vector<cursiv::Diagnostic> diagnostics = world.validate(*document);

  std::vector<std::string> order;
  for (const cursiv::Diagnostic& diagnostic : diagnostics)
    order.push_back(diagnostic.message);
  const std::vector<std::string> owed{"first", "second", "third", "last"};
  CHECK(order == owed);
}

}  // namespace

void run() {
  absent_categories_skip_the_node_walk();
  node_options_are_interruptible();
  nested_pruning_preserves_checks_on_siblings();
  dispatch_follows_the_family_and_exact_stays_exact();
  an_unknown_check_target_is_refused();
  the_node_hook_takes_a_node_or_its_subtree_out();
  a_category_names_who_runs();
  built_in_is_refused_at_registration();
  hooks_run_around_the_walk();
  hooks_run_around_the_walk_with_no_ast();
  a_crashing_check_costs_itself_not_the_pass();
  a_crashing_hook_costs_itself_not_the_pass();
  the_two_stops_skip_the_checks();
  a_linking_error_names_its_member();
  a_misspelt_reference_offers_its_neighbours();
  the_validation_looks_up_no_neighbour();
  a_document_broken_everywhere_stops_being_spelt();
  an_unrepaired_parse_is_one_syntax_error();
  a_bound_alone_is_no_parse_fault();
  a_never_entered_parse_names_the_whole_document();
  anchors_land_where_the_grammar_wrote();
  a_repairs_quick_fix_really_fixes();
  the_answer_comes_back_in_document_order();
}

}  // namespace document_validator

namespace validation_acceptor {

namespace {

struct CountingGrammar final : cursiv::Grammar {
  mutable std::size_t writes = 0;
  mutable std::size_t rules = 0;

  bool recordWrites(const cursiv::CstNode& node,
                    std::string_view feature) const noexcept override {
    ++writes;
    return ((feature == "items" || feature == "self") && node.engine < 3) ||
           (feature == "x" && node.engine == 2);
  }
  bool recordFills(const cursiv::CstNode& node,
                   std::string_view feature) const noexcept override {
    return feature == "self" && node.engine == 3;
  }
  std::string_view recordRule(const cursiv::CstNode&) const noexcept override {
    ++rules;
    return {};
  }
  std::string_view recordFeature(const cursiv::CstNode&) const
      noexcept override {
    return {};
  }
  std::string_view terminalText(std::uint16_t) const noexcept override {
    return {};
  }
  std::string_view terminalName(std::uint16_t) const noexcept override {
    return {};
  }
  std::string_view terminalDoc(std::uint16_t) const noexcept override {
    return {};
  }
  std::string_view spellingDoc(std::string_view) const noexcept override {
    return {};
  }
};

std::string repeated_pairs(std::size_t pairs) {
  std::string text(pairs * 2, 'x');
  for (std::size_t i = 1; i < text.size(); i += 2) text[i] = 'y';
  return text;
}

struct Fixture {
  CountingGrammar grammar;
  cursiv::AstNode first;
  cursiv::AstNode second;
  cursiv::AstNode root;
  cursiv::Document document;

  explicit Fixture(std::uint32_t pairs)
      : document(cursiv::TextDocument::create(
            "file:///anchors", "anchors", 0, repeated_pairs(pairs * 2))) {
    cursiv::Cst cst(document.textDocument().getText());
    cursiv::CstBuilder builder(cst);
    const auto outer = builder.enter();
    const auto fill = [&](cursiv::AstNode& node, std::uint32_t begin) {
      const auto open = builder.enter();
      for (std::uint32_t i = 0; i < pairs * 2; ++i)
        builder.leaf(begin + i, begin + i + 1, i % 2 + 1);
      node.cstNodeId = builder.exit(open, begin, 3);
      node.parent = &root;
    };
    fill(first, 0);
    fill(second, pairs * 2);
    root.cstNodeId = builder.exit(outer, 0, 3);
    root.parent = &document;
    cursiv::ParseResult parsed(std::move(cst));
    parsed.value = &root;
    parsed.nodes = {&first, &second, &root};
    parsed.bind_grammar(grammar);
    document.parseResult = std::move(parsed);
  }
};

void check_span(std::optional<cursiv::TextRange> span, std::uint32_t begin,
                std::uint32_t end) {
  CHECK(span.has_value());
  if (span.has_value()) {
    CHECK(span->begin == begin);
    CHECK(span->end == end);
  }
}

void indexed_anchors_reuse_their_collections() {
  const auto discard = [](cursiv::Diagnostic&&) {};
  for (const std::uint32_t pairs : {128u, 1024u}) {
    Fixture fixture(pairs);
    cursiv::ValidationAcceptor accept(discard, fixture.document, "anchors");
    for (std::uint32_t i = 0; i < pairs; ++i) {
      check_span(accept.featureSpan(fixture.first, "items", i), i, i + 1);
      check_span(accept.keywordSpan(fixture.first, "x", i), i * 2, i * 2 + 1);
    }
    CHECK(fixture.grammar.writes <= pairs * 4);
    CHECK(fixture.grammar.rules <= pairs * 2);
    check_span(accept.featureSpan(fixture.first, "items", pairs * 4),
               pairs * 2 - 1, pairs * 2);
    check_span(accept.keywordSpan(fixture.first, "x", pairs * 4),
               pairs * 2 - 2, pairs * 2 - 1);
  }
}

void cached_anchors_keep_their_name_and_kind() {
  Fixture fixture(8);
  const auto discard = [](cursiv::Diagnostic&&) {};
  cursiv::ValidationAcceptor accept(discard, fixture.document, "anchors");
  std::string name = "items";
  check_span(accept.featureSpan(fixture.first, name, 1), 1, 2);
  const auto calls = fixture.grammar.writes;
  name.assign("other");
  check_span(accept.featureSpan(fixture.first, "items", 2), 2, 3);
  CHECK(fixture.grammar.writes == calls);
  check_span(accept.featureSpan(fixture.first, "x", 1), 3, 4);
  check_span(accept.keywordSpan(fixture.first, "x", 1), 2, 3);
  check_span(accept.featureSpan(fixture.first, "x", 0), 1, 2);
  check_span(accept.keywordSpan(fixture.first, "x", 0), 0, 1);
  check_span(accept.featureSpan(fixture.first, "self", 1), 0, 1);
  check_span(accept.featureSpan(fixture.first, "self", 0), 0, 16);
}

void switching_nodes_and_missing_anchors_preserve_ranges() {
  Fixture fixture(8);
  const auto discard = [](cursiv::Diagnostic&&) {};
  cursiv::ValidationAcceptor accept(discard, fixture.document, "anchors");
  check_span(accept.featureSpan(fixture.first, "items", 1), 1, 2);
  check_span(accept.featureSpan(fixture.second, "items", 1), 17, 18);
  check_span(accept.featureSpan(fixture.first, "items", 1), 1, 2);
  check_span(accept.featureSpan(fixture.first, "missing", 1), 0, 16);
  const auto calls = fixture.grammar.writes;
  check_span(accept.featureSpan(fixture.first, "missing", 5), 0, 16);
  CHECK(fixture.grammar.writes == calls);
  check_span(accept.keywordSpan(fixture.first, "missing", 5), 0, 16);
  cursiv::AstNode detached;
  CHECK(!accept.featureSpan(detached, "items", 1).has_value());
  CHECK(!accept.keywordSpan(detached, "x", 1).has_value());
}

void related_nodes_use_their_own_documents_cst() {
  Fixture source(2);
  Fixture target(8);
  std::vector<cursiv::Diagnostic> diagnostics;
  const auto collect = [&](cursiv::Diagnostic&& diagnostic) {
    diagnostics.push_back(std::move(diagnostic));
  };
  cursiv::ValidationAcceptor accept(collect, source.document, "anchors");
  check_span(accept.spanOf(target.second), 16, 32);
  accept.error(source.first, "duplicate")
      .relatedInformation(target.second, "other declaration", "file:///other");
  CHECK(diagnostics.size() == 1);
  CHECK(diagnostics.front().relatedInformation.size() == 1);
  const auto& related = diagnostics.front().relatedInformation.front();
  CHECK(related.uri == "file:///other");
  CHECK(related.range.begin == 16 && related.range.end == 32);
  check_span(accept.featureSpan(target.second, "items", 1), 17, 18);
  check_span(accept.keywordSpan(target.second, "x", 1), 18, 19);
  accept.error(target.second, "foreign name").keyword("x");
  CHECK(diagnostics.back().range.begin == 16 &&
        diagnostics.back().range.end == 17);
}

void anchor_caches_distinguish_documents_with_the_same_node_ids() {
  Fixture source(8);
  Fixture target(8);
  const auto discard = [](cursiv::Diagnostic&&) {};
  cursiv::ValidationAcceptor accept(discard, source.document, "anchors");
  CHECK(source.first.cstNodeId == target.first.cstNodeId);
  check_span(accept.featureSpan(source.first, "items", 1), 1, 2);
  CHECK(target.grammar.writes == 0);
  check_span(accept.featureSpan(target.first, "items", 1), 1, 2);
  CHECK(target.grammar.writes > 0);
}

}  // namespace

void run() {
  indexed_anchors_reuse_their_collections();
  cached_anchors_keep_their_name_and_kind();
  switching_nodes_and_missing_anchors_preserve_ranges();
  related_nodes_use_their_own_documents_cst();
  anchor_caches_distinguish_documents_with_the_same_node_ids();
}

}  // namespace validation_acceptor

// Pins two `deleted`-repair message defects: a stray byte that quotes to
// nothing must still name the expectation, and a deleted token that is the
// expectation is said once, not twice.
namespace repair_message {

namespace {

// `kind` selects one repair; `std::nullopt` takes whichever the engine made,
// which is what a site spelled differently by each of them is pinned on.
std::string message_for(const cursiv::Parser& engine, std::string_view text,
                        std::optional<cursiv::DiagnosticKind> kind) {
  const cursiv::ParseResult result = engine.parse(text);
  for (const cursiv::ParseDiagnostic& d : result.cst().diagnostics()) {
    if (kind && d.kind != *kind) continue;
    const bool sane = d.begin <= d.end && d.end <= text.size();
    const std::string_view over =
        sane ? text.substr(d.begin, d.end - d.begin) : std::string_view{};
    return cursiv::repair_message(engine.getGrammar(), d, over,
                                  d.end >= text.size());
  }
  return {};
}

void a_tight_region_deletion_still_names_the_expectation() {
  const std::unique_ptr<cursiv::Parser> engine =
      tmsg::generated::MessageG::create_parser();
  const std::string message =
      message_for(*engine, "[a ]", cursiv::DiagnosticKind::deleted);
  CHECK(!message.empty());
  CHECK(message != "Syntax error");
  CHECK(message == "Expecting ']'.");
}

// One refusal, where the position owed an operand: the pin is that the
// sentence names the fault ONCE, never echoing the token back as the thing it
// expects.
void an_unwound_token_matching_the_expectation_is_not_echoed_twice() {
  const std::unique_ptr<cursiv::Parser> engine =
      tmsg::generated::MessageG::create_parser();
  const std::string message = message_for(*engine, "1 + ;", std::nullopt);
  CHECK(!message.empty());
  CHECK(message != "Syntax error");
  CHECK(message.find("expecting '+'") == std::string::npos);
#if !defined(CURSIV_ENGINE_LL)
  CHECK(message == "Expecting NUM.");
#endif
}

// Past 24 bytes the quote is cut back to a lead byte, never through a
// multi-byte character's continuation bytes.
void a_long_deletion_does_not_split_a_multibyte_character() {
  const std::unique_ptr<cursiv::Parser> engine =
      tmsg::generated::MessageG::create_parser();
  const std::string over = std::string(23, 'a') +
                           "\xC3\xA9"
                           "bbb";
  cursiv::ParseDiagnostic d;
  d.kind = cursiv::DiagnosticKind::deleted;
  const std::string message =
      cursiv::repair_message(engine->getGrammar(), d, over, /*atEnd=*/false);
  CHECK(message == "Unexpected '" + std::string(23, 'a') + "...'.");
}

void quotes_trim_before_shortening_and_flatten_line_breaks() {
  const auto engine = tmsg::generated::MessageG::create_parser();
  cursiv::ParseDiagnostic d;
  d.kind = cursiv::DiagnosticKind::deleted;
  CHECK(cursiv::repair_message(engine->getGrammar(), d,
                               " \t\r\na\nb\tc\rd  \n", false) ==
        "Unexpected 'a b c d'.");
  CHECK(cursiv::repair_message(engine->getGrammar(), d,
                               "short" + std::string(100000, ' '), false) ==
        "Unexpected 'short'.");
  CHECK(cursiv::repair_message(engine->getGrammar(), d,
                               std::string(100000, ' '), false) ==
        "Syntax error");
}

}  // namespace

void run() {
  a_tight_region_deletion_still_names_the_expectation();
  an_unwound_token_matching_the_expectation_is_not_echoed_twice();
  a_long_deletion_does_not_split_a_multibyte_character();
  quotes_trim_before_shortening_and_flatten_line_breaks();
}

}  // namespace repair_message

int main() {
  document_validator::run();
  validation_acceptor::run();
  repair_message::run();
  return cursiv::test::report("validation");
}
