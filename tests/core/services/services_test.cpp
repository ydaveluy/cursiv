// The service containers: what a language's container owes and when it is
// complete, the registry that routes a document to its language, the slots,
// the two values a diagnostic and a JSON answer are made of, and the side
// channel observations are said on.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/core/observability/ObservabilitySink.hpp"
#include "cursiv/core/observability/ObservationFormat.hpp"
#include "cursiv/core/observability/StderrObservabilitySink.hpp"
#include "cursiv/core/references/DefaultNameProvider.hpp"
#include "cursiv/core/references/DefaultScopeComputation.hpp"
#include "cursiv/core/services/CoreLanguage.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/CoreServicesFor.hpp"
#include "cursiv/core/services/DefaultServiceRegistry.hpp"
#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/ServiceSlot.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/DefaultAstNodeLocator.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "cursiv/core/workspace/MemoryFileSystemProvider.hpp"
#include "cursiv/core/workspace/WorkspaceManager.hpp"
#include "cursiv/test/Check.hpp"
#include "support/WorkspaceWorld.hpp"
#include "workspace/lsp/LspModule.hpp"

#ifndef __wasi__
#include <unistd.h>
#endif

// Pins the container every generated language is built through: the
// defaults, the module, and the registration that either owns them or fails.
namespace core_language {

namespace {

void install_workspace(cursiv::CoreServices& services) {
  services.languageMetaData = {"workspace", {".ws"}, {}};
  services.parser = ws::generated::Workspace::create_parser();
  services.astReflection = ws::generated::Workspace::create_ast_reflection();
}

void create_language_builds_a_workspace_of_its_own() {
  const cursiv::CoreLanguage<cursiv::CoreServices, cursiv::SharedCoreServices>
      built = cursiv::create_language<cursiv::CoreServices,
                                      cursiv::SharedCoreServices,
                                      install_workspace>();
  CHECK(built.language.languageMetaData.languageId == "workspace");
  CHECK(&built.language ==
        &built.shared->serviceRegistry->getServices("file:///a.ws", {}));
}

void install_nothing(cursiv::CoreServices&) {}

// `register_language`'s `bool` promises `false` on failure; what a caller
// gets is an uncaught exception instead.
void register_language_throws_instead_of_answering_false() {
  cursiv::SharedCoreServices shared;
  cursiv::install_default_shared_core_services(shared);

  bool threw = false;
  try {
    (void)cursiv::register_language<
        cursiv::CoreServices, cursiv::SharedCoreServices, install_nothing>(
        shared);
  } catch (const cursiv::ServiceRegistrationError&) {
    // Pinned as is: see G6-06
    threw = true;
  }
  CHECK(threw);
}

}  // namespace

void run() {
  create_language_builds_a_workspace_of_its_own();
  register_language_throws_instead_of_answering_false();
}

}  // namespace core_language

// Pins what one language's container owes and when it is complete. The answer
// reads the container's own rows, so a service added in one place cannot be
// forgotten in another.
namespace core_services {

namespace {

cursiv::LanguageMetaData meta() { return {"workspace", {".ws"}, {}}; }

cursiv::SharedCoreServices shared;

void a_fresh_container_owes_its_metadata_first() {
  const cursiv::CoreServices s(shared);
  CHECK(s.firstMissingService() == "languageMetaData");
  CHECK(s.parser == nullptr);
  CHECK(s.languageMetaData.languageId.empty());
}

void what_is_written_stops_being_owed() {
  cursiv::CoreServices s(shared);
  s.languageMetaData = meta();
  CHECK(s.languageMetaData.languageId == "workspace");
  // The metadata is in, so the next hole is the one after it.
  CHECK(s.firstMissingService() == "parser");
  s.parser = ws::generated::Workspace::create_parser();
  CHECK(s.firstMissingService() == "astReflection");
  s.astReflection = ws::generated::Workspace::create_ast_reflection();
  CHECK(s.firstMissingService() == "nameProvider");
  s.nameProvider = std::make_unique<cursiv::DefaultNameProvider>(s);
  CHECK(s.firstMissingService() == "astNodeLocator");
  s.astNodeLocator = std::make_unique<cursiv::DefaultAstNodeLocator>(s);
  CHECK(s.firstMissingService() == "astNodeDescriptionProvider");

  // A module that pre-installed a service keeps it: the defaults fill holes,
  // they do not replace.
  s.scopeComputation = std::make_unique<cursiv::DefaultScopeComputation>(s);
  const cursiv::ScopeComputation* const mine = s.scopeComputation.get();

  // What no grammar decides is filled in one call.
  cursiv::install_default_core_services(s);
  CHECK(s.firstMissingService().empty());
  CHECK(s.astNodeDescriptionProvider != nullptr);
  CHECK(s.scopeComputation.get() == mine);
}

// The container's one door to the engine answers like the engine.
void the_container_s_parser_is_the_engine() {
  cursiv::CoreServices s(shared);
  s.languageMetaData = meta();
  s.parser = ws::generated::Workspace::create_parser();

  const cursiv::ParseResult r = s.parser->parse("datatype String\n");
  CHECK(static_cast<bool>(r));
  CHECK(&s.shared == &shared);
}

// A derived container that adds a slot declares it in `visitServices`, and the
// declaration is what makes its absence reportable.
struct Derived final : cursiv::CoreServicesFor<cursiv::SharedCoreServices> {
  std::unique_ptr<const cursiv::NameProvider> extra;

