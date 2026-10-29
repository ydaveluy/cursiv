# =============================================================================
# GENERATED FILE -- DO NOT EDIT.
# Produced by cmake/GenerateScaffold.cmake from cmake/cursiv-new.cmake.in and
# templates/project/, CMakePresets.json and cmake/wasi.cmake. Edit those
# and regenerate:  cmake -DCURSIV_TAG=main -P cmake/GenerateScaffold.cmake
# =============================================================================
# cursiv-new.cmake -- scaffold a new cursiv language. CMake only, no other
# tool. Usage:
#   cmake -DNAME=MyLang [-DEXT=.ml] [-DDIR=mylang]
#         [-DCURSIV_TAG=main]
#         -P cursiv-new.cmake
cmake_minimum_required(VERSION 3.14)

if(NOT DEFINED NAME)
  message(FATAL_ERROR "NAME is required: cmake -DNAME=MyLang -P cursiv-new.cmake")
endif()
# NAME is a C++ identifier because it becomes the grammar type, and its
# lowercase form becomes the project's namespace as well as the language id.
if(NOT NAME MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
  message(FATAL_ERROR "NAME must be a valid C++ identifier (got '${NAME}')")
endif()

set(CURSIV_NEW_CLASS "${NAME}")
string(TOLOWER "${NAME}" CURSIV_NEW_ID)
# The namespace is global, so leading underscores and double underscores are
# reserved as well as C++ keywords. Check both spellings before writing files.
set(_cursiv_keywords
    alignas alignof and and_eq asm auto bitand bitor bool break case catch
    char char8_t char16_t char32_t class compl concept const consteval constexpr
    constinit const_cast continue co_await co_return co_yield decltype default
    delete do double dynamic_cast else enum explicit export extern false float
    for friend goto if inline int long mutable namespace new noexcept not not_eq
    nullptr operator or or_eq private protected public register reinterpret_cast
    requires return short signed sizeof static static_assert static_cast struct
    switch template this thread_local throw true try typedef typeid typename
    union unsigned using virtual void volatile wchar_t while xor xor_eq)
if(CURSIV_NEW_ID IN_LIST _cursiv_keywords OR CURSIV_NEW_ID MATCHES "^_|__")
  message(FATAL_ERROR
      "NAME and its lowercase namespace must not be reserved C++ identifiers (got '${NAME}')")
endif()
if(NOT DEFINED EXT)
  set(EXT ".${CURSIV_NEW_ID}")
endif()
if(NOT EXT MATCHES "^\\.")
  message(FATAL_ERROR "EXT must start with '.' (got '${EXT}')")
endif()
set(CURSIV_NEW_EXT "${EXT}")
if(NOT DEFINED DIR)
  set(DIR "${CURSIV_NEW_ID}")
endif()
if(NOT DEFINED CURSIV_TAG)
  set(CURSIV_TAG "main")
endif()
set(CURSIV_NEW_TAG "${CURSIV_TAG}")

get_filename_component(_target "${DIR}" ABSOLUTE)
file(GLOB _existing LIST_DIRECTORIES true "${_target}/*" "${_target}/.*")
if(_existing)
  message(FATAL_ERROR "Target dir '${_target}' is not empty; refusing to overwrite")
endif()

# The placeholders are substituted in the path as well as in the content, so
# no template carries a hardcoded language id in a file or directory name.
function(_cursiv_new_emit rel content)
  string(CONFIGURE "${rel}" rel @ONLY)
  string(CONFIGURE "${content}" out @ONLY)
  file(WRITE "${_target}/${rel}" "${out}")
  message(STATUS "  + ${rel}")
endfunction()

function(_cursiv_new_finish)
  message(STATUS "Created ${CURSIV_NEW_ID} in ${_target}")
  message(STATUS "Next: cd ${DIR}")
  message(STATUS "      cmake -B build")
  message(STATUS "      cmake --build build -j")
endfunction()

message(STATUS "Scaffolding ${NAME} (id=${CURSIV_NEW_ID}, ext=${EXT})")

_cursiv_new_emit([==[.clang-format]==] [==[# Google style, but a long expression breaks before its operators (not its
# assignments), so the `|` and `+` of a grammar rule start lines.
BasedOnStyle: Google
BreakBeforeBinaryOperators: NonAssignment
]==])
_cursiv_new_emit([==[.github/workflows/ci.yml]==] [==[name: ci

on:
  push:
  pull_request:

permissions:
  contents: read

concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true

jobs:
  build:
    runs-on: ubuntu-26.04
    timeout-minutes: 60
    container: gcc:16
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          persist-credentials: false

      - name: Install build tools
        run: |
          apt-get update
          apt-get install -y --no-install-recommends cmake git ninja-build

      - name: Configure
        run: cmake --preset gcc-release

      - name: Build
        run: cmake --build --preset gcc-release --parallel 4

      - name: Test
        run: ctest --preset gcc-release --parallel 4 --output-on-failure
]==])
_cursiv_new_emit([==[.gitignore]==] [==[# The generated tree is build output: the generator rewrites it from the
# grammar, and a table is megabytes git has no use for.
generated/
/build*
/.cache/
/compile_commands.json
vscode/node_modules
vscode/out
vscode/bin
*.vsix
vscode/server/
]==])
_cursiv_new_emit([==[.vscode/extensions.json]==] [==[{
  "recommendations": [
    "ms-vscode.cmake-tools",
    "llvm-vs-code-extensions.vscode-clangd"
  ]
}
]==])
_cursiv_new_emit([==[.vscode/launch.json]==] [==[{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Run @CURSIV_NEW_CLASS@ Extension",
      "type": "extensionHost",
      "request": "launch",
      "runtimeExecutable": "${execPath}",
      "args": [
        "--extensionDevelopmentPath=${workspaceFolder}/vscode",
        "${workspaceFolder}/example"
      ],
      "outFiles": [
        "${workspaceFolder}/vscode/out/**/*.js"
      ],
      "env": {
        "CURSIV_SERVER_DIR": "${command:cmake.buildDirectory}/src"
      },
      "preLaunchTask": "build the server and the extension"
    }
  ]
}
]==])
_cursiv_new_emit([==[.vscode/settings.json]==] [==[{
  "cmake.useCMakePresets": "always",
  "cmake.copyCompileCommands": "${sourceDirectory}/compile_commands.json",
  "C_Cpp.intelliSenseEngine": "disabled",
  "C_Cpp.formatting": "disabled",
  "clangd.enable": true,
  "clangd.arguments": ["--background-index"],
  "[cpp]": {
    "editor.defaultFormatter": "llvm-vs-code-extensions.vscode-clangd"
  }
}
]==])
_cursiv_new_emit([==[.vscode/tasks.json]==] [==[{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "build the server and the extension",
      "detail": "Build @CURSIV_NEW_ID@_lsp in the active preset's tree, then bundle the TypeScript.",
      "dependsOrder": "sequence",
      "dependsOn": [
        "build the language server",
        "install extension dependencies",
        "compile the extension"
      ],
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": []
    },
    {
      "label": "build the language server",
      "type": "process",
      "command": "cmake",
      "args": [
        "--build", "${command:cmake.buildDirectory}",
        "--target", "@CURSIV_NEW_ID@_lsp", "--parallel"
      ],
      "options": { "cwd": "${workspaceFolder}" },
      "problemMatcher": ["$gcc"],
      "presentation": { "reveal": "silent", "panel": "shared", "clear": true }
    },
    {
      "label": "install extension dependencies",
      "type": "shell",
      "command": "npm",
      "args": ["install", "--no-audit", "--no-fund"],
      "options": { "cwd": "${workspaceFolder}/vscode" },
      "problemMatcher": []
    },
    {
      "label": "compile the extension",
      "type": "shell",
      "command": "npm",
      "args": ["run", "compile"],
      "options": { "cwd": "${workspaceFolder}/vscode" },
      "problemMatcher": []
    }
  ]
}
]==])
_cursiv_new_emit([==[CMakeLists.txt]==] [==[cmake_minimum_required(VERSION 3.30)
project(@CURSIV_NEW_ID@ LANGUAGES CXX)

include(FetchContent)
# A local checkout wins over the tag: `-DCURSIV_SOURCE_DIR=<path>` is how you
# build this language against an engine you are also editing.
if(CURSIV_SOURCE_DIR)
  FetchContent_Declare(cursiv SOURCE_DIR ${CURSIV_SOURCE_DIR})
else()
  FetchContent_Declare(cursiv
      GIT_REPOSITORY https://github.com/ydaveluy/cursiv.git
      GIT_TAG        @CURSIV_NEW_TAG@)
endif()
FetchContent_MakeAvailable(cursiv)

# FetchContent may use a local override or a shared download directory.
if(CMAKE_SYSTEM_NAME STREQUAL "WASI" AND CURSIV_NODE)
  set(CMAKE_CROSSCOMPILING_EMULATOR ${CURSIV_NODE} --no-warnings
      "${cursiv_SOURCE_DIR}/tools/wasi-run.mjs")
endif()

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
]==])
_cursiv_new_emit([==[README.md]==] [==[# @CURSIV_NEW_CLASS@

A language project created with [Cursiv](https://ydaveluy.github.io/cursiv/).
Start with the sample, then edit the grammar and AST to define your language.

## Build and run

You need CMake 3.30 or later, Git and a C++20 compiler with `std::format` and
`musttail` support. Cursiv's CI uses GCC 16 and Clang 22.

```bash
cmake -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/src/@CURSIV_NEW_ID@_cli example/hello@CURSIV_NEW_EXT@
```

CMake generates the parser automatically, including when the grammar changes.
Generated files are under `build/src/generated-src/`; do not edit them.

## The sample language

```text
person Alice
person Bob

Hello Alice!
Hello Bob!
```

`person` declares a name and `Hello` refers to it. An unknown name produces a
diagnostic. The same references support go-to-definition and rename in the
editor.

Use `--dump` to inspect the concrete syntax tree (CST):

```bash
./build/src/@CURSIV_NEW_ID@_cli --dump example/hello@CURSIV_NEW_EXT@
```

The CLI returns `0` for no errors, `1` for document errors, `2` for invalid
arguments or paths, and `3` for an internal or output error.

## Customize the language

| To change… | Edit |
|---|---|
| Syntax | `src/@CURSIV_NEW_ID@/core/@CURSIV_NEW_CLASS@Grammar.hpp` |
| AST types | `src/@CURSIV_NEW_ID@/core/ast.hpp` |
| Validation rules | `src/@CURSIV_NEW_ID@/core/validation/` |
| Language services | `src/@CURSIV_NEW_ID@/core/CoreServices.hpp` and `CoreModule.cpp` |
| CLI behavior | `src/@CURSIV_NEW_ID@/cli/main.cpp` |
| Editor features | `src/@CURSIV_NEW_ID@/lsp/LspModule.cpp` |
| Tests | `tests/grammar_test.cpp` |

Follow the [language tutorial](https://ydaveluy.github.io/cursiv/learn/walkthrough/)
to replace the sample with a complete language.

## Open the editor

Install Node.js, npm and the recommended VS Code extensions, including CMake
Tools and clangd. Native servers support VS Code 1.86 or later; WASI servers
require 1.101 or later. Open this project folder in VS Code, configure the
CMake build and press **F5**. The launch task builds the server and client,
then opens `example/` in a development window.

Native GCC/Clang presets run the native server. WASI presets run a WebAssembly
server through Node. See
[Editor integration](https://ydaveluy.github.io/cursiv/learn/editor-integration/)
for customization and packaging.

## Measure performance

```bash
cmake --build build --target @CURSIV_NEW_ID@_bench
./build/src/@CURSIV_NEW_ID@_bench example/hello@CURSIV_NEW_EXT@ --iterations 5
```

The benchmark measures parsing, document builds and installed editor providers.
Use `--json bench.json` or `--csv bench.csv` to export results, and `--help` for
all options. Its entry point is `src/@CURSIV_NEW_ID@/bench/main.cpp`.

## Optional components

The CLI, server, benchmark and VS Code extension are included by default.
Remove the component folders you do not need; CMake omits their targets and
tests. Removing `lsp/` also disables the benchmark, which uses LSP services.
]==])
_cursiv_new_emit([==[example/hello@CURSIV_NEW_EXT@]==] [==[// The people this document knows about.
person Alice
person Bob

/* And the greetings for them. Each name is a cross reference: rename Alice
   above and this one follows, because the linker resolved it. */
Hello Alice!
Hello Bob!
]==])
_cursiv_new_emit([==[src/@CURSIV_NEW_ID@/bench/main.cpp]==] [==[#include "cursiv/bench/LanguageBench.hpp"
#include "@CURSIV_NEW_ID@/lsp/LspModule.hpp"

int main(int argc, char** argv) {
  return cursiv::bench::run_language<
      @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedServices,
      @CURSIV_NEW_ID@::register_lsp_services>(argc, argv);
}
]==])
_cursiv_new_emit([==[src/@CURSIV_NEW_ID@/cli/main.cpp]==] [==[// The @CURSIV_NEW_ID@ command line: parse one file, link it, validate it, and
// answer on the exit code -- 0 clean, 1 diagnostics, 2 misuse, 3 fatal.

#include "cursiv/cli/cli.hpp"
#include "cursiv/core/services/CoreLanguage.hpp"
#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"

// A language is an installer: the three type arguments are all that
// separates one from another, so `run_cli` itself is compiled once for
// every language there is and no grammar reaches it.
constexpr auto kRegister =
    &cursiv::register_language<@CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@CoreServices,
                           @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedCoreServices,
                           @CURSIV_NEW_ID@::install_core_module>;

int main(int argc, char** argv) {
  return cursiv::run_cli<@CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedCoreServices>(
      argc, argv, kRegister);
}
]==])
_cursiv_new_emit([==[src/@CURSIV_NEW_ID@/core/@CURSIV_NEW_CLASS@Grammar.hpp]==] [==[// The @CURSIV_NEW_ID@ language: its grammar. A cursiv grammar is a C++ type --
// there is no grammar file to parse, and no generator reads this header at
// build time except the one compiled against it.
#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "@CURSIV_NEW_ID@/core/ast.hpp"

// A hello-world language, and every piece of it is a piece you will reuse:
//
//     // people, and greetings for them
//     person Alice
//     person Bob
//
//     Hello Alice!
//
// The four kinds of member:
//   * `terminal<...>`    a token the lexer produces; `as<T>(...)` around
//                         its body converts its text (a number, say)
//   * `rule<...>`        a parser rule, its AST class deduced from the
//                         members it writes -- nothing is annotated
//   * `entry`            the rule a document starts at
//   * `ignored`/`hidden` trivia: `ignored` is dropped, `hidden` stays in the
//                        tree, which is what lets a comment attach to what
//                        follows it
//
// `ref<R>` calls the rule or terminal `R`. The directives build the tree:
//   * `create<T>`         make a T here, even if nothing else writes to it
//   * `assign<&T::m>(e)`  parse `e`, store it in `m` (once)
//   * `append<&T::m>(e)`  parse `e`, push it onto the list `m`
//   * `flag<&T::m>(e)`    parse `e`, then set the bool `m` to true
//   * `nest<&T::m>(e)`    wrap the node built so far in a new T, which
//                         holds it in `m`
//
// One trap: rules that reduce in a cycle without reading text are refused by
// name -- `A -> B` with `B -> A`, or a rule that reaches itself past symbols
// deriving nothing. Make one step of the cycle match at least one token.

namespace @CURSIV_NEW_ID@ {

struct @CURSIV_NEW_CLASS@ : cursiv::GrammarSpec {
  // `s` is the whitespace class, `w` the word class, `cr<"...">` a character
  // range written as in a regular expression.
  struct WS : terminal<some(s)> {};
  struct SL_COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ML_COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};

  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};

  // `kw` is a keyword (a whole word, never a prefix of a longer one); `ikw` is
  // its case-insensitive twin, and `lit` is raw text with no word boundary.
  struct Person : rule<kw<"person"> + assign<&ast::Person::name>(ref<ID>)> {};

  // `person` is a `reference<Person>` in the AST, so this reads a name and
  // the linker resolves it after the parse: the default scope makes every
  // named node visible inside its container.
  struct Greeting
      : rule<kw<"Hello"> + assign<&ast::Greeting::person>(ref<ID>) + lit<"!">> {
  };

  struct Model : rule<create<ast::Model>
                      + some(append<&ast::Model::persons>(ref<Person>)
                             | append<&ast::Model::greetings>(ref<Greeting>))> {
  };

  using entry = Model;
  using ignored = WS;
  using hidden = list<SL_COMMENT, ML_COMMENT>;
};

}  // namespace @CURSIV_NEW_ID@
]==])
_cursiv_new_emit([==[src/@CURSIV_NEW_ID@/core/CoreModule.cpp]==] [==[// What this language installs over the generated module: what its author
// writes. Everything the grammar derives -- parser, reflection, metadata -- is
// in the generated one, and this file is the only place the two meet.

#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"
#include "@CURSIV_NEW_ID@/core/validation/@CURSIV_NEW_CLASS@Validator.hpp"
#include "@CURSIV_NEW_ID@/generated/@CURSIV_NEW_CLASS@/Module.hpp"

namespace @CURSIV_NEW_ID@ {

void install_core_module(@CURSIV_NEW_CLASS@CoreServices& services) {
  @CURSIV_NEW_ID@::generated::@CURSIV_NEW_CLASS@::install_generated_module(services);
  register_validation_checks(services);
}

}  // namespace @CURSIV_NEW_ID@
]==])
_cursiv_new_emit([==[src/@CURSIV_NEW_ID@/core/CoreServices.hpp]==] [==[// What this language owns beyond what every language owns, and the module
// that fills its container.
#pragma once

#include <memory>

#include "cursiv/core/services/CoreServicesFor.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "@CURSIV_NEW_ID@/core/validation/@CURSIV_NEW_CLASS@Validator.hpp"

// Two containers per language: the shared one holds what is workspace-wide
// (the document store, the index), the other holds one language's
// services. Add a service of your own here, filled in `install_core_module`.

namespace @CURSIV_NEW_ID@ {

struct @CURSIV_NEW_CLASS@SharedCoreServices : virtual cursiv::SharedCoreServices {};

struct @CURSIV_NEW_CLASS@CoreServices
    : cursiv::CoreServicesFor<@CURSIV_NEW_CLASS@SharedCoreServices> {
  std::unique_ptr<const @CURSIV_NEW_CLASS@Validator> validator;

  explicit @CURSIV_NEW_CLASS@CoreServices(
      @CURSIV_NEW_CLASS@SharedCoreServices& sharedServices)
      : cursiv::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}

  // One row per service, for whoever asks what a container holds: the
  // language's own rows come after the defaults.
  void visitServices(
      cursiv::function_ref<void(const cursiv::ServiceSlot&)> yield) const override {
    cursiv::CoreServices::visitServices(yield);
    yield({"validator", true, validator != nullptr});
  }
};

void install_core_module(@CURSIV_NEW_CLASS@CoreServices& services);

}  // namespace @CURSIV_NEW_ID@
]==])
_cursiv_new_emit([==[src/@CURSIV_NEW_ID@/core/ast.hpp]==] [==[// The @CURSIV_NEW_ID@ language: its AST. Plain structs -- the grammar names
// their members, and the generated builder fills them.
#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

// Every class derives `cursiv::AstNode` through `cursiv::Extends`, which
// brings the field vocabulary into scope -- `vector`, `string_view` and
// `reference` below are the base's aliases, not std names spelled by hand.

namespace @CURSIV_NEW_ID@::ast {

struct Person : cursiv::Extends<cursiv::AstNode> {
  string_view name;
};

struct Greeting : cursiv::Extends<cursiv::AstNode> {
  reference<Person> person;
};

struct Model : cursiv::Extends<cursiv::AstNode> {
  vector<Person*> persons;
  vector<Greeting*> greetings;
};

}  // namespace @CURSIV_NEW_ID@::ast
]==])
_cursiv_new_emit([==[src/@CURSIV_NEW_ID@/core/validation/@CURSIV_NEW_CLASS@Validator.cpp]==] [==[#include "@CURSIV_NEW_ID@/core/validation/@CURSIV_NEW_CLASS@Validator.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/core/validation/ValidationRegistry.hpp"
#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"

namespace @CURSIV_NEW_ID@ {

// `.property<&T::m>()` is what puts the squiggle under the name instead of
// under the whole declaration: without it the range is the node's.
void @CURSIV_NEW_CLASS@Validator::checkPersonStartsWithCapital(
    const ast::Person& person, cursiv::ValidationAcceptor& accept) const {
  if (!person.name.empty() && person.name.front() >= 'a' &&
      person.name.front() <= 'z')
    accept.warning(person, "Person name should start with a capital.")
        .property<&ast::Person::name>();
}

void @CURSIV_NEW_CLASS@Validator::checkUniquePersons(
    const ast::Model& model, cursiv::ValidationAcceptor& accept) const {
  std::unordered_map<std::string_view, std::vector<const ast::Person*>> names;
  for (const ast::Person* const person : model.persons)
    if (person != nullptr) names[person->name].push_back(person);

  for (const auto& [name, people] : names) {
    if (people.size() <= 1) continue;
    const std::string message = "Duplicate person name: " + std::string(name);
    for (const ast::Person* const person : people)
      accept.error(*person, message).property<&ast::Person::name>();
  }
}

// A check is registered against the AST class it judges: the validator walks
// the tree once and calls each check on the nodes of its own type.
void register_validation_checks(@CURSIV_NEW_CLASS@CoreServices& services) {
  using V = @CURSIV_NEW_CLASS@Validator;
  services.validator = std::make_unique<const V>(services);
  const V& validator = *services.validator;
  services.validationRegistry->registerChecks(
      {cursiv::ValidationRegistry::makeValidationCheck<
           &V::checkPersonStartsWithCapital>(validator),
       cursiv::ValidationRegistry::makeValidationCheck<&V::checkUniquePersons>(
           validator)});
}

}  // namespace @CURSIV_NEW_ID@
]==])
_cursiv_new_emit([==[src/@CURSIV_NEW_ID@/core/validation/@CURSIV_NEW_CLASS@Validator.hpp]==] [==[// The checks of this language: what the grammar accepts but the language does
// not. Two here -- a naming convention, and the one fact no single node can see.
#pragma once

#include "cursiv/core/services/DefaultCoreService.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "@CURSIV_NEW_ID@/core/ast.hpp"

namespace @CURSIV_NEW_ID@ {

class @CURSIV_NEW_CLASS@Validator final : public cursiv::DefaultCoreService {
 public:
  using cursiv::DefaultCoreService::DefaultCoreService;

  void checkPersonStartsWithCapital(const ast::Person& person,
                                    cursiv::ValidationAcceptor& accept) const;
  void checkUniquePersons(const ast::Model& model,
                          cursiv::ValidationAcceptor& accept) const;
};

// Declared here rather than in the module, so a check and its registration are
// read together. `CoreServices.hpp` is the one that includes this header.
struct @CURSIV_NEW_CLASS@CoreServices;
void register_validation_checks(@CURSIV_NEW_CLASS@CoreServices& services);

}  // namespace @CURSIV_NEW_ID@
]==])
_cursiv_new_emit([==[src/@CURSIV_NEW_ID@/lsp/LspModule.cpp]==] [==[#include "@CURSIV_NEW_ID@/lsp/LspModule.hpp"

#include <memory>
#include <utility>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"

namespace @CURSIV_NEW_ID@ {

void install_lsp_module(@CURSIV_NEW_CLASS@Services& services) {
  cursiv::install_default_lsp_services(services);
}

// The three storeys in order: the framework's defaults, the language's
// core module, then its LSP module. The container is one object -- both
// bases derive `CoreServices` virtually -- so the providers read what the
// core module wired.
bool register_lsp_services(cursiv::SharedCoreServices& sharedServices) {
  auto& shared =
      dynamic_cast<@CURSIV_NEW_CLASS@SharedServices&>(sharedServices);
  auto services = std::make_unique<@CURSIV_NEW_CLASS@Services>(shared);
  cursiv::install_default_core_services(*services);
  install_core_module(*services);
  install_lsp_module(*services);
  shared.serviceRegistry->registerServices(std::move(services));
  return true;
}

}  // namespace @CURSIV_NEW_ID@
]==])
_cursiv_new_emit([==[src/@CURSIV_NEW_ID@/lsp/LspModule.hpp]==] [==[#pragma once

#include "cursiv/core/services/SharedCoreServices.hpp"
#include "@CURSIV_NEW_ID@/lsp/LspServices.hpp"

namespace @CURSIV_NEW_ID@ {

// The LSP storey of this language: the framework's defaults, then whatever the
// language overrides -- a formatter, a hover provider, a semantic token
// provider. Add it here.
void install_lsp_module(@CURSIV_NEW_CLASS@Services& services);

// Files this language into a workspace the server owns. What `main` hands to
// the runner, which has already installed the workspace-wide services.
bool register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace @CURSIV_NEW_ID@
]==])
_cursiv_new_emit([==[src/@CURSIV_NEW_ID@/lsp/LspServices.hpp]==] [==[// The same language one storey up. Both containers derive `CoreServices`
// virtually, so this holds one of it and the core module wires the same
// object the LSP providers read.
#pragma once

#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"

namespace @CURSIV_NEW_ID@ {

struct @CURSIV_NEW_CLASS@SharedServices : @CURSIV_NEW_CLASS@SharedCoreServices,
                                           cursiv::SharedLspServices {};

struct @CURSIV_NEW_CLASS@Services final : @CURSIV_NEW_CLASS@CoreServices,
                                           cursiv::LspServices {
  explicit @CURSIV_NEW_CLASS@Services(
      @CURSIV_NEW_CLASS@SharedServices& sharedServices)
      : cursiv::CoreServices(sharedServices),
        @CURSIV_NEW_CLASS@CoreServices(sharedServices),
        cursiv::LspServices(sharedServices) {}

  // One emission per storey through the diamond: the language rows through the
  // core side, the LSP rows alone.
  void visitServices(
      cursiv::function_ref<void(const cursiv::ServiceSlot&)> yield) const override {
    @CURSIV_NEW_CLASS@CoreServices::visitServices(yield);
    cursiv::LspServices::visitOwnServices(yield);
  }
};

}  // namespace @CURSIV_NEW_ID@
]==])
_cursiv_new_emit([==[src/@CURSIV_NEW_ID@/lsp/main.cpp]==] [==[// The @CURSIV_NEW_ID@ language server: one language, the standard transport
// (stdio -- the editor spawns this binary and speaks the protocol to it).

#include "cursiv/lsp/runtime/LanguageServerMain.hpp"
#include "@CURSIV_NEW_ID@/lsp/LspModule.hpp"

int main(int argc, char** argv) {
  return cursiv::run_language_server_main<
      @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedServices>(
      argc, argv, "@CURSIV_NEW_ID@-lsp", &@CURSIV_NEW_ID@::register_lsp_services);
}
]==])
_cursiv_new_emit([==[src/CMakeLists.txt]==] [==[# Generate the parser and its language module.
cursiv_generate_language(ID @CURSIV_NEW_ID@
    GRAMMAR    @CURSIV_NEW_ID@/core/@CURSIV_NEW_CLASS@Grammar.hpp
    NAMESPACE  @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@
    EXTENSIONS @CURSIV_NEW_EXT@
    OUT        @CURSIV_NEW_ID@/generated)

# Add custom services and validation over the generated module.
file(GLOB_RECURSE _core CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/@CURSIV_NEW_ID@/core/*.cpp)
add_library(@CURSIV_NEW_ID@Core ${_core})
target_link_libraries(@CURSIV_NEW_ID@Core PUBLIC @CURSIV_NEW_ID@Generated)

file(GLOB_RECURSE _cli CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/@CURSIV_NEW_ID@/cli/*.cpp)
if(_cli)
  add_executable(@CURSIV_NEW_ID@_cli ${_cli})
  target_link_libraries(@CURSIV_NEW_ID@_cli
                        PRIVATE @CURSIV_NEW_ID@Core cursiv::cli)
endif()

if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/@CURSIV_NEW_ID@/lsp")
  file(GLOB_RECURSE _lsp CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/@CURSIV_NEW_ID@/lsp/*.cpp)
  list(REMOVE_ITEM _lsp ${CMAKE_CURRENT_SOURCE_DIR}/@CURSIV_NEW_ID@/lsp/main.cpp)
  add_library(@CURSIV_NEW_ID@Lsp ${_lsp})
  target_link_libraries(@CURSIV_NEW_ID@Lsp PUBLIC @CURSIV_NEW_ID@Core cursiv::lsp)
  add_executable(@CURSIV_NEW_ID@_lsp @CURSIV_NEW_ID@/lsp/main.cpp)
  target_link_libraries(@CURSIV_NEW_ID@_lsp PRIVATE @CURSIV_NEW_ID@Lsp)
endif()

if(TARGET @CURSIV_NEW_ID@Lsp AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/@CURSIV_NEW_ID@/bench/main.cpp")
  cursiv_add_benchmark(@CURSIV_NEW_ID@
      SOURCES @CURSIV_NEW_ID@/bench/main.cpp)
endif()

cursiv_package_language(@CURSIV_NEW_ID@)
]==])
_cursiv_new_emit([==[tests/CMakeLists.txt]==] [==[# Parse the sample and check its syntax tree.
add_executable(@CURSIV_NEW_ID@_grammar_test grammar_test.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  target_link_options(@CURSIV_NEW_ID@_grammar_test PRIVATE --no-wasm-opt)
endif()
target_link_libraries(@CURSIV_NEW_ID@_grammar_test PRIVATE @CURSIV_NEW_ID@Core)
target_compile_definitions(@CURSIV_NEW_ID@_grammar_test PRIVATE
    CURSIV_SAMPLE="${PROJECT_SOURCE_DIR}/example/hello@CURSIV_NEW_EXT@")
add_test(NAME @CURSIV_NEW_ID@_grammar_test COMMAND @CURSIV_NEW_ID@_grammar_test)

# Run the CLI against the same sample.
if(TARGET @CURSIV_NEW_ID@_cli)
  add_test(NAME @CURSIV_NEW_ID@_cli_test COMMAND @CURSIV_NEW_ID@_cli
           ${PROJECT_SOURCE_DIR}/example/hello@CURSIV_NEW_EXT@)
endif()
]==])
_cursiv_new_emit([==[tests/grammar_test.cpp]==] [==[// The smoke test of the language: build the container the way a workspace
// does, read the sample, and check the tree. No framework -- the exit code is
// the verdict, and ctest reads it.

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/services/CoreLanguage.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"
#include "@CURSIV_NEW_ID@/core/ast.hpp"

namespace {

int fail(const char* what) {
  std::fprintf(stderr, "@CURSIV_NEW_ID@: %s\n", what);
  return 1;
}

}  // namespace

int main() {
  std::ifstream file(CURSIV_SAMPLE);
  if (!file) return fail("the sample document is not readable");
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

  // The whole language in one call: the defaults, then `install_core_module`.
  const auto language = cursiv::create_language<
      @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@CoreServices,
      @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedCoreServices,
      @CURSIV_NEW_ID@::install_core_module>();

  const cursiv::ParseResult result = language.language.parser->parse(text);
  if (!result || !result.diagnostics.empty())
    return fail("the sample does not parse cleanly");

  const auto* const root =
      cursiv::ast_cast<@CURSIV_NEW_ID@::ast::Model>(result.value);
  if (root == nullptr) return fail("the parse built no model");

  const auto& model = *root;
  if (model.persons.size() != 2 || model.greetings.size() != 2)
    return fail("the model does not hold what the sample declares");

  std::puts("@CURSIV_NEW_ID@: the sample parses");
  return 0;
}
]==])
_cursiv_new_emit([==[vscode/esbuild.mjs]==] [==[import { rmSync } from 'node:fs';
import esbuild from 'esbuild';

const production = process.argv.includes('--production');
const watch = process.argv.includes('--watch');

if (!watch) {
  rmSync('out', { recursive: true, force: true });
}

// Only `vscode` is external -- the editor injects it at runtime; everything
// else is bundled, so the packaged .vsix is self-contained.
const context = await esbuild.context({
  entryPoints: ['src/extension.ts'],
  bundle: true,
  format: 'cjs',
  platform: 'node',
  target: 'node18', // VS Code 1.86 supports native servers; WASI needs 1.101.
  outfile: 'out/extension.js',
  external: ['vscode'],
  sourcemap: !production,
  minify: production,
  logLevel: 'info',
});

if (watch) {
  await context.watch();
} else {
  await context.rebuild();
  await context.dispose();
}
]==])
_cursiv_new_emit([==[vscode/language-configuration.json]==] [==[{
  "comments": {
    "lineComment": "//",
    "blockComment": [
      "/*",
      "*/"
    ]
  },
  "autoClosingPairs": [
    {
      "open": "/*",
      "close": " */",
      "notIn": [
        "string"
      ]
    }
  ]
}
]==])
_cursiv_new_emit([==[vscode/package.json]==] [==[{
  "name": "@CURSIV_NEW_ID@",
  "displayName": "@CURSIV_NEW_CLASS@",
  "description": "The @CURSIV_NEW_ID@ language, parsed by cursiv",
  "version": "0.1.0",
  "publisher": "example",
  "private": true,
  "license": "MIT",
  "engines": {
    "vscode": "^1.86.0"
  },
  "categories": [
    "Programming Languages"
  ],
  "activationEvents": [
    "onLanguage:@CURSIV_NEW_ID@"
  ],
  "main": "./out/extension.js",
  "contributes": {
    "languages": [
      {
        "id": "@CURSIV_NEW_ID@",
        "aliases": [
          "@CURSIV_NEW_CLASS@"
        ],
        "extensions": [
          "@CURSIV_NEW_EXT@"
        ],
        "configuration": "./language-configuration.json"
      }
    ],
    "grammars": [
      {
        "language": "@CURSIV_NEW_ID@",
        "scopeName": "source.@CURSIV_NEW_ID@",
        "path": "./syntaxes/@CURSIV_NEW_ID@.tmLanguage.json"
      }
    ],
    "configuration": {
      "title": "@CURSIV_NEW_CLASS@",
      "properties": {
        "@CURSIV_NEW_ID@.server.path": {
          "type": "string",
          "default": "",
          "description": "Absolute path to the @CURSIV_NEW_ID@_lsp executable. Empty means the packaged copy, then ../build/."
        }
      }
    },
    "mcpServerDefinitionProviders": [
      {
        "id": "cursiv.@CURSIV_NEW_ID@.mcp",
        "label": "cursiv @CURSIV_NEW_CLASS@"
      }
    ]
  },
  "scripts": {
    "compile": "node esbuild.mjs",
    "watch": "node esbuild.mjs --watch",
    "bundle": "node esbuild.mjs --production",
    "typecheck": "tsc -p tsconfig.json",
    "vscode:prepublish": "npm run bundle",
    "package": "vsce package --no-dependencies --allow-missing-repository"
  },
  "files": [
    "package.json",
    "out",
    "{bin,server}/**",
    "language-configuration.json",
    "syntaxes"
  ],
  "dependencies": {
    "vscode-languageclient": "^9.0.1"
  },
  "devDependencies": {
    "@types/node": "^20.19.43",
    "@types/vscode": "~1.86.0",
    "esbuild": "^0.28.2",
    "typescript": "^7.0.2",
    "@vscode/vsce": "^3.9.2"
  },
  "overrides": {
    "@vscode/vsce": {
      "@azure/identity": "4.13.0"
    },
    "@azure/msal-node": {
      "uuid": "^11.1.1"
    }
  }
}
]==])
_cursiv_new_emit([==[vscode/src/extension.ts]==] [==[// The @CURSIV_NEW_ID@ extension: it spawns `@CURSIV_NEW_ID@_lsp` and speaks
// the protocol to it over stdio.

import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import * as vscode from 'vscode';
import {
    LanguageClient,
    TransportKind,
    type Executable,
    type LanguageClientOptions,
    type ServerOptions
} from 'vscode-languageclient/node';
import { startMcpBridge } from './mcp';

const ID = '@CURSIV_NEW_ID@';
const SERVER = '@CURSIV_NEW_ID@_lsp';

function executableName(): string {
    return process.platform === 'win32' ? `${SERVER}.exe` : SERVER;
}

// A native binary is its own command; a wasm module is a file Node runs, and
// the extension host IS Node, so the wasm route needs no dependency.
interface ServerCommand {
    command: string;
    args: string[];
    env?: Record<string, string>;
}

// A wasm module opens with `\0asm` and a host binary does not. The cross build
// names its output exactly like the native one, so the file itself is the only
// thing that says which of the two runners it needs.
function isWasm(file: string): boolean {
    let handle: number | undefined;
    try {
        handle = fs.openSync(file, 'r');
        const head = Buffer.alloc(4);
        return fs.readSync(handle, head, 0, 4, 0) === 4 &&
            head.toString('latin1') === '\0asm';
    } catch {
        return false;
    } finally {
        if (handle !== undefined) fs.closeSync(handle);
    }
}

// What the active CMake preset just built: `launch.json` hands its binary
// directory over, so F5 runs the artefact the editor is configured to build
// rather than whatever a previous cross left under `server/`.
function presetCommand(context: vscode.ExtensionContext): ServerCommand | undefined {
    const directory = process.env.CURSIV_SERVER_DIR?.trim();
    if (!directory) return undefined;
    const built = path.join(directory, executableName());
    if (!fs.existsSync(built)) return undefined;
    if (!isWasm(built)) return { command: built, args: [] };
    return wasmRun(built, path.join(context.extensionPath, 'server', 'wasi-run.mjs'));
}

// The wasm server, preferred when it is there: one artefact for every platform,
// and the sandbox comes with it -- the module reads only what the runner
// preopens. `cursiv_package_language` refreshes both files under `server/` on a
// threaded cross build, and a packaged .vsix carries them.
function wasmCommand(context: vscode.ExtensionContext): ServerCommand | undefined {
    return wasmRun(path.join(context.extensionPath, 'server', `${SERVER}.wasm`),
                   path.join(context.extensionPath, 'server', 'wasi-run.mjs'));
}

function wasmRun(module: string, runner: string): ServerCommand | undefined {
    if (!fs.existsSync(module) || !fs.existsSync(runner)) {
        return undefined;
    }
    return {
        command: process.execPath,
        args: ['--no-warnings', runner, module],
        // WASI cannot count the host's cores: `hardware_concurrency()` answers
        // one there, so the pool would never be built. The host knows.
        env: { CURSIV_WORKERS: String(Math.max(0, os.availableParallelism() - 1)) },
    };
}

// The extension lives at `<project>/vscode`, so `../build/src/` is where
// CMake put the native server.
function nativeCommand(context: vscode.ExtensionContext): ServerCommand | undefined {
    const name = executableName();
    for (const candidate of [
        path.join(context.extensionPath, 'bin', name),
        path.join(context.extensionPath, '..', 'build', 'src', name)
    ]) {
        if (fs.existsSync(candidate)) {
            return { command: path.resolve(candidate), args: [] };
        }
    }
    return undefined;
}

let client: LanguageClient | undefined;

export async function activate(context: vscode.ExtensionContext): Promise<void> {
    const configured = vscode.workspace.getConfiguration()
        .get<string>(`${ID}.server.path`)?.trim();
    const server: ServerCommand | undefined = configured
        ? { command: configured, args: [] }
        : presetCommand(context) ?? wasmCommand(context) ?? nativeCommand(context);
    if (!server) {
        void vscode.window.showErrorMessage(
            `Cannot find ${SERVER}. Build it with ` +
            `\`cmake --build --preset wasi-release --target ${SERVER}\`, or set ` +
            `\`${ID}.server.path\`.`
        );
        return;
    }

    const outputChannel = vscode.window.createOutputChannel(ID);
    context.subscriptions.push(outputChannel);
    outputChannel.appendLine(`server: ${server.command} ${server.args.join(' ')}`);

    // A native server has no --inspect to add, so the debug configuration is
    // the run configuration.
    const executable: Executable = {
        command: server.command,
        args: server.args,
        transport: TransportKind.stdio,
        ...(server.env ? { options: { env: { ...process.env, ...server.env } } } : {})
    };
    const serverOptions: ServerOptions = { run: executable, debug: executable };
    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: ID }],
        outputChannel
    };

    client = new LanguageClient(ID, '@CURSIV_NEW_CLASS@', serverOptions, clientOptions);
    await client.start();
    // The MCP door onto the same session: a model connected here reads what
    // the editor holds unsaved.
    context.subscriptions.push(startMcpBridge(ID, '@CURSIV_NEW_CLASS@', client, outputChannel));
}

