# =============================================================================
# GENERATED FILE -- DO NOT EDIT.
# Produced by cmake/GenerateScaffold.cmake from cmake/cursiv-new.cmake.in and
# templates/project/, CMakePresets.json, cmake/wasi.cmake,
# cmake/wasi-platform.cmake and tools/fetch-wasi-sdk.cmake. Edit those and
# regenerate:
#   cmake -DCURSIV_TAG=main -P cmake/GenerateScaffold.cmake
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
  message(STATUS "      cmake --build build --parallel")
  message(STATUS "      ./build/src/${CURSIV_NEW_ID}_cli example/hello${CURSIV_NEW_EXT}")
endfunction()

message(STATUS "Scaffolding ${NAME} (id=${CURSIV_NEW_ID}, ext=${EXT})")

_cursiv_new_emit([==[.clang-format]==] [==[# Google style. Long expressions break before their operators, so each `+`
# and `|` of a grammar rule starts a line.
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
_cursiv_new_emit([==[.gitignore]==] [==[# Build output, including the parser generated from the grammar.
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
      "detail": "Build @CURSIV_NEW_ID@_lsp with the active CMake preset, then compile the extension.",
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
# Cursiv is downloaded at the tag below. To build against a local copy of
# Cursiv instead, configure with `-DCURSIV_SOURCE_DIR=<path>`.
if(CURSIV_SOURCE_DIR)
  FetchContent_Declare(cursiv SOURCE_DIR ${CURSIV_SOURCE_DIR})
else()
  FetchContent_Declare(cursiv
      GIT_REPOSITORY https://github.com/ydaveluy/cursiv.git
      GIT_TAG        @CURSIV_NEW_TAG@)
endif()
FetchContent_MakeAvailable(cursiv)

# WebAssembly (WASI) builds run their tests with Node.js.
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
| Syntax | `src/core/@CURSIV_NEW_ID@/@CURSIV_NEW_CLASS@Grammar.hpp` |
| AST types | `src/core/@CURSIV_NEW_ID@/ast.hpp` |
| Validation rules | `src/core/@CURSIV_NEW_ID@/validation/` |
| Language services | `src/core/@CURSIV_NEW_ID@/CoreServices.hpp` and `CoreModule.cpp` |
| CLI behavior | `src/cli.cpp` |
| Editor features | `src/lsp/@CURSIV_NEW_ID@/LspModule.cpp` |
| Tests | `tests/grammar_test.cpp` |

Follow the [language tutorial](https://ydaveluy.github.io/cursiv/learn/walkthrough/)
to replace the sample with a complete language.

## Open the editor

Install Node.js, npm and the recommended VS Code extensions, including CMake
Tools and clangd. Native servers support VS Code 1.91 or later; WASI servers
require 1.101 or later. Open this project folder in VS Code, configure the
CMake build and press **F5**. The launch task builds the server and client,
then opens `example/` in a development window.

Native GCC/Clang presets run the native server. WASI presets run a WebAssembly
server through Node; `cmake -P tools/fetch-wasi-sdk.cmake` installs the SDK they
need. See
[Editor integration](https://ydaveluy.github.io/cursiv/learn/editor-integration/)
for customization and packaging.

## Measure performance

```bash
cmake --build build --target @CURSIV_NEW_ID@_bench
./build/src/@CURSIV_NEW_ID@_bench example/hello@CURSIV_NEW_EXT@ --iterations 5
```

The benchmark measures parsing, document builds and installed editor providers.
Use `--json bench.json` or `--csv bench.csv` to export results, and `--help` for
all options. Its entry point is `src/bench.cpp`.

## Optional components

The CLI, server, benchmark and VS Code extension are included by default.
Remove what you do not need: `src/cli.cpp`, `src/lsp.cpp` and `src/lsp/`,
`src/bench.cpp` or `vscode/`. CMake omits their targets and tests. Removing
`src/lsp/` also disables the benchmark, which uses LSP services.
]==])
_cursiv_new_emit([==[example/hello@CURSIV_NEW_EXT@]==] [==[// Two people.
person Alice
person Bob

/* A greeting for each. Each name refers to a person declared above: in the
   editor, renaming Alice also renames this use. */
Hello Alice!
Hello Bob!
]==])
_cursiv_new_emit([==[src/CMakeLists.txt]==] [==[# Core: the language itself. Generates the parser from the grammar, again when
# the grammar changes, then builds the services and checks under core/. Every
# other package links it.
cursiv_generate_language(ID @CURSIV_NEW_ID@
    GRAMMAR    @CURSIV_NEW_ID@/@CURSIV_NEW_CLASS@Grammar.hpp
    NAMESPACE  @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@
    EXTENSIONS @CURSIV_NEW_EXT@
    OUT        @CURSIV_NEW_ID@/generated)

file(GLOB_RECURSE _core CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
add_library(@CURSIV_NEW_ID@Core ${_core})
target_link_libraries(@CURSIV_NEW_ID@Core PUBLIC @CURSIV_NEW_ID@Generated)

# CLI: the command line. cli.cpp starts the executable; the code under cli/
# builds into a library that the tests can link. Delete cli.cpp to drop it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cli.cpp")
  add_executable(@CURSIV_NEW_ID@_cli cli.cpp)
  target_include_directories(@CURSIV_NEW_ID@_cli PRIVATE cli)
  target_link_libraries(@CURSIV_NEW_ID@_cli PRIVATE @CURSIV_NEW_ID@Core cursiv::cli)
  file(GLOB_RECURSE _cli CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/cli/*.cpp)
  if(_cli)
    add_library(@CURSIV_NEW_ID@Cli ${_cli})
    target_include_directories(@CURSIV_NEW_ID@Cli PUBLIC cli)
    target_link_libraries(@CURSIV_NEW_ID@Cli PUBLIC @CURSIV_NEW_ID@Core cursiv::cli)
    target_link_libraries(@CURSIV_NEW_ID@_cli PRIVATE @CURSIV_NEW_ID@Cli)
  endif()
endif()

# LSP: the language server. lsp.cpp starts the executable; the code under lsp/
# builds into a library that the tests and the benchmark can link. Delete lsp.cpp
# and lsp/ to drop it.
if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lsp")
  file(GLOB_RECURSE _lsp CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/lsp/*.cpp)
  add_library(@CURSIV_NEW_ID@Lsp ${_lsp})
  target_include_directories(@CURSIV_NEW_ID@Lsp PUBLIC lsp)
  target_link_libraries(@CURSIV_NEW_ID@Lsp PUBLIC @CURSIV_NEW_ID@Core cursiv::lsp)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lsp.cpp")
    add_executable(@CURSIV_NEW_ID@_lsp lsp.cpp)
    target_link_libraries(@CURSIV_NEW_ID@_lsp PRIVATE @CURSIV_NEW_ID@Lsp)
  endif()
endif()

# Benchmark: bench.cpp measures the language through the LSP library. Delete it
# to drop it.
if(TARGET @CURSIV_NEW_ID@Lsp AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bench.cpp")
  cursiv_add_benchmark(@CURSIV_NEW_ID@ SOURCES bench.cpp)
endif()

# VS Code: a WASI build copies the language server into vscode/server/.
cursiv_package_language(@CURSIV_NEW_ID@)
]==])
_cursiv_new_emit([==[src/bench.cpp]==] [==[// The @CURSIV_NEW_ID@ benchmark: it times parsing, document builds and editor
// features on the given files. Run it with `--help` to list the options.

#include "cursiv/LanguageBench.hpp"
#include "@CURSIV_NEW_ID@/LspModule.hpp"

int main(int argc, char** argv) {
  return cursiv::bench::run_language<
      @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedServices,
      @CURSIV_NEW_ID@::register_lsp_services>(argc, argv);
}
]==])
_cursiv_new_emit([==[src/cli.cpp]==] [==[// The @CURSIV_NEW_ID@ command line: it parses, links and validates a file and
// prints its diagnostics. Run it with `--help` to list the options. It returns
// 0 for no errors, 1 for document errors, 2 for invalid arguments or paths, and
// 3 for an internal or output error.

#include "cursiv/cli.hpp"
#include "cursiv/services/CoreLanguage.hpp"
#include "@CURSIV_NEW_ID@/CoreServices.hpp"

// Adds the language to the workspace that the command line creates.
constexpr auto kRegister =
    &cursiv::register_language<@CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@CoreServices,
                           @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedCoreServices,
                           @CURSIV_NEW_ID@::install_core_module>;

int main(int argc, char** argv) {
  return cursiv::run_cli<@CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedCoreServices>(
      argc, argv, kRegister);
}
]==])
_cursiv_new_emit([==[src/core/@CURSIV_NEW_ID@/@CURSIV_NEW_CLASS@Grammar.hpp]==] [==[// The grammar of the @CURSIV_NEW_ID@ language, written in C++. The build
// generates the parser from this header.
#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "@CURSIV_NEW_ID@/ast.hpp"

// The sample language declares people and greets them:
//
//     person Alice
//     Hello Alice!
//
// The members of a grammar:
//   * `terminal<...>`  a token, such as a name or a number; `as<T>(...)`
//                      converts its text, for example to a number
//   * `rule<...>`      a rule; it builds the AST class whose members it fills
//   * `entry`          the rule that reads a whole document
//   * `ignored`        tokens allowed anywhere and dropped, such as spaces
//   * `hidden`         tokens allowed anywhere and kept in the syntax tree,
//                      such as comments, which hover can then show
//
// In a rule, `ref<R>` reads the rule or terminal `R`, and these build the AST:
//   * `create<T>`         build a T, even if no member is set
//   * `assign<&T::m>(e)`  read `e` and store it in `m`
//   * `append<&T::m>(e)`  read `e` and add it to the list `m`
//   * `flag<&T::m>(e)`    read `e` and set the bool `m` to true
//   * `nest<&T::m>(e)`    wrap the node built so far in a new T, stored in `m`
//
// If the build refuses the grammar, look up the message on the "Grammar
// errors" page of the Cursiv documentation.

namespace @CURSIV_NEW_ID@ {

struct @CURSIV_NEW_CLASS@ : cursiv::GrammarSpec {
  // `s` matches a whitespace character, `w` a word character, and `cr<"...">`
  // a character from a range, written as in a regular expression.
  struct WS : terminal<some(s)> {};
  struct SL_COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ML_COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};

  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};

  // `kw` matches a whole keyword, not the start of a longer word; `ikw` does
  // the same in any letter case, and `lit` matches exact text such as `!`.
  struct Person : rule<kw<"person"> + assign<&ast::Person::name>(ref<ID>)> {};

  // `ast::Greeting::person` is a `reference<Person>`: this reads a name, and
  // after parsing the name resolves to the `Person` declared with it. By
  // default, a named node is visible inside the node that contains it.
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
_cursiv_new_emit([==[src/core/@CURSIV_NEW_ID@/CoreModule.cpp]==] [==[// Installs the services of the @CURSIV_NEW_ID@ language: first the parser and
// the other services generated from the grammar, then your own, such as the
// validation checks.

#include "@CURSIV_NEW_ID@/CoreServices.hpp"
#include "@CURSIV_NEW_ID@/validation/@CURSIV_NEW_CLASS@Validator.hpp"
#include "@CURSIV_NEW_ID@/generated/@CURSIV_NEW_CLASS@/Module.hpp"

namespace @CURSIV_NEW_ID@ {

void install_core_module(@CURSIV_NEW_CLASS@CoreServices& services) {
  @CURSIV_NEW_ID@::generated::@CURSIV_NEW_CLASS@::install_generated_module(services);
  register_validation_checks(services);
}

}  // namespace @CURSIV_NEW_ID@
]==])
_cursiv_new_emit([==[src/core/@CURSIV_NEW_ID@/CoreServices.hpp]==] [==[// The services of the @CURSIV_NEW_ID@ language.
#pragma once

#include <memory>

#include "cursiv/services/CoreServicesFor.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "@CURSIV_NEW_ID@/validation/@CURSIV_NEW_CLASS@Validator.hpp"

// The shared services belong to the whole workspace, such as its documents.
// The core services belong to this language. To add a service, declare it in
// the core services and create it in `install_core_module`.

namespace @CURSIV_NEW_ID@ {

struct @CURSIV_NEW_CLASS@SharedCoreServices : virtual cursiv::SharedCoreServices {};

struct @CURSIV_NEW_CLASS@CoreServices
    : cursiv::CoreServicesFor<@CURSIV_NEW_CLASS@SharedCoreServices> {
  std::unique_ptr<const @CURSIV_NEW_CLASS@Validator> validator;

  using CoreServicesFor::CoreServicesFor;

  // Lists the services after the default ones, so that a missing required
  // service is reported by name. Add a line for each service you add.
  void visitServices(
      cursiv::function_ref<void(const cursiv::ServiceSlot&)> yield) const override {
    cursiv::CoreServices::visitServices(yield);
    yield({"validator", true, validator != nullptr});
  }
};

// Installs the services of this language; see CoreModule.cpp.
void install_core_module(@CURSIV_NEW_CLASS@CoreServices& services);

}  // namespace @CURSIV_NEW_ID@
]==])
_cursiv_new_emit([==[src/core/@CURSIV_NEW_ID@/ast.hpp]==] [==[// The AST classes of the @CURSIV_NEW_ID@ language. The grammar fills their
// members when it parses a document.
#pragma once

#include "cursiv/syntax-tree/AstNode.hpp"

// `cursiv::Extends<cursiv::AstNode>` provides the member types used below:
// `string_view` holds text, `vector<T*>` holds child nodes, and `reference<T>`
// holds a name that resolves to a `T` after parsing.

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
_cursiv_new_emit([==[src/core/@CURSIV_NEW_ID@/validation/@CURSIV_NEW_CLASS@Validator.cpp]==] [==[#include "@CURSIV_NEW_ID@/validation/@CURSIV_NEW_CLASS@Validator.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/validation/ValidationRegistry.hpp"
#include "@CURSIV_NEW_ID@/CoreServices.hpp"

namespace @CURSIV_NEW_ID@ {

// `.property<&ast::Person::name>()` marks the name instead of the whole
// declaration.
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

// Creates the validator and registers its checks. Each check runs on every
// node of the type of its first parameter.
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
_cursiv_new_emit([==[src/core/@CURSIV_NEW_ID@/validation/@CURSIV_NEW_CLASS@Validator.hpp]==] [==[// The validation checks of the @CURSIV_NEW_ID@ language: rules that the
// grammar alone cannot express. Declare a check here, write it in the .cpp
// file and register it in `register_validation_checks`.
#pragma once

#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "@CURSIV_NEW_ID@/ast.hpp"

namespace @CURSIV_NEW_ID@ {

class @CURSIV_NEW_CLASS@Validator final : public cursiv::DefaultCoreService {
 public:
  using cursiv::DefaultCoreService::DefaultCoreService;

  void checkPersonStartsWithCapital(const ast::Person& person,
                                    cursiv::ValidationAcceptor& accept) const;
  void checkUniquePersons(const ast::Model& model,
                          cursiv::ValidationAcceptor& accept) const;
};

// Creates the validator and registers its checks; `install_core_module` calls
// it.
struct @CURSIV_NEW_CLASS@CoreServices;
void register_validation_checks(@CURSIV_NEW_CLASS@CoreServices& services);

}  // namespace @CURSIV_NEW_ID@
]==])
_cursiv_new_emit([==[src/lsp.cpp]==] [==[// The @CURSIV_NEW_ID@ language server. The editor starts it and talks to it
// over standard input and output.

#include "cursiv/runtime/LanguageServerMain.hpp"
#include "@CURSIV_NEW_ID@/LspModule.hpp"

int main(int argc, char** argv) {
  return cursiv::run_language_server_main<
      @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedServices>(
      argc, argv, "@CURSIV_NEW_ID@-lsp", &@CURSIV_NEW_ID@::register_lsp_services);
}
]==])
_cursiv_new_emit([==[src/lsp/@CURSIV_NEW_ID@/LspModule.cpp]==] [==[#include "@CURSIV_NEW_ID@/LspModule.hpp"

#include <memory>
#include <utility>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/LspServices.hpp"
#include "@CURSIV_NEW_ID@/CoreServices.hpp"

namespace @CURSIV_NEW_ID@ {

void install_lsp_module(@CURSIV_NEW_CLASS@Services& services) {
  cursiv::install_default_lsp_services(services);
}

// Creates the services of this language for the language server: the
// defaults, then the core module, then the LSP module.
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
_cursiv_new_emit([==[src/lsp/@CURSIV_NEW_ID@/LspModule.hpp]==] [==[#pragma once

#include "cursiv/services/SharedCoreServices.hpp"
#include "@CURSIV_NEW_ID@/LspServices.hpp"

namespace @CURSIV_NEW_ID@ {

// Installs the editor services: the defaults, then your own, such as a
// formatter or a hover provider. Add yours in LspModule.cpp.
void install_lsp_module(@CURSIV_NEW_CLASS@Services& services);

// Adds this language to the workspace of the language server; `main` passes
// it to the server.
bool register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace @CURSIV_NEW_ID@
]==])
_cursiv_new_emit([==[src/lsp/@CURSIV_NEW_ID@/LspServices.hpp]==] [==[// The services of the language server: the core services of the language plus
// the editor services. To add an editor service, declare it here.
#pragma once

#include "cursiv/services/LspServicesFor.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "@CURSIV_NEW_ID@/CoreServices.hpp"

namespace @CURSIV_NEW_ID@ {

struct @CURSIV_NEW_CLASS@SharedServices : @CURSIV_NEW_CLASS@SharedCoreServices,
                                           cursiv::SharedLspServices {};

struct @CURSIV_NEW_CLASS@Services final
    : cursiv::LspServicesFor<@CURSIV_NEW_CLASS@CoreServices, @CURSIV_NEW_CLASS@SharedServices> {
  using LspServicesFor::LspServicesFor;
};

}  // namespace @CURSIV_NEW_ID@
]==])
_cursiv_new_emit([==[tests/CMakeLists.txt]==] [==[# Parse the sample and check its syntax tree.
add_executable(@CURSIV_NEW_ID@_grammar_test grammar_test.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "WASI" AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
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
  # The same, named from where the caller stands.
  add_test(NAME @CURSIV_NEW_ID@_cli_relative_test
           COMMAND @CURSIV_NEW_ID@_cli example/hello@CURSIV_NEW_EXT@
           WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()
]==])
_cursiv_new_emit([==[tests/grammar_test.cpp]==] [==[// Parses the sample document and checks its AST. Update the checks at the end
// when you change the language. CTest reads the exit code: 0 means success.

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "cursiv/parser/Parser.hpp"
#include "cursiv/services/CoreLanguage.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "@CURSIV_NEW_ID@/CoreServices.hpp"
#include "@CURSIV_NEW_ID@/ast.hpp"

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

  // Create the language services, including the parser.
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

// Bundle the extension and its dependencies into one file. VS Code provides
// the `vscode` module itself.
const context = await esbuild.context({
  entryPoints: ['src/extension.ts'],
  bundle: true,
  format: 'cjs',
  platform: 'node',
  target: 'node20', // The Node.js version of VS Code 1.91.
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
    "vscode": "^1.91.0"
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
    "vscode-languageclient": "^11.0.0"
  },
  "devDependencies": {
    "@types/node": "^20.19.43",
    "@types/vscode": "~1.91.0",
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
_cursiv_new_emit([==[vscode/src/extension.ts]==] [==[// The @CURSIV_NEW_ID@ extension for VS Code. It starts the language server
// `@CURSIV_NEW_ID@_lsp` and talks to it over standard input and output.

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

// How to start the server: a native server runs directly, and a WebAssembly
// server runs with the Node.js that runs this extension.
interface ServerCommand {
    command: string;
    args: string[];
    env?: Record<string, string>;
}

// Native and WebAssembly builds name the server the same way, so the first
// bytes of the file tell them apart.
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

// The server built by the active CMake preset: when you press F5,
// `launch.json` sets CURSIV_SERVER_DIR to its build folder.
function presetCommand(context: vscode.ExtensionContext): ServerCommand | undefined {
    const directory = process.env.CURSIV_SERVER_DIR?.trim();
    if (!directory) return undefined;
    const built = path.join(directory, executableName());
    if (!fs.existsSync(built)) return undefined;
    if (!isWasm(built)) return { command: built, args: [] };
    return wasmRun(built, path.join(context.extensionPath, 'server', 'wasi-run.mjs'));
}

// The WebAssembly server under `server/`, which a WASI build copies there. It
// runs on every platform, so a packaged extension prefers it.
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
        // A WebAssembly server cannot see how many CPU cores there are.
        env: { CURSIV_WORKERS: String(Math.max(0, os.availableParallelism() - 1)) },
    };
}

// The native server: the packaged copy under `bin/`, or the one CMake built
// in `../build/src/`.
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

    const outputChannel = vscode.window.createOutputChannel(ID, { log: true });
    context.subscriptions.push(outputChannel);
    outputChannel.appendLine(`server: ${server.command} ${server.args.join(' ')}`);

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
    // Let AI assistants use the language server through MCP. They see the
    // same documents as the editor, including unsaved changes.
    context.subscriptions.push(startMcpBridge(ID, '@CURSIV_NEW_CLASS@', client, outputChannel));
}

export async function deactivate(): Promise<void> {
    const running = client;
    client = undefined;
    await running?.stop();
}
]==])
_cursiv_new_emit([==[vscode/src/mcp.ts]==] [==[// The MCP server of the extension. It accepts HTTP requests from this machine
// only, with a token created for each session, and forwards them to the
// language server.

import * as crypto from 'node:crypto';
import * as http from 'node:http';
import * as vscode from 'vscode';
import type { LanguageClient } from 'vscode-languageclient/node';

const MCP_METHOD = 'cursiv/mcp';

// The MCP registration API of VS Code 1.101 and later, declared here so the
// extension still builds and runs on VS Code 1.91.
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
        // Refuse requests from web pages (protection against DNS rebinding).
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
                // A notification has no result: reply 202 Accepted.
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
            // Other MCP clients connect with this URL and token, for example:
            //   claude mcp add --transport http <name> <url> --header "Authorization: Bearer <token>"
            outputChannel.appendLine(`mcp: ${url} (Authorization: Bearer ${token})`);
            const api = vscode as unknown as McpRegistry;
            const register = api.lm?.registerMcpServerDefinitionProvider;
            const HttpDefinition = api.McpHttpServerDefinition;
            // Before 1.101, VS Code cannot register MCP servers: clients use
            // the URL above.
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
_cursiv_new_emit([==[cmake/wasi.cmake]==] [==[# The default toolchain of the wasi-debug and wasi-release presets: the Clang of
# wasi-sdk 34, the pinned release. Another compiler has a file like this one, and
# shares the rest through wasi-platform.cmake.
# Cursiv throws: `-fwasm-exceptions` at both compile and link selects the `eh`
# half of the SDK's dual `eh`/`noeh` sysroot.
if(NOT WASI_SDK)
  set(WASI_SDK $ENV{WASI_SDK})
endif()
if(CMAKE_HOST_WIN32)
  set(_cursiv_exe .exe)
  file(TO_CMAKE_PATH "${WASI_SDK}" WASI_SDK)
  file(TO_CMAKE_PATH "$ENV{USERPROFILE}" _cursiv_home)
else()
  set(_cursiv_exe "")
  set(_cursiv_home "$ENV{HOME}")
endif()
# The conventional spots, so a preset works with no environment at all.
if(NOT WASI_SDK)
  foreach(_cursiv_candidate "${_cursiv_home}/wasi-sdk-34.0" "/opt/wasi-sdk")
    if(EXISTS "${_cursiv_candidate}/bin/clang++${_cursiv_exe}")
      set(WASI_SDK "${_cursiv_candidate}")
      break()
    endif()
  endforeach()
endif()
if(NOT WASI_SDK)
  message(FATAL_ERROR
      "run cmake -P tools/fetch-wasi-sdk.cmake, or set "
      "-DWASI_SDK=<wasi-sdk-34+ directory> (the presets also look in "
      "~/wasi-sdk-34.0 and /opt/wasi-sdk), or pass your own toolchain with "
      "--toolchain.")
endif()

# Carried into try_compile, which re-runs this file in a scope the cache does
# not reach: without this line CMake probes the compiler with no compiler set.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES WASI_SDK)

set(CMAKE_C_COMPILER   ${WASI_SDK}/bin/clang${_cursiv_exe})
set(CMAKE_CXX_COMPILER ${WASI_SDK}/bin/clang++${_cursiv_exe})
set(CMAKE_AR           ${WASI_SDK}/bin/llvm-ar${_cursiv_exe})
set(CMAKE_RANLIB       ${WASI_SDK}/bin/llvm-ranlib${_cursiv_exe})
set(CMAKE_SYSROOT      ${WASI_SDK}/share/wasi-sysroot)

# `-mllvm -wasm-use-legacy-eh=false` selects the EH scheme the runtime still
# accepts.
set(_cursiv_wasi_eh "-fwasm-exceptions -mllvm -wasm-use-legacy-eh=false")
set(CMAKE_C_FLAGS_INIT   "${_cursiv_wasi_eh}")
set(CMAKE_CXX_FLAGS_INIT "${_cursiv_wasi_eh}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wno-unused-command-line-argument -lunwind")

include(${CMAKE_CURRENT_LIST_DIR}/wasi-platform.cmake)

set(CMAKE_C_COMPILER_TARGET   ${CURSIV_WASI_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${CURSIV_WASI_TARGET})

# SDK Clang runs wasm-opt automatically for optimized links when it is on
# PATH, and preserves target_features for it. Do not add another pass here.
]==])
_cursiv_new_emit([==[cmake/wasi-platform.cmake]==] [==[# What every WASI toolchain shares, whichever compiler it names. A toolchain
# file names its compiler and includes this one: see cmake/wasi.cmake, and
# "Use your own toolchain" in docs/recipes/wasm.md.
set(CMAKE_SYSTEM_NAME WASI)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

# Carried into try_compile, which re-runs the toolchain in a scope the cache
# does not reach.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
     CURSIV_WASI_THREADS CURSIV_WASI_MAX_MEMORY CURSIV_WASI_STACK)

# Threads are a different target, not a flag: `wasm32-wasip1-threads` has
# its own sysroot and libc++. It is the production target (vscode/node), so
# it is the default; OFF builds the threadless `wasm32-wasip1`, inline-only.
option(CURSIV_WASI_THREADS "Build for wasm32-wasip1-threads" ON)
if(CURSIV_WASI_THREADS)
  set(CURSIV_WASI_TARGET wasm32-wasip1-threads)
else()
  set(CURSIV_WASI_TARGET wasm32-wasip1)
endif()

# The _WASI_EMULATED_* defines are what lets `<csignal>` be included on a target
# with no signals.
set(_cursiv_wasi_emul "-D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS")
string(APPEND CMAKE_C_FLAGS_INIT " ${_cursiv_wasi_emul}")
string(APPEND CMAKE_CXX_FLAGS_INIT " ${_cursiv_wasi_emul}")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT
       " -lwasi-emulated-signal -lwasi-emulated-process-clocks")
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
  add_link_options("LINKER:--import-memory,--max-memory=${CURSIV_WASI_MAX_MEMORY}")
endif()
# The linker's default stack is 64 KiB, which recursive AST walks and large
# frames overrun: past it the stack pointer wraps into data and a store traps.
set(CURSIV_WASI_STACK 1048576 CACHE STRING
    "Stack reserved for a wasm executable's main thread (bytes)")
# Directory link options are regenerated when cache tunables change, without
# rewriting the user's CMAKE_EXE_LINKER_FLAGS.
add_link_options("LINKER:-z,stack-size=${CURSIV_WASI_STACK}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# A sysroot's libraries can contain DWARF even when our own sources use Release.
add_link_options("$<$<CONFIG:Release>:LINKER:--strip-debug>")

# The project resolves the runner after locating the Cursiv source tree.
find_program(CURSIV_NODE node)
]==])
_cursiv_new_emit([==[tools/fetch-wasi-sdk.cmake]==] [==[# The pinned SDK, fetched and verified -- one version, one checksum per host,
# because an artifact is reproducible only against an exact release.
# cmake/wasi.cmake finds the result on its own.
#   cmake -P tools/fetch-wasi-sdk.cmake                 # to ~/wasi-sdk-34.0
#   cmake -DDEST=<dir> -P tools/fetch-wasi-sdk.cmake
cmake_minimum_required(VERSION 3.30)

set(_version 34.0)
set(_release https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34)
set(_sha256_x86_64_linux b761e3a0721dbae9c09a0059e5fdb2bf917d1b4a8a7b430fb3b5aafb0984b2c4)
set(_sha256_arm64_linux f7e243dff54d60bcc576e94d6166b69f410f2500ae4a9ceef34315be10e77971)
set(_sha256_x86_64_macos 87d27fa8adc68dee59bfbf2e22a6d34ef717c34d6bf1d8af2a56fc929d9ce0eb)
set(_sha256_arm64_macos 9c59398106b417f8f14913380fdf0097a8cc0ff4af9eb3ce0065a859e88d49e9)
set(_sha256_x86_64_windows cccb5c323a9b34f0349a9b09e8804a0a7632c68c3310f4b5f437ed57d7e71d8f)
set(_sha256_arm64_windows 45e1c71f3e965621e7b98ebe1d37b0e4b1f77f3e8072113ffb4534e67b1a4b7c)

if(CMAKE_HOST_WIN32)
  set(_exe .exe)
  file(TO_CMAKE_PATH "$ENV{USERPROFILE}" _home)
else()
  set(_exe "")
  set(_home "$ENV{HOME}")
endif()
if(NOT DEFINED DEST)
  set(DEST "${_home}/wasi-sdk-${_version}")
endif()

# Whether `dir` holds this version, whole.
function(_sdk_complete dir out)
  set(${out} FALSE PARENT_SCOPE)
  if(NOT EXISTS "${dir}/VERSION")
    return()
  endif()
  file(STRINGS "${dir}/VERSION" _lines LIMIT_COUNT 1)
  if(NOT _lines STREQUAL _version)
    return()
  endif()
  foreach(_tool clang clang++ llvm-ar llvm-ranlib wasm-ld)
    if(NOT EXISTS "${dir}/bin/${_tool}${_exe}")
      return()
    endif()
  endforeach()
  foreach(_target wasm32-wasip1 wasm32-wasip1-threads)
    foreach(_file include/${_target}/stdio.h include/${_target}/eh/c++/v1/format
                  lib/${_target}/libc.a lib/${_target}/eh/libc++.a)
      set(_path "${dir}/share/wasi-sysroot/${_file}")
      if(NOT EXISTS "${_path}")
        return()
      endif()
      file(SIZE "${_path}" _size)
      if(_size EQUAL 0)
        return()
      endif()
    endforeach()
  endforeach()
  set(${out} TRUE PARENT_SCOPE)
endfunction()

_sdk_complete("${DEST}" _done)
if(_done)
  message(STATUS "already installed: ${DEST} (${_version})")
  return()
endif()
# Nothing here overwrites what it did not write.
if(EXISTS "${DEST}")
  message(FATAL_ERROR "${DEST} exists and is not wasi-sdk ${_version}: "
                      "remove it, or choose another with -DDEST=<dir>")
endif()

string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" _os)
if(_os STREQUAL "darwin")
  set(_os macos)
endif()
cmake_host_system_information(RESULT _arch QUERY OS_PLATFORM)
string(TOLOWER "${_arch}" _arch)
if(_arch STREQUAL "amd64")
  set(_arch x86_64)
elseif(_arch STREQUAL "aarch64")
  set(_arch arm64)
endif()
set(_sha256 "${_sha256_${_arch}_${_os}}")
if(NOT _sha256)
  message(FATAL_ERROR "wasi-sdk ${_version} has no release for ${_arch} ${_os}")
endif()

# Unpacked beside its destination, then moved in one step: an interrupted run
# leaves no half-installed SDK behind.
set(_name wasi-sdk-${_version}-${_arch}-${_os})
set(_work "${DEST}.fetching")
file(REMOVE_RECURSE "${_work}")
set(_tarball "${_work}/${_name}.tar.gz")
foreach(_attempt 1 2 3)
  message(STATUS "fetching ${_release}/${_name}.tar.gz")
  file(DOWNLOAD "${_release}/${_name}.tar.gz" "${_tarball}"
       EXPECTED_HASH SHA256=${_sha256} TLS_VERIFY ON STATUS _status)
  list(GET _status 0 _code)
  if(_code EQUAL 0)
    break()
  endif()
  list(GET _status 1 _why)
  message(WARNING "download failed: ${_why}")
endforeach()
if(NOT _code EQUAL 0)
  file(REMOVE_RECURSE "${_work}")
  message(FATAL_ERROR "cannot fetch wasi-sdk ${_version}")
endif()
file(ARCHIVE_EXTRACT INPUT "${_tarball}" DESTINATION "${_work}")
file(RENAME "${_work}/${_name}" "${DEST}")
file(REMOVE_RECURSE "${_work}")

_sdk_complete("${DEST}" _done)
if(NOT _done)
  message(FATAL_ERROR "incomplete wasi-sdk installation: ${DEST}")
endif()
message(STATUS "installed: ${DEST} (${_version})")
]==])

_cursiv_new_finish()