  explicit Derived(cursiv::SharedCoreServices& sharedServices)
      : cursiv::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}

  void visitServices(cursiv::function_ref<void(const cursiv::ServiceSlot&)>
                         yield) const override {
    cursiv::CoreServices::visitServices(yield);
    yield({"extra", true, extra != nullptr});
  }
};

void an_added_slot_is_declared_and_owed() {
  Derived s(shared);
  s.languageMetaData = meta();
  s.parser = ws::generated::Workspace::create_parser();
  s.astReflection = ws::generated::Workspace::create_ast_reflection();
  cursiv::install_default_core_services(s);
  // Every framework row is filled; the language's own row is the hole left.
  CHECK(s.firstMissingService() == "extra");
  s.extra = std::make_unique<cursiv::DefaultNameProvider>(s);
  CHECK(s.firstMissingService().empty());
}

// `make_default_core_services` builds, names and fills in one call.
void make_default_builds_a_complete_container() {
  auto s = cursiv::make_default_core_services<Derived>(shared, "made");
  CHECK(s->languageMetaData.languageId == "made");
  // The three the installer leaves to the language, plus the derived row.
  CHECK(s->parser == nullptr);
  CHECK(s->astReflection == nullptr);
  CHECK(s->astNodeDescriptionProvider != nullptr);
}

// The diamond: a container that is two facades of one language still holds
// exactly one `CoreServices`.
struct FacadeA : virtual cursiv::CoreServices {
  explicit FacadeA(cursiv::SharedCoreServices& sharedServices)
      : cursiv::CoreServices(sharedServices) {}
};
struct FacadeB : virtual cursiv::CoreServices {
  explicit FacadeB(cursiv::SharedCoreServices& sharedServices)
      : cursiv::CoreServices(sharedServices) {}
};
struct Both final : FacadeA, FacadeB {
  explicit Both(cursiv::SharedCoreServices& sharedServices)
      : cursiv::CoreServices(sharedServices),
        FacadeA(sharedServices),
        FacadeB(sharedServices) {}
};

void the_diamond_holds_one_container() {
  Both s(shared);
  s.languageMetaData = meta();
  cursiv::CoreServices& a = static_cast<FacadeA&>(s);
  cursiv::CoreServices& b = static_cast<FacadeB&>(s);
  CHECK(&a == &b);
  a.parser = ws::generated::Workspace::create_parser();
  CHECK(b.parser != nullptr);
}

// The first hole reported is the first slot declared: the order is a surface.
void the_shared_container_reports_in_declaration_order() {
  cursiv::SharedCoreServices bare;
  CHECK(bare.firstMissingService() == "observabilitySink");
}

// Fill-if-empty holds on the shared side too.
void a_preinstalled_shared_service_is_kept() {
  cursiv::SharedCoreServices s;
  s.fileSystemProvider = std::make_unique<cursiv::EmptyFileSystemProvider>();
  const cursiv::FileSystemProvider* const mine = s.fileSystemProvider.get();
  cursiv::install_default_shared_core_services(s);
  CHECK(s.firstMissingService().empty());
  CHECK(s.fileSystemProvider.get() == mine);
}

}  // namespace

void run() {
  a_fresh_container_owes_its_metadata_first();
  what_is_written_stops_being_owed();
  the_container_s_parser_is_the_engine();
  an_added_slot_is_declared_and_owed();
  make_default_builds_a_complete_container();
  the_diamond_holds_one_container();
  the_shared_container_reports_in_declaration_order();
  a_preinstalled_shared_service_is_kept();
}

}  // namespace core_services

// Pins the integer half of `DiagnosticCode`, LSP's `integer | string`: the
// alternative nothing in the tree constructs yet.
namespace diagnostic {

namespace {

void an_integer_code_holds_only_that_alternative() {
  const cursiv::DiagnosticCode code(std::int64_t{42});
  CHECK(code.isInteger());
  CHECK(!code.isString());
  CHECK(code.integer() == 42);
}

void an_integer_code_compares_both_ways() {
  const cursiv::DiagnosticCode code(std::int64_t{7});
  CHECK(code == std::int64_t{7});
  CHECK(!(code == std::int64_t{8}));
  CHECK(!(code == "7"));  // a string code names a different alternative

  const cursiv::DiagnosticCode same(std::int64_t{7});
  const cursiv::DiagnosticCode other("7");
  CHECK(code == same);
  CHECK(!(code == other));
}

}  // namespace

void run() {
  an_integer_code_holds_only_that_alternative();
  an_integer_code_compares_both_ways();
}

}  // namespace diagnostic

