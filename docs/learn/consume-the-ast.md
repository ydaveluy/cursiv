# Run a Language Headlessly

Once your language parses, the payoff is doing something with the result —
interpret it, generate code, query it, transform it. The headless path takes a
source file, builds the typed AST, and walks it, following the statemachine CLI
([`cli/main.cpp`](https://github.com/ydaveluy/pegium/tree/main/examples/statemachine/src/statemachine/cli/main.cpp)).

## The headless pipeline

```cpp
#include <pegium/cli/CliUtils.hpp>
#include <statemachine/core/CoreModule.hpp>

int run(const std::string &fileName) {
  // 1. A shared runtime plus your registered language.
  auto sharedServices =
      pegium::make_shared_services<statemachine::StatemachineSharedCoreServices>();
  auto &shared = *sharedServices;
  auto services = statemachine::createStatemachineCoreServices(shared);
  auto &languageServices = *services;
  shared.serviceRegistry->registerServices(std::move(services));

  // 2. Parse + build + validate the file in one call.
  auto document =
      pegium::build_document_from_path(fileName, languageServices);
  if (pegium::has_error_diagnostics(*document)) {
    pegium::print_error_diagnostics(*document, std::cerr);
    return 2;
  }

  // 3. Get the typed root node and walk it.
  auto *model = pegium::ast_ptr_cast<statemachine::ast::Statemachine>(
      document->parseResult.value);
  if (model == nullptr) {
    return 2;
  }

  for (const auto &state : model->states) {
    if (!state) {
      continue;
    }
    std::cout << "state " << state->name << "\n";
    for (const auto &transition : state->transitions) {
      if (transition && transition->event && transition->state) {
        std::cout << "  " << transition->event->name << " => "
                  << transition->state->name << "\n";
      }
    }
  }
  return 0;
}
```

## What each step gives you

- `make_shared_services()` builds the process-wide runtime; you register your
  language on it exactly as your `main` would.
- `build_document_from_path(path, services)` runs the whole core pipeline —
  parse → index content → compute scopes → link references → validate — and
  returns the built `Document`.
- `has_error_diagnostics(document)` is the gate: stop here (and
  `print_error_diagnostics`) if the input has errors. `document->diagnostics`
  holds the full list when you need to inspect it.
- `document->parseResult.value` is the root AST node. `ast_ptr_cast<T>(...)`
  types it (returning `nullptr` if the actual type does not match).

## Working with the tree

- **Owned children** are `pointer<T>` (raw, arena-owned pointers) inside a
  `vector<>`. They can be null if recovery produced a partial tree, so guard with
  `if (!state) continue;`.
- **Cross-references** are `reference<T>` members. They resolve lazily through the
  linker: `transition->event` converts to `bool` (false when unresolved) and
  `transition->event->name` dereferences to the linked node. Because
  `has_error_diagnostics` already rejected unresolved references, the remaining
  references are safe to follow — still, guarding (`if (transition->event)`) keeps
  the code robust against partial results.

## This is what a generator does

Walking the typed model is exactly how the statemachine example emits C++: its
[`cli/Generator.cpp`](https://github.com/ydaveluy/pegium/tree/main/examples/statemachine/src/statemachine/cli/Generator.cpp)
traverses `model->states` and `state->transitions` to produce a state-pattern
class hierarchy. Interpreters, linters, and transformers follow the same shape —
build the document once, then read the AST.

## Related pages

- [Build a Language End-to-End](walkthrough.md)
- [Test Your Language](test-your-language.md)
