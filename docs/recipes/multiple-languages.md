# Several languages in one project

One workspace and one binary can serve several languages, with references
from one to the other. In the `requirements` example, `.req` files declare
requirements, `.tst` files declare tests, and a test names the requirement it
covers.

## Two generated languages

Generate each language with its own id and extension. They may share one
grammar header:

```cmake title="src/CMakeLists.txt"
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

The header declares two grammar types, `req::Requirements` and `req::Tests`,
which can share AST types. Each generated language has its own parser.

## Register both languages

Define one installer per language and register both in the shared services:

```cpp title="core/CoreModule.cpp"
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

Each call creates its own services, even with the same container class. Add
each language's validation checks to its installer. The document's extension
selects the language: `a.req` uses the first, `a.tst` the second. The CLI's
`--help` lists both:

```
$ requirements_cli --help
  this binary serves .req, .tst
```

## References across languages

A `reference<Requirement>` in a test can resolve to a requirement declared in
a `.req` document, when both are built in the same workspace. From the CLI,
pass `--root <dir>` to load the folder; a single-file run sees only that
file's declarations.

## One CLI, one server

The command line takes the function that registers both languages:

```cpp
#include "cursiv/cli/cli.hpp"
#include "requirements/core/CoreModule.hpp"

int main(int argc, char** argv) {
  return cursiv::run_cli<req::RequirementsSharedCoreServices>(
      argc, argv, &req::register_core_services);
}
```

The language server works the same way with `register_lsp_services`: one
process serves both languages.

## Advanced: embed a grammar in a document

To parse another grammar *inside* a document, use
`section<Json>(open, close)`:

```cpp
struct Doc : cursiv::GrammarSpec {
  struct Block
      : rule<create<ast::Block>(
            kw<"json"> + assign<&ast::Block::json>(
                             section<Json>(lit<"'''">, lit<"'''">)))> {};
  // ...
};
```

`Json` is a grammar type. The enclosing rule reads the delimiters, and Json
reads the content with its own trivia and keywords. The AST mixes nodes from
both grammars. This combines parsers only: the enclosing language must
provide any validation or scoping for the Json nodes. See
[Embedded grammars](../reference/grammar.md#embedded-grammars) for the
delimiter restrictions.