export async function deactivate(): Promise<void> {
    const running = client;
    client = undefined;
    await running?.stop();
}
]==])
_cursiv_new_emit([==[vscode/src/mcp.ts]==] [==[// The MCP bridge: the endpoint lives in the language server and rides the LSP
// wire as `cursiv/mcp`; this hosts the standard Streamable HTTP transport the
// module itself cannot open under WASI. Localhost only, one bearer token per
// session, no SSE -- the endpoint sends nothing on its own.

import * as crypto from 'node:crypto';
import * as http from 'node:http';
import * as vscode from 'vscode';
import type { LanguageClient } from 'vscode-languageclient/node';

const MCP_METHOD = 'cursiv/mcp';

// Announcing the server to the editor is VS Code 1.101 API; serving it is not.
// Declared here rather than taken from `@types/vscode` so the extension still
// compiles, installs and serves against the older engine it asks for.
interface McpRegistry {
    lm?: {
        registerMcpServerDefinitionProvider?(
            id: string,
            provider: { provideMcpServerDefinitions: () => unknown[] },
        ): vscode.Disposable;
    };
    McpHttpServerDefinition?: new (
        label: string,
        uri: vscode.Uri,
        headers?: Record<string, string>,
    ) => unknown;
}

function isLocal(value: string | undefined): boolean {
    if (!value) {
        return true;
    }
    try {
        const host = new URL(value.includes('://') ? value : `http://${value}`).hostname;
        return host === '127.0.0.1' || host === 'localhost' || host === '[::1]';
    } catch {
        return false;
    }
}

export function startMcpBridge(
    key: string,
    label: string,
    client: LanguageClient,
    outputChannel: vscode.OutputChannel,
): vscode.Disposable {
    const token = crypto.randomUUID();

    const server = http.createServer((request, response) => {
        const answer = (status: number, body?: unknown): void => {
            if (body === undefined) {
                response.writeHead(status).end();
                return;
            }
            response
                .writeHead(status, { 'Content-Type': 'application/json' })
                .end(JSON.stringify(body));
        };
        const refusal = (status: number, code: number, message: string): void =>
            answer(status, { jsonrpc: '2.0', id: null, error: { code, message } });
        // Anti DNS-rebinding: a browser page reaching for the port names its
        // origin, and no browser origin has business here.
        if (!isLocal(request.headers.host) || !isLocal(request.headers.origin)) {
            return refusal(403, -32000, 'forbidden origin');
        }
        if (request.headers.authorization !== `Bearer ${token}`) {
            return refusal(401, -32000, 'missing or wrong bearer token');
        }
        if (request.method !== 'POST') {
            return answer(405);
        }
        let body = '';
        request.setEncoding('utf8');
        request.on('data', (chunk: string) => {
            body += chunk;
            if (body.length > 16 * 1024 * 1024) {
                request.destroy();
            }
        });
        request.on('end', () => {
            let message: unknown;
            try {
                message = JSON.parse(body);
            } catch {
                return refusal(400, -32700, 'parse error');
            }
            const id = (message as { id?: unknown })?.id ?? null;
            client.sendRequest(MCP_METHOD, message).then(
                // A notification answers nothing, which the transport spells 202.
                (result) => (result === null ? answer(202) : answer(200, result)),
                (error) => answer(500, {
                    jsonrpc: '2.0',
                    id,
                    error: { code: -32603, message: String((error as Error)?.message ?? error) },
                }),
            );
        });
    });

    const registration = new Promise<vscode.Disposable | undefined>((resolve) => {
        server.listen(0, '127.0.0.1', () => {
            const address = server.address();
            if (address === null || typeof address === 'string') {
                return resolve(undefined);
            }
            const url = `http://127.0.0.1:${address.port}/mcp`;
            // The line an external client copies:
            //   claude mcp add --transport http <name> <url> --header "Authorization: Bearer <token>"
            outputChannel.appendLine(`mcp: ${url} (Authorization: Bearer ${token})`);
            const api = vscode as unknown as McpRegistry;
            const register = api.lm?.registerMcpServerDefinitionProvider;
            const HttpDefinition = api.McpHttpServerDefinition;
            // Before 1.101 the editor has no MCP registry, so the line above is
            // the whole wiring: the server serves, a client is pointed at it.
            if (register === undefined || HttpDefinition === undefined) {
                return resolve(undefined);
            }
            resolve(register.call(api.lm, `cursiv.${key}.mcp`, {
                provideMcpServerDefinitions: () => [
                    new HttpDefinition(`cursiv ${label}`, vscode.Uri.parse(url), {
                        Authorization: `Bearer ${token}`,
                    }),
                ],
            }));
        });
    });

    return new vscode.Disposable(() => {
        void registration.then((r) => r?.dispose());
        server.close();
    });
}
]==])
_cursiv_new_emit([==[vscode/syntaxes/@CURSIV_NEW_ID@.tmLanguage.json]==] [==[{
  "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
  "name": "@CURSIV_NEW_CLASS@",
  "scopeName": "source.@CURSIV_NEW_ID@",
  "patterns": [
    {
      "include": "#comments"
    },
    {
      "include": "#keywords"
    },
    {
      "include": "#numbers"
    }
  ],
  "repository": {
    "comments": {
      "patterns": [
        {
          "name": "comment.line.double-slash.@CURSIV_NEW_ID@",
          "match": "//.*$"
        },
        {
          "name": "comment.block.@CURSIV_NEW_ID@",
          "begin": "/\\*",
          "end": "\\*/"
        }
      ]
    },
    "keywords": {
      "name": "keyword.control.@CURSIV_NEW_ID@",
      "match": "\\b(person|age|greet)\\b"
    },
    "numbers": {
      "name": "constant.numeric.@CURSIV_NEW_ID@",
      "match": "\\b[0-9]+\\b"
    }
  }
}
]==])
_cursiv_new_emit([==[vscode/tsconfig.json]==] [==[{
  "compilerOptions": {
    "module": "commonjs",
    "target": "ES2020",
    "lib": [
      "ES2020"
    ],
    "rootDir": "./src",
    "outDir": "./out",
    "noEmit": true,
    "sourceMap": true,
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "types": [
      "node",
      "vscode"
    ]
  },
  "include": [
    "src/**/*.ts"
  ]
}
]==])
_cursiv_new_emit([==[CMakePresets.json]==] [==[{
  "version": 6,
  "configurePresets": [
    {
      "name": "gcc-release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_CXX_COMPILER": "g++"
      }
    },
    {
      "name": "gcc-debug",
      "inherits": "gcc-release",
      "binaryDir": "${sourceDir}/build-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    },
    {
      "name": "clang-release",
      "inherits": "gcc-release",
      "binaryDir": "${sourceDir}/build-clang",
      "cacheVariables": {
        "CMAKE_CXX_COMPILER": "clang++"
      }
    },
    {
      "name": "clang-debug",
      "inherits": "clang-release",
      "binaryDir": "${sourceDir}/build-clang-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    },
    {
      "name": "wasi-release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build-wasi",
      "toolchainFile": "${sourceDir}/cmake/wasi.cmake",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CURSIV_WASI_THREADS": "ON"
      }
    },
    {
      "name": "wasi-debug",
      "inherits": "wasi-release",
      "binaryDir": "${sourceDir}/build-wasi-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    }
  ],
  "buildPresets": [
    {"name": "gcc-release", "configurePreset": "gcc-release"},
    {"name": "gcc-debug", "configurePreset": "gcc-debug"},
    {"name": "clang-release", "configurePreset": "clang-release"},
    {"name": "clang-debug", "configurePreset": "clang-debug"},
    {"name": "wasi-release", "configurePreset": "wasi-release"},
    {"name": "wasi-debug", "configurePreset": "wasi-debug"}
  ],
  "testPresets": [
    {"name": "gcc-release", "configurePreset": "gcc-release"},
    {"name": "gcc-debug", "configurePreset": "gcc-debug"},
    {"name": "clang-release", "configurePreset": "clang-release"},
    {"name": "clang-debug", "configurePreset": "clang-debug"},
    {"name": "wasi-release", "configurePreset": "wasi-release"},
    {"name": "wasi-debug", "configurePreset": "wasi-debug"}
  ]
}
]==])
_cursiv_new_emit([==[cmake/wasi.cmake]==] [==[# The WASI toolchain used by the wasi-debug and wasi-release presets.
# wasi-sdk 34 is the pinned release. Cursiv throws: `-fwasm-exceptions` at
# both compile and link selects the `eh` half of its dual `eh`/`noeh` sysroot.
set(CMAKE_SYSTEM_NAME WASI)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

if(NOT WASI_SDK)
  set(WASI_SDK $ENV{WASI_SDK})
endif()
# The conventional spots, so a preset works with no environment at all.
if(NOT WASI_SDK)
  foreach(_cursiv_candidate "$ENV{HOME}/wasi-sdk-34.0" "/opt/wasi-sdk")
    if(EXISTS "${_cursiv_candidate}/bin/clang++")
      set(WASI_SDK "${_cursiv_candidate}")
      break()
    endif()
  endforeach()
endif()
if(NOT WASI_SDK)
  message(FATAL_ERROR
      "set -DWASI_SDK=<wasi-sdk-34+ directory> (or install it at "
      "~/wasi-sdk-34.0 or /opt/wasi-sdk). The wasm target builds ONLY with "
      "the SDK's own clang -- gcc cannot emit wasm.")
endif()

# Carried into try_compile, which re-runs this file in a scope the cache
# does not reach: without this line CMake probes the compiler with no
# compiler set.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    WASI_SDK CURSIV_WASI_THREADS CURSIV_WASI_MAX_MEMORY CURSIV_WASI_STACK)

set(CMAKE_C_COMPILER   ${WASI_SDK}/bin/clang)
set(CMAKE_CXX_COMPILER ${WASI_SDK}/bin/clang++)
set(CMAKE_AR           ${WASI_SDK}/bin/llvm-ar)
set(CMAKE_RANLIB       ${WASI_SDK}/bin/llvm-ranlib)
set(CMAKE_SYSROOT      ${WASI_SDK}/share/wasi-sysroot)
# Threads are a different target, not a flag: `wasm32-wasip1-threads` has
# its own sysroot and libc++. It is the production target (vscode/node), so
# it is the default; OFF builds the threadless `wasm32-wasip1`, inline-only.
option(CURSIV_WASI_THREADS "Build for wasm32-wasip1-threads" ON)
if(CURSIV_WASI_THREADS)
  set(_cursiv_wasi_target wasm32-wasip1-threads)
else()
  set(_cursiv_wasi_target wasm32-wasip1)
endif()
set(CMAKE_C_COMPILER_TARGET   ${_cursiv_wasi_target})
set(CMAKE_CXX_COMPILER_TARGET ${_cursiv_wasi_target})

# `-mllvm -wasm-use-legacy-eh=false` selects the EH scheme the runtime still
# accepts; the _WASI_EMULATED_* defines are what lets `<csignal>` be included
# on a target with no signals.
set(_cursiv_wasi_emul "-D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS")
if(CURSIV_WASI_THREADS)
  # Define _REENTRANT when compiling. At link time the threaded target already
  # selects libpthread and shared memory; the memory must also be imported so
  # the runner can hand it to each worker.
  add_compile_options(-pthread)
  # A shared memory must declare its maximum at link time (wasm format). It is
  # an addressing cap, not a reservation: pages are committed as the heap
  # grows. Must be a multiple of 65536; 4 GiB is the wasm32 ceiling.
  # The 100,000-level LL stress test needs about 1 GiB with mimalloc.
  set(CURSIV_WASI_MAX_MEMORY 2147483648 CACHE STRING
      "Shared-memory maximum handed to the linker (bytes)")
  # Clang already selects shared memory for the threaded target.
  add_link_options("LINKER:--import-memory,--max-memory=${CURSIV_WASI_MAX_MEMORY}")
endif()
set(_cursiv_wasi_eh "-fwasm-exceptions -mllvm -wasm-use-legacy-eh=false")
set(CMAKE_C_FLAGS_INIT   "${_cursiv_wasi_eh} ${_cursiv_wasi_emul}")
set(CMAKE_CXX_FLAGS_INIT "${_cursiv_wasi_eh} ${_cursiv_wasi_emul}")
# The linker's default stack is 64 KiB, which recursive AST walks and large
# frames overrun: past it the stack pointer wraps into data and a store traps.
set(CURSIV_WASI_STACK 1048576 CACHE STRING
    "Stack reserved for a wasm executable's main thread (bytes)")
# Directory link options are regenerated when cache tunables change, without
# rewriting the user's CMAKE_EXE_LINKER_FLAGS.
add_link_options("LINKER:-z,stack-size=${CURSIV_WASI_STACK}")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-Wno-unused-command-line-argument -lunwind -lwasi-emulated-signal -lwasi-emulated-process-clocks")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# SDK Clang runs wasm-opt automatically for optimized links when it is on
# PATH, and preserves target_features for it. Do not add another pass here.
# The SDK's libraries contain DWARF even when our own sources use Release.
add_link_options("$<$<CONFIG:Release>:LINKER:--strip-debug>")

# The project resolves the runner after locating the Cursiv source tree.
find_program(CURSIV_NODE node)
]==])

_cursiv_new_finish()
