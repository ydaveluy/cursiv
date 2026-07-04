# Multiple Languages

Integrate several dependent languages in one Pegium workspace. A common driver: one file type defines concepts and another consumes them. Pegium's `requirements` example does exactly that, with `.req` and `.tst` files sharing a workspace.

## Core idea

Both languages share **one** custom shared type. In the requirements example that is `RequirementsSharedCoreServices` (deriving `pegium::SharedCoreServices`), and each concrete language gets its own container built from it. When you need editor features, layer the LSP variant `RequirementsSharedServices` (deriving `pegium::SharedServices`) on top of the same core setup.

```cpp
// One shared type, used by both languages.
struct RequirementsSharedCoreServices : virtual pegium::SharedCoreServices {
  // define here custom shared services
};

// Each language derives CoreServicesFor<...> with the same shared type.
struct RequirementsCoreServices
    : pegium::CoreServicesFor<RequirementsSharedCoreServices> {
  std::unique_ptr<validation::RequirementsValidator> validator;
  explicit RequirementsCoreServices(
      const RequirementsSharedCoreServices &sharedServices)
      : pegium::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}
};

struct TestsCoreServices
    : pegium::CoreServicesFor<RequirementsSharedCoreServices> {
  std::unique_ptr<validation::TestsValidator> validator;
  explicit TestsCoreServices(
      const RequirementsSharedCoreServices &sharedServices)
      : pegium::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}
};
```

The explicit two-init constructor is irreducible: `pegium::CoreServices` is a virtual base with no default constructor, so the most-derived container must initialize it directly.

This means:

- Documents, indexing, and workspace infrastructure stay shared.
- Each language chooses its own parser.
- Each language installs its own validators and reference logic.
- LSP features such as formatters stay in a dedicated LSP layer.
- All registered languages become visible to the same service registry.

## A living example

See `examples/requirements/src/requirements/core/CoreModule.cpp`. It builds one service factory for requirements files and another for test files. Each `create*` factory takes the language's **typed** shared type and installs the language's core module (the container *is-a* `RequirementsCoreServices`, so `install*` is a single-argument function):

```cpp
std::unique_ptr<RequirementsCoreServices>
createRequirementsCoreServices(
    const RequirementsSharedCoreServices &sharedServices,
    std::string languageId) {
  auto services = pegium::makeDefaultCoreServices<RequirementsCoreServices>(
      sharedServices, std::move(languageId));
  installRequirementsCoreModule(*services);
  return services;
}
```

The second factory, `createTestsCoreServices`, follows the same pattern but swaps parser, extensions, and validator for the `tests-lang` language.

The LSP-specific formatters live separately in `examples/requirements/src/requirements/lsp/LspModule.cpp`, where the same core setup is reused and the formatter is added on top. There the `create*` factories build with `pegium::makeDefaultServices<RequirementsServices>` and call `installRequirementsCoreModule(*services)` followed by `installRequirementsLspModule(*services)`.

## Registration

Once each language has its own core service object, register them all in the shared registry. The `register*` function takes the **base** `pegium::SharedCoreServices &` handed to it by the workspace, then `dynamic_cast`s it back to the languages' own shared type to build the containers:

```cpp
bool registerRequirementsCoreServices(
    pegium::SharedCoreServices &sharedServices) {
  auto services = createRequirementsAndTestsCoreServices(
      dynamic_cast<RequirementsSharedCoreServices &>(sharedServices));
  sharedServices.serviceRegistry->registerServices(
      std::move(services.requirements));
  sharedServices.serviceRegistry->registerServices(std::move(services.tests));
  return true;
}
```

This step makes both languages available to document loading and indexing. The LSP registration function `registerRequirementsLspServices` shares the same shape but takes `pegium::SharedServices &`, downcasts to `RequirementsSharedServices &`, and returns the LSP-enabled service containers.

## Wiring the mains

The CLI seeds one shared container with the languages' shared type and registers both languages against it:

```cpp
auto sharedServices =
    pegium::make_shared_services<requirements::RequirementsSharedCoreServices>();
auto &shared = *sharedServices;
auto services = requirements::createRequirementsAndTestsCoreServices(shared);
shared.serviceRegistry->registerServices(std::move(services.requirements));
shared.serviceRegistry->registerServices(std::move(services.tests));
```

The LSP server hands its shared type to `runLanguageServerMain`, which owns the session container and calls the registration function:

```cpp
return pegium::runLanguageServerMain<requirements::RequirementsSharedServices>(
    argc, argv, "requirements-lsp",
    requirements::registerRequirementsLspServices);
```

## What changes when you split a language

Review these points for each language:

- Parser type
- Language id
- File extensions
- Validation registration
- Scoping and reference behavior
- Formatter or other LSP providers, when you expose editor features
- Editor integration such as syntax highlighting or VS Code contributions

Keep the shared services common unless you want isolated workspaces.

## When not to split

Do not split a language just because the grammar has several entry rules. Keep one language when the files share the same lifecycle, editor behavior, and symbol space.

Split into multiple languages when the file types have clearly distinct identities, or when separate parsers and editor behavior make the model easier to maintain.

## Related pages

- [Examples: Requirements](../examples/requirements.md)
- [Scoping](scoping/index.md)
- [Configuration Services](../reference/configuration-services.md)