// Pins the determinism `toJsonString` promises: sorted keys, escaped
// control characters, the classic locale, and the null fallback.
namespace json_value {

namespace {

void object_keys_serialize_sorted() {
  cursiv::JsonValue::Object obj;
  obj["b"] = 1;
  obj["a"] = 2;
  const cursiv::JsonValue v(std::move(obj));
  CHECK(v.toJsonString({.pretty = false}) == R"({"a":2,"b":1})");
}

void pretty_and_compact_differ_only_in_layout() {
  cursiv::JsonValue::Array arr;
  arr.emplace_back(1);
  arr.emplace_back(2);
  const cursiv::JsonValue v(std::move(arr));
  CHECK(v.toJsonString({.pretty = false}) == "[1,2]");
  CHECK(v.toJsonString({.pretty = true, .indentSize = 2}) == "[\n  1,\n  2\n]");
}

void strings_escape_quotes_backslashes_and_control_characters() {
  const std::string input =
      std::string("a\"\\") + '\b' + '\f' + '\n' + '\r' + '\t' + '\x01' + "z";
  const cursiv::JsonValue v(input);

  std::string expected = "\"a";
  expected += "\\\"";
  expected += "\\\\";
  expected += "\\b";
  expected += "\\f";
  expected += "\\n";
  expected += "\\r";
  expected += "\\t";
  expected += "\\u0001";
  expected += "z\"";
  CHECK(v.toJsonString({.pretty = false}) == expected);
}

void doubles_serialize_with_the_classic_locale() {
  const cursiv::JsonValue v(3.5);
  CHECK(v.toJsonString({.pretty = false}) == "3.5");
}

// A non-finite double has no JSON spelling: it lands as `null`.
void non_finite_numbers_fall_back_to_null() {
  const cursiv::JsonValue nan(std::numeric_limits<double>::quiet_NaN());
  const cursiv::JsonValue inf(std::numeric_limits<double>::infinity());
  CHECK(nan.isNull());
  CHECK(inf.isNull());
  CHECK(nan.toJsonString({.pretty = false}) == "null");
}

void assignment_can_extract_an_owned_child() {
  using Value = cursiv::JsonValue;
  Value integer(Value::Object{{"child", 7}});
  integer = integer.object().at("child");
  CHECK(integer.isInteger() && integer.integer() == 7);

  Value object(Value::Object{{"child", Value::Object{{"leaf", 9}}}});
  object = object.object().at("child");
  CHECK(object.toJsonString({.pretty = false}) == R"({"leaf":9})");

  const std::string payload(128, 'x');
  Value text(Value::Object{{"child", payload}});
  text = std::move(text.object().at("child"));
  CHECK(text.isString() && text.string() == payload);

  Value array(Value::Array{Value::Array{1, 2}});
  array = std::move(array.array().front());
  CHECK(array.toJsonString({.pretty = false}) == "[1,2]");

  Value copy = array;
  array = copy;
  CHECK(array.toJsonString({.pretty = false}) == "[1,2]");
  array = std::move(copy);
  CHECK(array.toJsonString({.pretty = false}) == "[1,2]");
}

}  // namespace

void run() {
  object_keys_serialize_sorted();
  pretty_and_compact_differ_only_in_layout();
  strings_escape_quotes_backslashes_and_control_characters();
  doubles_serialize_with_the_classic_locale();
  non_finite_numbers_fall_back_to_null();
  assignment_can_extract_an_owned_child();
}

}  // namespace json_value

