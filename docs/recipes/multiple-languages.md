# Several languages in one project

A workspace may hold more than one language, served by one binary, with
references that cross from one to the other. In the `requirements` example,
`.req` declares requirements, `.tst` declares tests, and a test
names the requirement it covers.

## Two generated languages

Two calls, two ids, two extensions — and they may share one grammar header:

`src/CMakeLists.txt`:

```cmake
cursiv_generate_language(ID         requirements-lang
    GRAMMAR    requirements/core/RequirementsGrammar.hpp
    NAMESPACE  req::Requirements
    EXTENSIONS .req
    OUT        requirements/generated)

cursiv_generate_language(ID         tests-lang
    GRAMMAR    requirements/core/RequirementsGrammar.hpp
    NAMESPACE  req::Tests
    EXTENSIONS .tst
    OUT        requirements/generated)

file(GLOB_RECURSE _core CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/requirements/core/*.cpp)
add_library(requirementsCore ${_core})
target_link_libraries(requirementsCore
    PUBLIC requirements-langGenerated tests-langGenerated)
```

The header declares two grammar types: `req::Requirements` and `req::Tests`.
They can use the same AST declarations. Each generated language has its own
parser and metadata.

## Register both languages

Define one installer per language and register both in the shared workspace.
Using the Requirements example's service types, the registration code is:

`core/CoreModule.cpp`:

```cpp
#include "cursiv/core/services/CoreLanguage.hpp"
#include "requirements/core/CoreModule.hpp"
#include "requirements/generated/Requirements/Module.hpp"
#include "requirements/generated/Tests/Module.hpp"

namespace req {

void install_requirements_core_module(RequirementsCoreServices& services) {
  req::generated::Requirements::install_generated_module(services);
}

void install_tests_core_module(RequirementsCoreServices& services) {
  req::generated::Tests::install_generated_module(services);
}

bool register_core_services(cursiv::SharedCoreServices& shared) {
  return cursiv::register_language<
             RequirementsCoreServices, RequirementsSharedCoreServices,
             install_requirements_core_module>(shared) &&
         cursiv::register_language<
             RequirementsCoreServices, RequirementsSharedCoreServices,
             install_tests_core_module>(shared);
}

}  // namespace req
```

These calls create separate service instances, even though both use the same
container class. Add each language's validation checks to its installer.
The registry selects services by the document's URI: `a.req` uses the first
language, and `a.tst` the second.

A binary that serves both announces both extensions:

```
$ requirements_cli --help
  this binary serves .req, .tst
```

## References across languages

A `reference<Requirement>` in the tests language can resolve to a declaration
exported by a requirements document. Both documents must be built in the same
workspace.

From the CLI, pass `--root <dir>` to load the workspace folder before
validating a file. A single-file run only sees that file's declarations.

## One CLI, one server

The command line takes a registrar that installs several languages:

```cpp
#include "cursiv/cli/cli.hpp"
#include "requirements/core/CoreModule.hpp"

int main(int argc, char** argv) {
  return cursiv::run_cli<req::RequirementsSharedCoreServices>(
      argc, argv, &req::register_core_services);
}
```

The server uses the same registration pattern: `register_lsp_services` installs
both containers into the shared one, and the editor sees two languages served
by one process.

## Advanced: embed a grammar in a document

Several registered languages process separate documents. To parse another
grammar *inside* a document, use `section<Json>(open, close)` instead:

```cpp
struct Doc : cursiv::GrammarSpec {
  struct Block
      : rule<create<ast::Block>(
            kw<"json"> + assign<&ast::Block::json>(
                             section<Json>(lit<"'''">, lit<"'''">)))> {};
  // ...
};
```

Here `Json` names a grammar type. The caller reads the opening and closing
tokens; Json's entry reads the content using its own trivia and lexical
settings. Its `eof` matches the section boundary. The AST can contain nodes
from both grammars.

This composes parsers, not service containers. It does not install Json's
validators or scope services for the embedded nodes. The enclosing language
must provide the services it needs for them. See
[Embedded grammars](../reference/grammar.md#embedded-grammars) for delimiter
restrictions and recovery behavior.