// Pins which language a document belongs to. One routing table and one answer:
// a URI that routed one way for the parse and another for the services would
// be two languages for one document.
namespace service_registry {

namespace {

cursiv::SharedCoreServices shared;

// A container complete enough to be registered: the registry refuses anything
// less.
std::unique_ptr<cursiv::CoreServices> language(
    std::string id, std::vector<std::string> extensions,
    std::vector<std::string> names = {}) {
  auto s = std::make_unique<cursiv::CoreServices>(shared);
  s->languageMetaData = {std::move(id), std::move(extensions),
                         std::move(names)};
  s->parser = ws::generated::Workspace::create_parser();
  s->nameProvider = std::make_unique<cursiv::DefaultNameProvider>(*s);
  s->astReflection = ws::generated::Workspace::create_ast_reflection();
  cursiv::install_default_core_services(*s);
  return s;
}

// The registry owns what it routes to, so a lane that wants to name a language
// keeps the address the registration handed over.
const cursiv::CoreServices* enroll(
    cursiv::DefaultServiceRegistry& registry,
    std::unique_ptr<cursiv::CoreServices> services) {
  const cursiv::CoreServices* const address = services.get();
  registry.registerServices(std::move(services));
  return address;
}

// A shared container that owes a required slot is refused before any language
// claims -- the antecedent that lets a required slot go unguarded at its use.
void an_incomplete_shared_container_is_refused() {
  cursiv::SharedCoreServices bare;
  cursiv::DefaultServiceRegistry registry(bare);
  bool threw = false;
  try {
    registry.registerServices(std::make_unique<cursiv::CoreServices>(bare));
  } catch (const cursiv::ServiceRegistrationError& refusal) {
    // The shared gate speaks, not the language one: that is what pins it.
    threw = std::string_view(refusal.what()).find("shared services") !=
            std::string_view::npos;
  }
  CHECK(threw);
}

void an_incomplete_language_is_refused() {
  cursiv::DefaultServiceRegistry registry(shared);

  bool threw = false;
  try {
    registry.registerServices(std::make_unique<cursiv::CoreServices>(shared));
  } catch (const cursiv::ServiceRegistrationError&) {
    threw = true;
  }
  CHECK(threw);

  // Named, but owing a parser: still refused. This is what keeps a half-built
  // language out of the build.
  auto parserless = std::make_unique<cursiv::CoreServices>(shared);
  parserless->languageMetaData = {"ws", {".ws"}, {}};
  threw = false;
  try {
    registry.registerServices(std::move(parserless));
  } catch (const cursiv::ServiceRegistrationError&) {
    threw = true;
  }
  CHECK(threw);
  CHECK(registry.all().empty());
}

void a_uri_routes_by_extension() {
  cursiv::DefaultServiceRegistry registry(shared);
  const cursiv::CoreServices* const ws =
      enroll(registry, language("ws", {".ws"}));

  CHECK(&registry.getServices("file:///a/b.ws") == ws);
  CHECK(registry.hasServices("file:///a/b.ws"));
  // A workspace routinely holds files no language owns. `findServices` says so
  // with a null; `getServices` throws, because its caller asked for a language.
  CHECK(registry.findServices("file:///a/b.txt") == nullptr);
  CHECK(!registry.hasServices("file:///a/b.txt"));
  bool threw = false;
  try {
    (void)registry.getServices("file:///a/b.txt");
  } catch (const cursiv::ServiceRegistryError&) {
    threw = true;
  }
  CHECK(threw);
}

// The registration spelling and the URI spelling must land in the same row.
void extensions_meet_however_they_are_spelled() {
  cursiv::DefaultServiceRegistry registry(shared);
  const cursiv::CoreServices* const one =
      enroll(registry, language("one", {"Model"}));  // no dot, upper case
  CHECK(registry.findServices("file:///a/b.model") == one);
  CHECK(registry.findServices("file:///a/b.MODEL") == one);
}

// An explicit id outranks everything, then the exact file name, then the
// extension.
void precedence_is_id_then_name_then_extension() {
  cursiv::DefaultServiceRegistry registry(shared);
  const cursiv::CoreServices* const byExt =
      enroll(registry, language("ext", {".cfg"}));
  const cursiv::CoreServices* const byName =
      enroll(registry, language("name", {}, {"special.cfg"}));
  const cursiv::CoreServices* const byId = enroll(registry, language("id", {}));

  CHECK(registry.findServices("file:///a/other.cfg") == byExt);
  CHECK(registry.findServices("file:///a/special.cfg") == byName);
  CHECK(registry.findServices("file:///a/special.cfg", "id") == byId);
  // An id nobody registered falls through to the URI, it does not veto.
  CHECK(registry.findServices("file:///a/special.cfg", "nope") == byName);
}

// A query or a fragment is not part of the file name, and a dotted directory
// gives no extension.
void routing_reads_the_file_name_only() {
  cursiv::DefaultServiceRegistry registry(shared);
  const cursiv::CoreServices* const ws =
      enroll(registry, language("ws", {".ws"}));
  CHECK(registry.findServices("file:///a/b.ws?v=2") == ws);
  CHECK(registry.findServices("file:///a/b.ws#top") == ws);
  CHECK(registry.findServices("file:///a.ws/b") == nullptr);
  CHECK(registry.findServices("file:///a/b/") == nullptr);
}

void routing_uses_the_decoded_uri_path() {
  cursiv::DefaultServiceRegistry registry(shared);
  const cursiv::CoreServices* const byExtension =
      enroll(registry, language("extension", {".ws", ".host", ".env", "."}));
  const cursiv::CoreServices* const byName =
      enroll(registry, language("name", {}, {"Makefile", ".env"}));

  CHECK(registry.findServices("untitled:Makefile") == byName);
  CHECK(registry.findServices("untitled:%4Dakefile?other.ws#top") == byName);
  CHECK(registry.findServices("mem://Makefile") == nullptr);
  CHECK(registry.findServices("mem://my.host?other.ws#top") == nullptr);
  CHECK(registry.findServices("mem://my.host/Makefile///") == byName);
  CHECK(registry.findServices("mem:///a.ws///") == byExtension);
  CHECK(registry.findServices("untitled:.ws") == nullptr);
  CHECK(registry.findServices("untitled:.env") == byName);
  CHECK(registry.findServices("mem:///..") == nullptr);
  CHECK(registry.findServices("mem:///...") == byExtension);
  CHECK(registry.findServices("mem:///a%2Fb%2Ews%2F") == byExtension);
}

void a_later_registration_replaces_the_mapping() {
  cursiv::DefaultServiceRegistry registry(shared);
  const cursiv::CoreServices* const first =
      enroll(registry, language("first", {".cfg"}));
  const cursiv::CoreServices* const second =
      enroll(registry, language("second", {".cfg"}));

  CHECK(registry.findServices("file:///a/b.cfg") == second);
  // Both are still registered languages; only the claim moved.
  CHECK(registry.all().size() == 2);
  CHECK(registry.all()[0] == first);
}

// Re-registering the same id retires the old container and keeps the rank:
// registration order is the order a consumer walks.
void re_registering_an_id_keeps_its_rank() {
  cursiv::DefaultServiceRegistry registry(shared);
  const cursiv::CoreServices* const a =
      enroll(registry, language("a", {".aa", ".ab"}));
  const cursiv::CoreServices* const b =
      enroll(registry, language("b", {".bb"}));
  // `a` again, narrowed to one extension.
  const cursiv::CoreServices* const a2 =
      enroll(registry, language("a", {".aa"}));

  CHECK(registry.all().size() == 2);
  CHECK(registry.all()[0] == a2);
  CHECK(registry.all()[1] == b);
  CHECK(a != a2);
  CHECK(registry.findServices("file:///x.aa") == a2);
  // The narrowed-away extension routes to nothing, not to a corpse.
  CHECK(registry.findServices("file:///x.ab") == nullptr);
  CHECK(registry.findServices("file:///x.bb") == b);
}

void a_retired_container_outlives_its_documents() {
  bool destroyed = false;
  struct TrackedServices final : cursiv::CoreServices {
    bool& destroyed;
    explicit TrackedServices(bool& flag)
        : CoreServices(service_registry::shared), destroyed(flag) {}
    ~TrackedServices() noexcept override { destroyed = true; }
  };
  {
    cursiv::DefaultServiceRegistry registry(shared);
    auto first = std::make_unique<TrackedServices>(destroyed);
    first->languageMetaData = {"tracked", {".ws"}, {}};
    first->parser = ws::generated::Workspace::create_parser();
    first->astReflection = ws::generated::Workspace::create_ast_reflection();
    cursiv::install_default_core_services(*first);
    registry.registerServices(std::move(first));
    registry.registerServices(language("tracked", {".ws"}));
    CHECK(!destroyed);
    CHECK(registry.all().size() == 1);
  }
  CHECK(destroyed);
}

// Nothing at all was ever registered: its own sentence, and the null answers.
void an_empty_registry_says_so() {
  cursiv::DefaultServiceRegistry registry(shared);
  CHECK(registry.all().empty());
  CHECK(registry.findServices("file:///a/b.ws") == nullptr);
  CHECK(!registry.hasServices("file:///a/b.ws"));
  bool threw = false;
  try {
    (void)registry.getServices("file:///a/b.ws");
  } catch (const cursiv::ServiceRegistryError& refusal) {
    threw = std::string_view(refusal.what()).find("empty") !=
            std::string_view::npos;
  }
  CHECK(threw);
}

// No container is not a registration.
void a_null_registration_is_refused() {
  cursiv::DefaultServiceRegistry registry(shared);
  bool threw = false;
  try {
    registry.registerServices(nullptr);
  } catch (const cursiv::ServiceRegistrationError&) {
    threw = true;
  }
  CHECK(threw);
}

}  // namespace

void run() {
  cursiv::install_default_shared_core_services(shared);
  an_incomplete_shared_container_is_refused();
  an_incomplete_language_is_refused();
  a_uri_routes_by_extension();
  extensions_meet_however_they_are_spelled();
  precedence_is_id_then_name_then_extension();
  routing_reads_the_file_name_only();
  routing_uses_the_decoded_uri_path();
  a_later_registration_replaces_the_mapping();
  re_registering_an_id_keeps_its_rank();
  a_retired_container_outlives_its_documents();
  an_empty_registry_says_so();
  a_null_registration_is_refused();
}

}  // namespace service_registry

// Pins the completeness invariant: the answer reads the container's own rows,
// so a service added in one place cannot be forgotten in another.
namespace service_slot {

namespace {

struct Container {
  std::vector<cursiv::ServiceSlot> slots;
  void visitServices(
      cursiv::function_ref<void(const cursiv::ServiceSlot&)> yield) const {
    for (const cursiv::ServiceSlot& s : slots) yield(s);
  }
};

void a_complete_container_names_nothing() {
  const Container c{{{"parser", true, true}, {"linker", true, true}}};
  CHECK(cursiv::first_missing_service(c).empty());
}

void the_first_required_hole_is_the_answer() {
  const Container c{{{"parser", true, true},
                     {"linker", true, false},
                     {"validator", true, false}}};
  CHECK(cursiv::first_missing_service(c) == "linker");
}

// An optional slot left empty is not a hole.
void what_is_not_required_is_not_owed() {
  const Container c{{{"documentation", false, false}, {"parser", true, true}}};
  CHECK(cursiv::first_missing_service(c).empty());
}

void an_empty_container_owes_nothing() {
  CHECK(cursiv::first_missing_service(Container{}).empty());
}

}  // namespace

void run() {
  a_complete_container_names_nothing();
  the_first_required_hole_is_the_answer();
  what_is_not_required_is_not_owed();
  an_empty_container_owes_nothing();
}

}  // namespace service_slot

// Pins that a combined container -- a language that is both a core language
// and an LSP one -- emits each service row exactly once through the diamond.
// The split (`visitOwnServices`) is what this guards: an override calling the
// wrong side would double the core rows, and nothing else would notice.
namespace shared_core_services {

namespace {

void a_combined_container_emits_each_row_once() {
  const ws::WorkspaceLanguage world = ws::create_lsp_services();

  std::unordered_map<std::string, int> counts;
  std::size_t rows = 0;
  world.language.visitServices([&](const cursiv::ServiceSlot& slot) {
    ++counts[std::string(slot.name)];
    ++rows;
  });
  CHECK(counts.size() == rows);
  for (const auto& [name, seen] : counts) {
    CHECK(seen == 1);
    if (seen != 1) CHECK(name.empty());  // name the duplicate in the log
  }
  // All three storeys answered: a core row, the language's own, an LSP one.
  CHECK(counts.contains("parser"));
  CHECK(counts.contains("qualifiedNameProvider"));
  CHECK(counts.contains("hoverProvider"));
}

}  // namespace

void run() {
  a_combined_container_emits_each_row_once();
}

}  // namespace shared_core_services

// Pins the runtime's side channel: an observation renders on one line, and
// the two workspace-level failures that owe no client an answer are said --
// a colliding extension, a root the scan was refused.
namespace observability_sink {

namespace {

using support::CaptureSink;

// A workspace whose observations are kept, over `disk` -- none, an empty one.
struct Observed {
  cursiv::SharedCoreServices shared;
  CaptureSink* capture = &support::capture_observations(shared);

  explicit Observed(std::unique_ptr<cursiv::FileSystemProvider> disk = {}) {
    shared.fileSystemProvider = std::move(disk);
    cursiv::install_default_shared_core_services(shared);
  }
};

void an_observation_is_one_line() {
  const cursiv::Observation observation{
      .severity = cursiv::ObservationSeverity::Warning,
      .code = cursiv::ObservationCode::WorkspaceDirectoryReadFailed,
      .message = "line one\nline two\r\nthree",
      .uri = "file:///w",
      .languageId = "ws"};
  const std::string line = cursiv::format_observation(observation);
  CHECK(line.find('\n') == std::string::npos);
  CHECK(line.find('\r') == std::string::npos);
  CHECK(line.find("warning WorkspaceDirectoryReadFailed:") == 0);
  CHECK(line.find("uri=file:///w") != std::string::npos);
  CHECK(line.find("languageId=ws") != std::string::npos);
}

// `publish` may run on any thread; the static mutex inside it is what keeps
// the header's promise that two threads' lines never splice into one.
#ifndef __wasi__
// Capturing the process's stderr needs dup/dup2, which WASI does not provide.
void concurrent_publishes_never_splice_two_lines() {
  std::fflush(stderr);
  const int saved = dup(fileno(stderr));
  std::FILE* const capture = std::tmpfile();
  dup2(fileno(capture), fileno(stderr));

  constexpr int kThreads = 8;
  constexpr int kPerThread = 300;
  constexpr int kRunLength = 64;
  cursiv::StderrObservabilitySink sink;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&sink, t] {
      cursiv::Observation observation;
      observation.uri = std::string(kRunLength, static_cast<char>('A' + t));
      for (int i = 0; i < kPerThread; ++i) sink.publish(observation);
    });
  }
  for (std::thread& th : threads) th.join();

  std::fflush(stderr);
  dup2(saved, fileno(stderr));
  close(saved);
  std::rewind(capture);

  int lines = 0;
  bool corrupted = false;
  std::string line;
  for (int c = std::fgetc(capture); c != EOF; c = std::fgetc(capture)) {
    if (c != '\n') {
      line.push_back(static_cast<char>(c));
      continue;
    }
    if (!line.empty()) {
      ++lines;
      const std::size_t at = line.find("uri=");
      const std::string_view run = at == std::string::npos
                                       ? std::string_view{}
                                       : std::string_view(line).substr(at + 4);
      if (run.size() != static_cast<std::size_t>(kRunLength)) {
        corrupted = true;
      } else {
        for (const char ch : run)
          if (ch != run.front()) corrupted = true;
      }
    }
    line.clear();
  }
  std::fclose(capture);

  CHECK(!corrupted);
  CHECK(lines == kThreads * kPerThread);
}
#endif

[[nodiscard]] std::unique_ptr<cursiv::CoreServices> language(
    cursiv::SharedCoreServices& shared, std::string id, std::string extension) {
  auto services = std::make_unique<cursiv::CoreServices>(shared);
  services->languageMetaData = {std::move(id), {std::move(extension)}, {}};
  services->parser = ws::generated::Workspace::create_parser();
  services->astReflection = ws::generated::Workspace::create_ast_reflection();
  cursiv::install_default_core_services(*services);
  return services;
}

// The later registration owns the extension, and the collision is said.
void a_colliding_extension_is_said() {
  Observed world;
  auto& [shared, capture] = world;

  shared.serviceRegistry->registerServices(language(shared, "one", ".x"));
  CHECK(capture->seen.empty());
  shared.serviceRegistry->registerServices(language(shared, "two", ".x"));
  CHECK(capture->seen.size() == 1);
  CHECK(capture->seen[0].code ==
        cursiv::ObservationCode::LanguageMappingCollision);
  CHECK(capture->seen[0].languageId == "two");
  // The message names the normalised key, which is what routes.
  CHECK(capture->seen[0].message.find("'.x'") != std::string::npos);
  // Re-registering a language is a replacement, never a collision with itself.
  shared.serviceRegistry->registerServices(language(shared, "two", ".x"));
  CHECK(capture->seen.size() == 1);
}

// A colliding file name is said too.
void a_colliding_file_name_is_said() {
  Observed world;
  auto& [shared, capture] = world;

  auto one = language(shared, "one", ".a");
  one->languageMetaData.fileNames = {"BUILD"};
  shared.serviceRegistry->registerServices(std::move(one));
  auto two = language(shared, "two", ".b");
  two->languageMetaData.fileNames = {"BUILD"};
  shared.serviceRegistry->registerServices(std::move(two));
  CHECK(capture->seen.size() == 1);
  CHECK(capture->seen[0].message.find("file name 'BUILD'") !=
        std::string::npos);
}

// A provider that refuses one URI, listed or read, and serves every other.
class SubfolderRefusingFileSystemProvider final
    : public cursiv::FileSystemProvider {
 public:
  explicit SubfolderRefusingFileSystemProvider(std::string refused)
      : _refused(std::move(refused)) {}
  void addFile(std::string_view uri, std::string text) {
    _inner.addFile(uri, std::move(text));
  }
  [[nodiscard]] cursiv::FileSystemNode stat(
      std::string_view uri) const override {
    return _inner.stat(uri);
  }
  [[nodiscard]] bool exists(std::string_view uri) const override {
    return _inner.exists(uri);
  }
  [[nodiscard]] std::vector<std::uint8_t> readBinary(
      std::string_view uri) const override {
    return _inner.readBinary(uri);
  }
  [[nodiscard]] std::string readFile(std::string_view uri) const override {
    if (uri == _refused) throw cursiv::FileSystemError("refused");
    return _inner.readFile(uri);
  }
  [[nodiscard]] std::vector<cursiv::FileSystemNode> readDirectory(
      std::string_view uri) const override {
    if (uri == _refused) throw cursiv::FileSystemError("refused");
    return _inner.readDirectory(uri);
  }

 private:
  cursiv::MemoryFileSystemProvider _inner;
  std::string _refused;
};

// A root's refusal is said as the bootstrap failure it is, and stepped over:
// the other folders are read and `ready` resolves -- or every request awaiting
// the scan would wait for the session.
void a_refused_root_is_said_and_the_others_are_read() {
  auto provider =
      std::make_unique<SubfolderRefusingFileSystemProvider>("file:///gone");
  provider->addFile("file:///w/a.ws", "datatype A\n");
  Observed world(std::move(provider));
  auto& [shared, capture] = world;
  (void)support::add_workspace<cursiv::CoreServices>(shared);

  const std::vector<cursiv::WorkspaceFolder> folders = {
      {"file:///gone", "gone"}, {"file:///w", "w"}};
  bool thrown = false;
  try {
    shared.workspaceManager->initializeWorkspace(folders);
  } catch (const cursiv::FileSystemError&) {
    thrown = true;
  }
  CHECK(!thrown);
  CHECK(shared.documents->getDocument("file:///w/a.ws") != nullptr);
  CHECK(shared.workspaceManager->ready().wait_for(std::chrono::seconds(0)) ==
        std::future_status::ready);
  CHECK(capture->seen.size() == 1);
  CHECK(!capture->seen.empty() &&
        capture->seen[0].code ==
            cursiv::ObservationCode::WorkspaceBootstrapFailed);
  CHECK(!capture->seen.empty() &&
        capture->seen[0].severity == cursiv::ObservationSeverity::Error);
}

// A subfolder's refusal is said and stepped over: the scan keeps its
// siblings, and nothing propagates.
void a_refused_subfolder_is_said_and_stepped_over() {
  auto provider =
      std::make_unique<SubfolderRefusingFileSystemProvider>("file:///w/sub");
  provider->addFile("file:///w/a.ws", "datatype A\n");
  provider->addFile("file:///w/sub/b.ws", "datatype B\n");
  provider->addFile("file:///w/other/c.ws", "datatype C\n");
  Observed world(std::move(provider));
  auto& [shared, capture] = world;
  (void)support::add_workspace<cursiv::CoreServices>(shared);

  const std::vector<cursiv::WorkspaceFolder> folders = {{"file:///w", "w"}};
  shared.workspaceManager->initializeWorkspace(folders);

  CHECK(shared.documents->getDocument("file:///w/a.ws") != nullptr);
  CHECK(shared.documents->getDocument("file:///w/other/c.ws") != nullptr);
  CHECK(shared.documents->getDocument("file:///w/sub/b.ws") == nullptr);
  CHECK(capture->seen.size() == 1);
  CHECK(capture->seen[0].code ==
        cursiv::ObservationCode::WorkspaceDirectoryReadFailed);
  CHECK(capture->seen[0].severity == cursiv::ObservationSeverity::Warning);
}

// A file the scan found and could not read is said, as one an update cannot
// read is, and left out; the files beside it are read.
void an_unreadable_file_is_said_and_the_others_are_read() {
  auto provider =
      std::make_unique<SubfolderRefusingFileSystemProvider>("file:///w/b.ws");
  provider->addFile("file:///w/a.ws", "datatype A\n");
  provider->addFile("file:///w/b.ws", "datatype B\n");
  Observed world(std::move(provider));
  auto& [shared, capture] = world;
  (void)support::add_workspace<cursiv::CoreServices>(shared);

  const std::vector<cursiv::WorkspaceFolder> folders = {{"file:///w", "w"}};
  shared.workspaceManager->initializeWorkspace(folders);

  CHECK(shared.documents->getDocument("file:///w/a.ws") != nullptr);
  CHECK(shared.documents->getDocument("file:///w/b.ws") == nullptr);
  CHECK(capture->seen.size() == 1);
  CHECK(!capture->seen.empty() &&
        capture->seen[0].code ==
            cursiv::ObservationCode::WorkspaceDocumentReadFailed &&
        capture->seen[0].severity == cursiv::ObservationSeverity::Warning &&
        capture->seen[0].uri == "file:///w/b.ws");
}

// A changed folder an update cannot list is said as the folder it is, not as
// the workspace root the scan read.
void a_changed_folder_no_update_can_list_is_a_folder() {
  auto provider =
      std::make_unique<SubfolderRefusingFileSystemProvider>("file:///w/sub");
  provider->addFile("file:///w/a.ws", "datatype A\n");
  provider->addFile("file:///w/sub/b.ws", "datatype B\n");
  Observed world(std::move(provider));
  auto& [shared, capture] = world;
  (void)support::add_workspace<cursiv::CoreServices>(shared);
  const std::vector<cursiv::WorkspaceFolder> folders = {{"file:///w", "w"}};
  shared.workspaceManager->initializeWorkspace(folders);
  capture->seen.clear();

  const std::string_view changed[] = {"file:///w/sub"};
  shared.documentBuilder->update(changed, {}, {});
  CHECK(capture->seen.size() == 1);
  CHECK(!capture->seen.empty() &&
        capture->seen[0].code ==
            cursiv::ObservationCode::WorkspaceDirectoryReadFailed &&
        capture->seen[0].severity == cursiv::ObservationSeverity::Warning &&
        capture->seen[0].uri == "file:///w/sub");
}

}  // namespace

void run() {
  an_observation_is_one_line();
#ifndef __wasi__
  concurrent_publishes_never_splice_two_lines();
#endif
  a_colliding_extension_is_said();
  a_colliding_file_name_is_said();
  a_refused_root_is_said_and_the_others_are_read();
  a_refused_subfolder_is_said_and_stepped_over();
  an_unreadable_file_is_said_and_the_others_are_read();
  a_changed_folder_no_update_can_list_is_a_folder();
}

}  // namespace observability_sink

int main() {
  core_language::run();
  core_services::run();
  diagnostic::run();
  json_value::run();
  service_registry::run();
  service_slot::run();
  shared_core_services::run();
  observability_sink::run();
  return cursiv::test::report("services");
}
