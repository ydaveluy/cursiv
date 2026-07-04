# =============================================================================
# GENERATED FILE — DO NOT EDIT.
# Produced by cmake/GenerateScaffold.cmake from cmake/pegium-new.cmake.in and the
# templates under templates/project/. To change the scaffolder, edit those and
# regenerate:  cmake -DPEGIUM_TAG=main -P cmake/GenerateScaffold.cmake
# =============================================================================
# pegium-new.cmake — scaffold a new pegium language. CMake only.
# Usage: cmake -DNAME=MyLang [-DEXT=.ml] [-DDIR=mylang] [-DLSP=ON] [-DVSCODE=ON]
#               [-DCLI=ON] [-DPEGIUM_TAG=main] -P pegium-new.cmake
cmake_minimum_required(VERSION 3.14)

if(NOT DEFINED NAME)
  message(FATAL_ERROR "NAME is required: cmake -DNAME=MyLang -P pegium-new.cmake")
endif()
if(NOT NAME MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
  message(FATAL_ERROR "NAME must be a valid C++ identifier (got '${NAME}')")
endif()

string(TOLOWER "${NAME}" PEGIUM_NEW_LANGUAGE_ID)
set(PEGIUM_NEW_CLASS "${NAME}")
if(NOT DEFINED EXT)
  set(EXT ".${PEGIUM_NEW_LANGUAGE_ID}")
endif()
if(NOT EXT MATCHES "^\\.")
  message(FATAL_ERROR "EXT must start with '.' (got '${EXT}')")
endif()
set(PEGIUM_NEW_EXT "${EXT}")
if(NOT DEFINED DIR)
  set(DIR "${PEGIUM_NEW_LANGUAGE_ID}")
endif()
if(NOT DEFINED PEGIUM_TAG)
  set(PEGIUM_TAG "main")
endif()
set(PEGIUM_NEW_PEGIUM_TAG "${PEGIUM_TAG}")
foreach(_opt LSP VSCODE CLI)
  if(NOT DEFINED ${_opt})
    set(${_opt} ON)
  endif()
endforeach()

if(NOT LSP)
  set(VSCODE OFF)
  message(STATUS "LSP=OFF: the VSCode extension is disabled (it needs the language server).")
endif()

get_filename_component(_target "${DIR}" ABSOLUTE)
if(EXISTS "${_target}")
  file(GLOB _existing LIST_DIRECTORIES true "${_target}/*" "${_target}/.*")
  if(_existing)
    message(FATAL_ERROR "Target dir '${_target}' is not empty; refusing to overwrite")
  endif()
endif()

function(_pegium_new_emit rel toggle content)
  if(toggle STREQUAL "lsp" AND NOT LSP)
    return()
  endif()
  if(toggle STREQUAL "vscode" AND NOT VSCODE)
    return()
  endif()
  if(toggle STREQUAL "cli" AND NOT CLI)
    return()
  endif()
  # Substitute the @PEGIUM_NEW_*@ placeholders in both the path (directory and
  # file names) and the file content, using the scaffold-time values. Paths use
  # string(CONFIGURE) too, so the templates carry no hardcoded language id.
  string(CONFIGURE "${rel}" rel @ONLY)
  string(CONFIGURE "${content}" out @ONLY)
  set(dest "${_target}/${rel}")
  get_filename_component(_dir "${dest}" DIRECTORY)
  file(MAKE_DIRECTORY "${_dir}")
  file(WRITE "${dest}" "${out}")
  message(STATUS "  + ${rel}")
endfunction()

function(_pegium_new_finish)
  message(STATUS "Created ${PEGIUM_NEW_LANGUAGE_ID} in ${_target}")
  message(STATUS "Next: cd ${DIR} && cmake -B build && cmake --build build -j")
endfunction()

message(STATUS "Scaffolding ${NAME} (id=${PEGIUM_NEW_LANGUAGE_ID}, ext=${EXT})")

_pegium_new_emit([==[.github/workflows/ci.yml]==] [==[always]==] [==[name: CI

# Build the language and run its tests on every push and pull request.
# The first run is slow: it clones and compiles pegium via FetchContent. To speed
# up repeated runs, add caching (e.g. actions/cache over build/_deps, or ccache).
# To also test Windows/macOS, add
#   strategy:
#     matrix:
#       os: [ubuntu-latest, macos-latest, windows-latest]
# and set `runs-on: ${{ matrix.os }}`.

on:
  push:
  pull_request:

concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true

jobs:
  build-test:
    name: Build & test
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=Debug
      - name: Build
        run: cmake --build build -j
      - name: Test
        run: ctest --test-dir build --output-on-failure
      # Type-check and bundle the VS Code extension, when one is present.
      - name: Set up Node
        if: hashFiles('vscode/package.json') != ''
        uses: actions/setup-node@v4
        with:
          node-version: 20
      - name: Build the extension
        if: hashFiles('vscode/package.json') != ''
        working-directory: vscode
        run: |
          npm install
          npm run compile
]==])
_pegium_new_emit([==[.github/workflows/release.yml]==] [==[vscode]==] [==[name: Release VS Code extension

# Builds one platform-specific .vsix per target — each bundles the native
# @PEGIUM_NEW_LANGUAGE_ID@-lsp server built on that platform's runner (the C++
# server cannot be cross-compiled the way a Go/Rust one could).

on:
  workflow_dispatch:
  push:
    tags:
      - 'v*'

jobs:
  package:
    name: Package (${{ matrix.target }})
    strategy:
      fail-fast: false
      matrix:
        include:
          - os: ubuntu-latest
            target: linux-x64
          - os: windows-latest
            target: win32-x64
          - os: macos-13
            target: darwin-x64
          - os: macos-latest
            target: darwin-arm64
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: 20
      - name: Install extension dependencies
        working-directory: vscode
        run: npm install
      - name: Package
        # builds the server Release + stripped, then bundles it into the .vsix
        working-directory: vscode
        run: npm run package -- --target ${{ matrix.target }}
      - uses: actions/upload-artifact@v4
        with:
          name: @PEGIUM_NEW_LANGUAGE_ID@-${{ matrix.target }}
          path: vscode/*.vsix
      # To publish to the Marketplace: set a real `publisher` in
      # vscode/package.json, add a VSCE_PAT repository secret, then uncomment:
      # - name: Publish
      #   working-directory: vscode
      #   run: npm run publish -- --target ${{ matrix.target }}
      #   env:
      #     VSCE_PAT: ${{ secrets.VSCE_PAT }}
]==])
_pegium_new_emit([==[.gitignore]==] [==[always]==] [==[/build*
vscode/node_modules
vscode/out
vscode/bin
*.vsix
]==])
_pegium_new_emit([==[.vscode/launch.json]==] [==[vscode]==] [==[{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Run @PEGIUM_NEW_CLASS@ Extension",
            "type": "extensionHost",
            "request": "launch",
            "runtimeExecutable": "${execPath}",
            "args": [
                "--extensionDevelopmentPath=${workspaceFolder}/vscode",
                "${workspaceFolder}/example",
                "${workspaceFolder}/example/hello@PEGIUM_NEW_EXT@"
            ],
            "outFiles": [
                "${workspaceFolder}/vscode/out/**/*.js"
            ],
            "preLaunchTask": "Build all"
        }
    ]
}
]==])
_pegium_new_emit([==[.vscode/tasks.json]==] [==[vscode]==] [==[{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Configure (CMake)",
            "type": "shell",
            "command": "cmake",
            "args": ["-B", "build", "-DCMAKE_BUILD_TYPE=Debug"],
            "options": { "cwd": "${workspaceFolder}" },
            "presentation": { "reveal": "silent", "panel": "shared", "clear": true },
            "problemMatcher": []
        },
        {
            "label": "Build language server (C++)",
            "type": "shell",
            "command": "cmake",
            "args": ["--build", "build", "--target", "@PEGIUM_NEW_LANGUAGE_ID@-lsp", "-j"],
            "options": { "cwd": "${workspaceFolder}" },
            "dependsOn": "Configure (CMake)",
            "presentation": { "reveal": "silent", "panel": "shared", "clear": true },
            "problemMatcher": "$gcc"
        },
        {
            "label": "Install extension deps (npm)",
            "type": "shell",
            "command": "npm",
            "args": ["install"],
            "options": { "cwd": "${workspaceFolder}/vscode" },
            "presentation": { "reveal": "silent", "panel": "shared", "clear": true },
            "problemMatcher": []
        },
        {
            "label": "Build extension (TypeScript)",
            "type": "shell",
            "command": "npm",
            "args": ["run", "compile"],
            "options": { "cwd": "${workspaceFolder}/vscode" },
            "dependsOn": "Install extension deps (npm)",
            "presentation": { "reveal": "silent", "panel": "shared", "clear": true },
            "problemMatcher": "$tsc"
        },
        {
            "label": "Build all",
            "dependsOn": [
                "Build language server (C++)",
                "Build extension (TypeScript)"
            ],
            "dependsOrder": "parallel",
            "group": { "kind": "build", "isDefault": true },
            "problemMatcher": []
        }
    ]
}
]==])
_pegium_new_emit([==[CMakeLists.txt]==] [==[always]==] [==[cmake_minimum_required(VERSION 3.15)
project(@PEGIUM_NEW_LANGUAGE_ID@ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    pegium
    GIT_REPOSITORY https://github.com/ydaveluy/pegium.git
    GIT_TAG @PEGIUM_NEW_PEGIUM_TAG@
)
FetchContent_MakeAvailable(pegium)

# Sources are discovered by globbing, so adding a .cpp under src/ (or a test under
# test/) needs no edit here. CONFIGURE_DEPENDS re-globs when files are added/removed.
set(_src "${CMAKE_CURRENT_SOURCE_DIR}/src/@PEGIUM_NEW_LANGUAGE_ID@")

# Each consumable library also gets a `@PEGIUM_NEW_LANGUAGE_ID@::` namespaced
# alias, mirroring pegium's own `pegium::core` / `pegium::lsp` targets. Link the
# aliases (not the raw names): a parent project that pulls this in with
# add_subdirectory(...) then consumes `@PEGIUM_NEW_LANGUAGE_ID@::core` /
# `@PEGIUM_NEW_LANGUAGE_ID@::lsp` exactly as it would any installed package.

# Core library: every source under core/.
file(GLOB_RECURSE _core_sources CONFIGURE_DEPENDS "${_src}/core/*.cpp")
if(_core_sources)
  add_library(@PEGIUM_NEW_LANGUAGE_ID@-core ${_core_sources})
  add_library(@PEGIUM_NEW_LANGUAGE_ID@::core ALIAS @PEGIUM_NEW_LANGUAGE_ID@-core)
  target_include_directories(@PEGIUM_NEW_LANGUAGE_ID@-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(@PEGIUM_NEW_LANGUAGE_ID@-core PUBLIC pegium::core)
endif()

# CLI tool: every source under cli/ (main.cpp plus any helpers).
file(GLOB_RECURSE _cli_sources CONFIGURE_DEPENDS "${_src}/cli/*.cpp")
if(_cli_sources)
  add_executable(@PEGIUM_NEW_LANGUAGE_ID@-cli ${_cli_sources})
  target_link_libraries(@PEGIUM_NEW_LANGUAGE_ID@-cli PRIVATE @PEGIUM_NEW_LANGUAGE_ID@::core pegium::cli)
endif()

# LSP: the server library is every lsp/ source except the executable entry point,
# which becomes the -lsp server.
file(GLOB_RECURSE _lsp_sources CONFIGURE_DEPENDS "${_src}/lsp/*.cpp")
set(_lsp_main "${_src}/lsp/main.cpp")
list(REMOVE_ITEM _lsp_sources "${_lsp_main}")
if(_lsp_sources)
  add_library(@PEGIUM_NEW_LANGUAGE_ID@-lsp-lib ${_lsp_sources})
  add_library(@PEGIUM_NEW_LANGUAGE_ID@::lsp ALIAS @PEGIUM_NEW_LANGUAGE_ID@-lsp-lib)
  target_include_directories(@PEGIUM_NEW_LANGUAGE_ID@-lsp-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(@PEGIUM_NEW_LANGUAGE_ID@-lsp-lib PUBLIC @PEGIUM_NEW_LANGUAGE_ID@::core pegium::lsp)
endif()

# The LSP server is built whenever lsp/main.cpp exists — even when it is the only
# lsp/ source (so there is no separate server library). Link the server library
# when it exists, otherwise link core + pegium::lsp directly.
if(EXISTS "${_lsp_main}")
  add_executable(@PEGIUM_NEW_LANGUAGE_ID@-lsp ${_lsp_main})
  if(TARGET @PEGIUM_NEW_LANGUAGE_ID@-lsp-lib)
    target_link_libraries(@PEGIUM_NEW_LANGUAGE_ID@-lsp PRIVATE @PEGIUM_NEW_LANGUAGE_ID@::lsp)
  else()
    target_include_directories(@PEGIUM_NEW_LANGUAGE_ID@-lsp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(@PEGIUM_NEW_LANGUAGE_ID@-lsp PRIVATE @PEGIUM_NEW_LANGUAGE_ID@::core pegium::lsp)
  endif()
endif()

enable_testing()
include(GoogleTest)  # provides gtest_discover_tests
# pegium already fetches GoogleTest for pegium::testing, so only declare it if a
# parent project (or pegium) has not provided it. Guard the CRT setting too
# instead of FORCE-ing it, so a parent project's choice is respected.
if(NOT TARGET GTest::gtest_main)
  FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.17.0  # keep in sync with the version pegium pins
  )
  if(NOT DEFINED gtest_force_shared_crt)
    set(gtest_force_shared_crt ON CACHE BOOL "")
  endif()
  FetchContent_MakeAvailable(googletest)
endif()

# Tests: every source under test/. The parsing smoke test needs only the core
# pipeline; the LSP feature tests (test/lsp/) use the reusable pegium::testing
# harness, so they and pegium::testing are linked only when the LSP server is built.
file(GLOB_RECURSE _test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")
if(_test_sources)
  add_executable(@PEGIUM_NEW_LANGUAGE_ID@-test ${_test_sources})
  set(_test_libs @PEGIUM_NEW_LANGUAGE_ID@::core pegium::cli GTest::gtest_main)
  if(TARGET @PEGIUM_NEW_LANGUAGE_ID@-lsp-lib)
    list(APPEND _test_libs @PEGIUM_NEW_LANGUAGE_ID@::lsp pegium::testing)
  endif()
  target_link_libraries(@PEGIUM_NEW_LANGUAGE_ID@-test PRIVATE ${_test_libs})
  target_compile_definitions(@PEGIUM_NEW_LANGUAGE_ID@-test PRIVATE
      PEGIUM_NEW_SAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/example/hello@PEGIUM_NEW_EXT@")
  # Register each GTest case as its own CTest test (IDE explorers, --gtest_filter).
  gtest_discover_tests(@PEGIUM_NEW_LANGUAGE_ID@-test DISCOVERY_MODE PRE_TEST)
endif()
]==])
_pegium_new_emit([==[README.md]==] [==[always]==] [==[# @PEGIUM_NEW_LANGUAGE_ID@

A hello-world language built with [pegium](https://github.com/ydaveluy/pegium).

## Quick start

### Build

```bash
cmake -B build
cmake --build build -j
```

### Run the CLI

```bash
./build/@PEGIUM_NEW_LANGUAGE_ID@-cli example/hello@PEGIUM_NEW_EXT@
```

### Open it in VS Code (press F5)

The recommended way to try the language is in the editor. Open this folder in VS
Code and press **F5** — that's the whole setup.

The first launch runs everything for you: it configures and builds the C++
language server, installs the extension's npm dependencies, and compiles the
TypeScript. Then a second window (the Extension Development Host) opens on the
`example/` folder with `hello@PEGIUM_NEW_EXT@` already open, so live syntax
highlighting, diagnostics and the other LSP features are active immediately.

> The first F5 is slow: CMake clones and builds pegium via `FetchContent`.
> Later launches are incremental.
>
> If the Extension Development Host shows *"Unable to locate
> @PEGIUM_NEW_LANGUAGE_ID@-lsp"*, the C++ build failed — check the **Terminal**
> and **Problems** panels in the main window.

### Run the tests

```bash
ctest --test-dir build --output-on-failure
```

These also run in CI on every push and pull request — see
[`.github/workflows/ci.yml`](.github/workflows/ci.yml).

### Package & publish the extension

`npm run package` builds the C++ server **Release and stripped** (in a dedicated
`build-release/` tree, so it doesn't disturb the Debug build F5 uses), copies it
into the extension's `bin/`, and produces a **platform-specific** `.vsix` — the
TypeScript is bundled with [esbuild](https://esbuild.github.io/) and the small,
optimized native server is bundled in `bin/`, so the result works as soon as it
is installed, with nothing else to configure:

```bash
(cd vscode && npm install && npm run package)          # for your platform
(cd vscode && npm run package -- --target linux-x64)   # for a specific target
```

Because the C++ server cannot be cross-compiled easily, each platform's `.vsix`
is normally built on that platform. The included
[`.github/workflows/release.yml`](.github/workflows/release.yml) does exactly that
— a Linux/Windows/macOS matrix builds and uploads one `.vsix` per target on every
`v*` tag.

To publish to the Marketplace, set a real `publisher` in `vscode/package.json`,
[create a publisher and a Personal Access Token](https://code.visualstudio.com/api/working-with-extensions/publishing-extension),
then add a `VSCE_PAT` secret and uncomment the publish step in the workflow (or run
`VSCE_PAT=… npm run publish` locally).

## What to edit next

| What you want to change     | File(s) to edit                                       |
|-----------------------------|-------------------------------------------------------|
| Add a new AST node          | `src/@PEGIUM_NEW_LANGUAGE_ID@/core/ast.hpp`                                  |
| Extend the grammar          | `src/@PEGIUM_NEW_LANGUAGE_ID@/core/@PEGIUM_NEW_CLASS@Parser.hpp`                        |
| Add validation checks       | `src/@PEGIUM_NEW_LANGUAGE_ID@/core/` (add a validator class)            |
| Add LSP features (hover, …) | `src/@PEGIUM_NEW_LANGUAGE_ID@/lsp/`                                     |
| Add a code generator        | `src/@PEGIUM_NEW_LANGUAGE_ID@/cli/main.cpp`            |
| Add more test cases         | `test/parsing_test.cpp`                               |

## Grammar at a glance

```
person John
person Jane

Hello John!
Hello Jane!
```

The grammar defines two kinds of declarations:

- `person <Name>` — declares a person.
- `Hello <Name>!` — greets a previously declared person.
]==])
_pegium_new_emit([==[example/hello@PEGIUM_NEW_EXT@]==] [==[always]==] [==[person John
person Jane

Hello John!
Hello Jane!
]==])
_pegium_new_emit([==[src/@PEGIUM_NEW_LANGUAGE_ID@/cli/main.cpp]==] [==[cli]==] [==[#include <@PEGIUM_NEW_LANGUAGE_ID@/core/CoreModule.hpp>

#include <pegium/cli/CliUtils.hpp>

#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: @PEGIUM_NEW_LANGUAGE_ID@-cli <file@PEGIUM_NEW_EXT@>\n";
    return 1;
  }
  try {
    auto sharedServices = pegium::make_shared_services<
        @PEGIUM_NEW_LANGUAGE_ID@::@PEGIUM_NEW_CLASS@SharedCoreServices>();
    auto &shared = *sharedServices;
    auto services = @PEGIUM_NEW_LANGUAGE_ID@::create@PEGIUM_NEW_CLASS@CoreServices(shared);
    auto &langServices = *services;
    shared.serviceRegistry->registerServices(std::move(services));

    auto document =
        pegium::build_document_from_path(argv[1], langServices);
    if (pegium::has_error_diagnostics(*document)) {
      std::cerr << "Validation errors:\n";
      pegium::print_error_diagnostics(*document, std::cerr);
      return 2;
    }
    std::cout << "Parsed " << argv[1] << " successfully.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Fatal error: " << error.what() << '\n';
    return 3;
  }
}
]==])
_pegium_new_emit([==[src/@PEGIUM_NEW_LANGUAGE_ID@/core/@PEGIUM_NEW_CLASS@Parser.hpp]==] [==[always]==] [==[#pragma once

#include <string>

#include <@PEGIUM_NEW_LANGUAGE_ID@/core/ast.hpp>

#include <pegium/core/parser/PegiumParser.hpp>

namespace @PEGIUM_NEW_LANGUAGE_ID@::parser {

using namespace pegium::parser;

class @PEGIUM_NEW_CLASS@Parser : public PegiumParser {
public:
  using PegiumParser::PegiumParser;

protected:
  const pegium::grammar::ParserRule &getEntryRule() const noexcept override {
    return ModelRule;
  }
  const Skipper &getSkipper() const noexcept override { return skipper; }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  static constexpr auto WS = some(s);
  Terminal<> SL_COMMENT{"SL_COMMENT", "//"_kw <=> &(eol | eof)};
  Terminal<> ML_COMMENT{"ML_COMMENT", "/*"_kw <=> "*/"_kw};
  Skipper skipper = skip(ignored(WS), hidden(ML_COMMENT, SL_COMMENT));

  // terminal ID returns string: ([A-Z_a-z] [0-9A-Z_a-z]*);
  Terminal<std::string> ID{"ID", "a-zA-Z_"_cr + many(w)};

  // Person: 'person' name=ID;
  Rule<ast::Person> PersonRule{"Person",
                               "person"_kw + assign<&ast::Person::name>(ID)};

  // Greeting: 'Hello' person=[Person:ID] '!';
  Rule<ast::Greeting> GreetingRule{
      "Greeting", "Hello"_kw + assign<&ast::Greeting::person>(ID) + "!"_kw};

  // Model: (persons+=Person | greetings+=Greeting)*;
  NullableRule<ast::Model> ModelRule{
      "Model", many(append<&ast::Model::persons>(PersonRule) |
                    append<&ast::Model::greetings>(GreetingRule))};
#pragma clang diagnostic pop
};

} // namespace @PEGIUM_NEW_LANGUAGE_ID@::parser
]==])
_pegium_new_emit([==[src/@PEGIUM_NEW_LANGUAGE_ID@/core/CoreModule.cpp]==] [==[always]==] [==[#include <@PEGIUM_NEW_LANGUAGE_ID@/core/CoreModule.hpp>

#include <utility>

#include <@PEGIUM_NEW_LANGUAGE_ID@/core/@PEGIUM_NEW_CLASS@Parser.hpp>

// This is the single translation unit that includes the grammar header, so the
// heavy grammar template instantiation happens here once, not in every module
// translation unit; the lsp module reaches the wiring through the declaration.

namespace @PEGIUM_NEW_LANGUAGE_ID@ {

std::unique_ptr<const pegium::parser::Parser> create@PEGIUM_NEW_CLASS@Parser() {
  return std::make_unique<const parser::@PEGIUM_NEW_CLASS@Parser>();
}

void install@PEGIUM_NEW_CLASS@CoreModule(@PEGIUM_NEW_CLASS@CoreServices &services) {
  services.parser =
      std::make_unique<const parser::@PEGIUM_NEW_CLASS@Parser>(services);
  services.languageMetaData.fileExtensions = {"@PEGIUM_NEW_EXT@"};
  services.validator =
      std::make_unique<validation::@PEGIUM_NEW_CLASS@Validator>(services);
  validation::registerValidationChecks(services, *services.validator);
}

std::unique_ptr<@PEGIUM_NEW_CLASS@CoreServices>
create@PEGIUM_NEW_CLASS@CoreServices(
    const @PEGIUM_NEW_CLASS@SharedCoreServices &sharedServices,
    std::string languageId) {
  auto services = pegium::makeDefaultCoreServices<@PEGIUM_NEW_CLASS@CoreServices>(
      sharedServices, std::move(languageId));
  install@PEGIUM_NEW_CLASS@CoreModule(*services);
  return services;
}

bool register@PEGIUM_NEW_CLASS@CoreServices(pegium::SharedCoreServices &sharedServices) {
  // The workspace container is the language's own shared type; recover it to
  // build the container with its custom shared services.
  sharedServices.serviceRegistry->registerServices(
      create@PEGIUM_NEW_CLASS@CoreServices(
          dynamic_cast<@PEGIUM_NEW_CLASS@SharedCoreServices &>(sharedServices)));
  return true;
}

} // namespace @PEGIUM_NEW_LANGUAGE_ID@
]==])
_pegium_new_emit([==[src/@PEGIUM_NEW_LANGUAGE_ID@/core/CoreModule.hpp]==] [==[always]==] [==[#pragma once

#include <memory>
#include <string>

#include <@PEGIUM_NEW_LANGUAGE_ID@/core/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace @PEGIUM_NEW_LANGUAGE_ID@ {

/// Creates a standalone @PEGIUM_NEW_CLASS@ parser.
std::unique_ptr<const pegium::parser::Parser> create@PEGIUM_NEW_CLASS@Parser();

/// Wires the @PEGIUM_NEW_CLASS@ core overrides. The LSP path reuses it directly.
void install@PEGIUM_NEW_CLASS@CoreModule(@PEGIUM_NEW_CLASS@CoreServices &services);

/// Builds the core-only @PEGIUM_NEW_CLASS@ language services.
std::unique_ptr<@PEGIUM_NEW_CLASS@CoreServices>
create@PEGIUM_NEW_CLASS@CoreServices(
    const @PEGIUM_NEW_CLASS@SharedCoreServices &sharedServices,
    std::string languageId = "@PEGIUM_NEW_LANGUAGE_ID@");

/// Registers the core-only @PEGIUM_NEW_CLASS@ services in `sharedServices`.
bool register@PEGIUM_NEW_CLASS@CoreServices(pegium::SharedCoreServices &sharedServices);

} // namespace @PEGIUM_NEW_LANGUAGE_ID@
]==])
_pegium_new_emit([==[src/@PEGIUM_NEW_LANGUAGE_ID@/core/CoreServices.hpp]==] [==[always]==] [==[#pragma once

#include <memory>

#include <@PEGIUM_NEW_LANGUAGE_ID@/core/validation/@PEGIUM_NEW_CLASS@Validator.hpp>

#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace @PEGIUM_NEW_LANGUAGE_ID@ {

/// Workspace-wide shared services.
struct @PEGIUM_NEW_CLASS@SharedCoreServices : virtual pegium::SharedCoreServices {
  // define here custom shared services
};

/// Core (headless) @PEGIUM_NEW_CLASS@ language services, built from the language's
/// shared-core type. Add your language services as members.
struct @PEGIUM_NEW_CLASS@CoreServices
    : pegium::CoreServicesFor<@PEGIUM_NEW_CLASS@SharedCoreServices> {
  std::unique_ptr<validation::@PEGIUM_NEW_CLASS@Validator> validator;

  explicit @PEGIUM_NEW_CLASS@CoreServices(
      const @PEGIUM_NEW_CLASS@SharedCoreServices &sharedServices)
      : pegium::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}
};

} // namespace @PEGIUM_NEW_LANGUAGE_ID@
]==])
_pegium_new_emit([==[src/@PEGIUM_NEW_LANGUAGE_ID@/core/ast.hpp]==] [==[always]==] [==[#pragma once

#include <pegium/core/syntax-tree/AstNode.hpp>

namespace @PEGIUM_NEW_LANGUAGE_ID@::ast {

struct Person : pegium::NamedAstNode {};

struct Greeting : pegium::AstNode {
  reference<Person> person;
};

struct Model : pegium::AstNode {
  vector<pointer<Person>> persons;
  vector<pointer<Greeting>> greetings;
};

} // namespace @PEGIUM_NEW_LANGUAGE_ID@::ast
]==])
_pegium_new_emit([==[src/@PEGIUM_NEW_LANGUAGE_ID@/core/validation/@PEGIUM_NEW_CLASS@Validator.cpp]==] [==[always]==] [==[#include <@PEGIUM_NEW_LANGUAGE_ID@/core/validation/@PEGIUM_NEW_CLASS@Validator.hpp>

#include <cctype>

namespace @PEGIUM_NEW_LANGUAGE_ID@::validation {

void @PEGIUM_NEW_CLASS@Validator::checkPersonNameStartsWithCapital(
    const ast::Person &person,
    const pegium::validation::ValidationAcceptor &accept) const {
  if (person.name.empty()) {
    return;
  }
  const auto first = person.name.front();
  if (std::toupper(static_cast<unsigned char>(first)) == first) {
    return;
  }
  accept.warning(person, "Person name should start with a capital.")
      .property<&ast::Person::name>();
}

} // namespace @PEGIUM_NEW_LANGUAGE_ID@::validation
]==])
_pegium_new_emit([==[src/@PEGIUM_NEW_LANGUAGE_ID@/core/validation/@PEGIUM_NEW_CLASS@Validator.hpp]==] [==[always]==] [==[#pragma once

#include <@PEGIUM_NEW_LANGUAGE_ID@/core/ast.hpp>

#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/DefaultCoreService.hpp>
#include <pegium/core/validation/ValidationAcceptor.hpp>
#include <pegium/core/validation/ValidationRegistry.hpp>

namespace @PEGIUM_NEW_LANGUAGE_ID@::validation {

/// Language validator. It inherits `pegium::DefaultCoreService` so checks can
/// reach the language services through `services.*` (parser, references, shared
/// caches, ...) as the language grows — no refactor needed the first time a
/// check needs a sibling service or a per-workspace cache.
class @PEGIUM_NEW_CLASS@Validator final : public pegium::DefaultCoreService {
public:
  using pegium::DefaultCoreService::DefaultCoreService;

  void checkPersonNameStartsWithCapital(
      const ast::Person &person,
      const pegium::validation::ValidationAcceptor &accept) const;
};

inline void registerValidationChecks(pegium::CoreServices &services,
                                     @PEGIUM_NEW_CLASS@Validator &validator) {
  auto &registry = *services.validation.validationRegistry;
  registry.registerChecks(
      {pegium::validation::ValidationRegistry::makeValidationCheck<
          &@PEGIUM_NEW_CLASS@Validator::checkPersonNameStartsWithCapital>(
          validator)});
}

} // namespace @PEGIUM_NEW_LANGUAGE_ID@::validation
]==])
_pegium_new_emit([==[src/@PEGIUM_NEW_LANGUAGE_ID@/lsp/LspModule.cpp]==] [==[lsp]==] [==[#include <@PEGIUM_NEW_LANGUAGE_ID@/lsp/LspModule.hpp>

#include <utility>

#include <@PEGIUM_NEW_LANGUAGE_ID@/core/CoreModule.hpp>
#include <pegium/lsp/services/DefaultLspModule.hpp>

namespace @PEGIUM_NEW_LANGUAGE_ID@ {

void install@PEGIUM_NEW_CLASS@LspModule(@PEGIUM_NEW_CLASS@Services &services) {
  // Register your LSP overrides here, e.g.:
  //   services.lsp.formatter = std::make_unique<@PEGIUM_NEW_CLASS@Formatter>(services);
  (void)services;
}

std::unique_ptr<@PEGIUM_NEW_CLASS@Services>
create@PEGIUM_NEW_CLASS@LspServices(
    const @PEGIUM_NEW_CLASS@SharedServices &sharedServices,
    std::string languageId) {
  auto services = pegium::makeDefaultServices<@PEGIUM_NEW_CLASS@Services>(
      sharedServices, std::move(languageId));
  install@PEGIUM_NEW_CLASS@CoreModule(*services);
  install@PEGIUM_NEW_CLASS@LspModule(*services);
  return services;
}

bool register@PEGIUM_NEW_CLASS@LspServices(pegium::SharedServices &sharedServices) {
  // The session container is the language's own shared type; recover it to
  // build the container with its custom shared services.
  sharedServices.serviceRegistry->registerServices(
      create@PEGIUM_NEW_CLASS@LspServices(
          dynamic_cast<@PEGIUM_NEW_CLASS@SharedServices &>(sharedServices)));
  return true;
}

} // namespace @PEGIUM_NEW_LANGUAGE_ID@
]==])
_pegium_new_emit([==[src/@PEGIUM_NEW_LANGUAGE_ID@/lsp/LspModule.hpp]==] [==[lsp]==] [==[#pragma once

#include <memory>
#include <string>

#include <@PEGIUM_NEW_LANGUAGE_ID@/lsp/LspServices.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace @PEGIUM_NEW_LANGUAGE_ID@ {

/// LSP service overrides applied on top of pegium's default LSP services.
void install@PEGIUM_NEW_CLASS@LspModule(@PEGIUM_NEW_CLASS@Services &services);

/// Builds the LSP-enabled @PEGIUM_NEW_CLASS@ language services.
std::unique_ptr<@PEGIUM_NEW_CLASS@Services>
create@PEGIUM_NEW_CLASS@LspServices(
    const @PEGIUM_NEW_CLASS@SharedServices &sharedServices,
    std::string languageId = "@PEGIUM_NEW_LANGUAGE_ID@");

/// Registers the LSP-enabled @PEGIUM_NEW_CLASS@ services in `sharedServices`.
bool register@PEGIUM_NEW_CLASS@LspServices(pegium::SharedServices &sharedServices);

} // namespace @PEGIUM_NEW_LANGUAGE_ID@
]==])
_pegium_new_emit([==[src/@PEGIUM_NEW_LANGUAGE_ID@/lsp/LspServices.hpp]==] [==[lsp]==] [==[#pragma once

#include <@PEGIUM_NEW_LANGUAGE_ID@/core/CoreServices.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace @PEGIUM_NEW_LANGUAGE_ID@ {

/// Workspace-wide shared services in an LSP session.
struct @PEGIUM_NEW_CLASS@SharedServices
    : @PEGIUM_NEW_CLASS@SharedCoreServices, pegium::SharedServices {};

/// LSP-enabled @PEGIUM_NEW_CLASS@ language services.
struct @PEGIUM_NEW_CLASS@Services final
    : pegium::ServicesFor<@PEGIUM_NEW_CLASS@CoreServices,
                          @PEGIUM_NEW_CLASS@SharedServices> {
  explicit @PEGIUM_NEW_CLASS@Services(
      const @PEGIUM_NEW_CLASS@SharedServices &sharedServices)
      : pegium::CoreServices(sharedServices), ServicesFor(sharedServices) {}
};

} // namespace @PEGIUM_NEW_LANGUAGE_ID@
]==])
_pegium_new_emit([==[src/@PEGIUM_NEW_LANGUAGE_ID@/lsp/main.cpp]==] [==[lsp]==] [==[#include <@PEGIUM_NEW_LANGUAGE_ID@/lsp/LspModule.hpp>

#include <pegium/lsp/runtime/LanguageServerRuntime.hpp>

int main(int argc, char **argv) {
  return pegium::runLanguageServerMain<
      @PEGIUM_NEW_LANGUAGE_ID@::@PEGIUM_NEW_CLASS@SharedServices>(
      argc, argv, "@PEGIUM_NEW_LANGUAGE_ID@-lsp",
      @PEGIUM_NEW_LANGUAGE_ID@::register@PEGIUM_NEW_CLASS@LspServices);
}
]==])
_pegium_new_emit([==[test/lsp/lsp_features_test.cpp]==] [==[lsp]==] [==[// LSP feature tests using the reusable `pegium::testing` harness. It builds
// a marked-up source string through the
// whole pipeline and drives the LSP services through pegium's public API, so it
// is only built when the language server is scaffolded (LSP=ON).
//
// Markers in the source: `<|>` is a cursor index, `<| … |>` marks a range.
#include <@PEGIUM_NEW_LANGUAGE_ID@/lsp/LspModule.hpp>

#include <pegium/testing/Testing.hpp>

#include <algorithm>
#include <string_view>

#include <gtest/gtest.h>
#include <lsp/types.h>

namespace {

// Builds a workspace with the @PEGIUM_NEW_CLASS@ LSP services registered.
pegium::testing::TestWorkspace make@PEGIUM_NEW_CLASS@Workspace() {
  auto ws = pegium::testing::TestWorkspace::withShared<
      @PEGIUM_NEW_LANGUAGE_ID@::@PEGIUM_NEW_CLASS@SharedServices>();
  ws.registerLanguage(@PEGIUM_NEW_LANGUAGE_ID@::create@PEGIUM_NEW_CLASS@LspServices(
      ws.shared<@PEGIUM_NEW_LANGUAGE_ID@::@PEGIUM_NEW_CLASS@SharedServices>()));
  return ws;
}

// Validation: an unresolved cross-reference is reported as a diagnostic. The
// message is matched as a substring, so the assertion stays readable.
TEST(@PEGIUM_NEW_CLASS@Lsp, ReportsUnresolvedReference) {
  auto ws = make@PEGIUM_NEW_CLASS@Workspace();
  pegium::testing::expectValidation(
      ws, "@PEGIUM_NEW_LANGUAGE_ID@",
      {.text = "Hello Ghost!\n",
       .diagnostics = {{.message = "Could not resolve reference"}}});
}

// Completion: at the cursor of a greeting, the names of the declared persons are
// proposed. `.check` lets us assert a subset without pinning the full list.
TEST(@PEGIUM_NEW_CLASS@Lsp, CompletesDeclaredNames) {
  auto ws = make@PEGIUM_NEW_CLASS@Workspace();
  pegium::testing::expectCompletion(
      ws, "@PEGIUM_NEW_LANGUAGE_ID@",
      {.text = "person John\nperson Jane\nHello <|>",
       .check = [](const ::lsp::CompletionList &list) {
         const auto proposes = [&](std::string_view label) {
           return std::ranges::any_of(list.items, [&](const auto &item) {
             return item.label == label;
           });
         };
         EXPECT_TRUE(proposes("John"));
         EXPECT_TRUE(proposes("Jane"));
       }});
}

} // namespace
]==])
_pegium_new_emit([==[test/parsing_test.cpp]==] [==[always]==] [==[#include <@PEGIUM_NEW_LANGUAGE_ID@/core/CoreModule.hpp>
#include <pegium/cli/CliUtils.hpp>

#include <gtest/gtest.h>

TEST(@PEGIUM_NEW_CLASS@Parsing, SampleParsesWithoutErrors) {
  auto sharedServices = pegium::make_shared_services<
      @PEGIUM_NEW_LANGUAGE_ID@::@PEGIUM_NEW_CLASS@SharedCoreServices>();
  auto &shared = *sharedServices;
  auto services = @PEGIUM_NEW_LANGUAGE_ID@::create@PEGIUM_NEW_CLASS@CoreServices(shared);
  auto &langServices = *services;
  shared.serviceRegistry->registerServices(std::move(services));

  auto document = pegium::build_document_from_path(
      PEGIUM_NEW_SAMPLE_PATH, langServices);
  EXPECT_FALSE(pegium::has_error_diagnostics(*document));
}
]==])
_pegium_new_emit([==[vscode/esbuild.mjs]==] [==[vscode]==] [==[import { rmSync } from 'node:fs';
import esbuild from 'esbuild';

const production = process.argv.includes('--production');
const watch = process.argv.includes('--watch');

// Start from a clean output dir so stale artifacts (e.g. a dev sourcemap, or
// test files compiled by tsc) never leak into a production bundle or the .vsix.
if (!watch) {
  rmSync('out', { recursive: true, force: true });
}

// Bundle the extension into a single CommonJS file. Only `vscode` is external
// (the editor injects it at runtime); everything else — including
// vscode-languageclient — is bundled in, so the packaged .vsix is self-contained.
const context = await esbuild.context({
  entryPoints: ['src/extension.ts'],
  bundle: true,
  format: 'cjs',
  platform: 'node',
  target: 'node18',
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
_pegium_new_emit([==[vscode/language-configuration.json]==] [==[vscode]==] [==[{
    "comments": {
        "lineComment": "//",
        "blockComment": [ "/*", "*/" ]
    },
    "brackets": [
        ["{", "}"],
        ["[", "]"],
        ["(", ")"]
    ],
    "autoClosingPairs": [
        ["{", "}"],
        ["[", "]"],
        ["(", ")"],
        ["\"", "\""],
        ["'", "'"]
    ],
    "surroundingPairs": [
        ["{", "}"],
        ["[", "]"],
        ["(", ")"],
        ["\"", "\""],
        ["'", "'"]
    ]
}
]==])
_pegium_new_emit([==[vscode/package.json]==] [==[vscode]==] [==[{
  "name": "@PEGIUM_NEW_LANGUAGE_ID@-dsl",
  "publisher": "@PEGIUM_NEW_LANGUAGE_ID@",
  "displayName": "@PEGIUM_NEW_CLASS@ DSL",
  "version": "0.1.0",
  "description": "@PEGIUM_NEW_CLASS@ language powered by Pegium",
  "license": "MIT",
  "engines": {
    "vscode": "^1.86.0"
  },
  "categories": [
    "Programming Languages"
  ],
  "activationEvents": [
    "onLanguage:@PEGIUM_NEW_LANGUAGE_ID@"
  ],
  "main": "./out/extension.js",
  "contributes": {
    "languages": [
      {
        "id": "@PEGIUM_NEW_LANGUAGE_ID@",
        "aliases": [
          "@PEGIUM_NEW_CLASS@",
          "@PEGIUM_NEW_LANGUAGE_ID@"
        ],
        "extensions": [
          "@PEGIUM_NEW_EXT@"
        ],
        "configuration": "./language-configuration.json"
      }
    ],
    "grammars": [
      {
        "language": "@PEGIUM_NEW_LANGUAGE_ID@",
        "scopeName": "source.@PEGIUM_NEW_LANGUAGE_ID@",
        "path": "./syntaxes/@PEGIUM_NEW_LANGUAGE_ID@.tmLanguage.json"
      }
    ],
    "configuration": {
      "title": "@PEGIUM_NEW_CLASS@",
      "properties": {
        "@PEGIUM_NEW_LANGUAGE_ID@.server.path": {
          "type": "string",
          "default": "",
          "description": "Absolute path to the @PEGIUM_NEW_LANGUAGE_ID@-lsp executable. If empty, it is looked up under ../build relative to the extension directory (including the Debug/ and Release/ subdirectories that multi-config generators use)."
        }
      }
    }
  },
  "scripts": {
    "check-types": "tsc --noEmit -p ./tsconfig.json",
    "compile": "npm run check-types && node esbuild.mjs",
    "watch": "node esbuild.mjs --watch",
    "vscode:prepublish": "npm run check-types && node esbuild.mjs --production",
    "package": "node scripts/package.mjs",
    "publish": "node scripts/package.mjs --publish"
  },
  "files": [
    "package.json",
    "out",
    "bin",
    "language-configuration.json",
    "syntaxes"
  ],
  "vsce": {
    "dependencies": false
  },
  "dependencies": {
    "vscode-languageclient": "~9.0.1"
  },
  "devDependencies": {
    "@types/node": "^20.17.17",
    "@types/vscode": "^1.86.0",
    "@vscode/vsce": "^3.2.0",
    "esbuild": "^0.24.0",
    "typescript": "^5.7.2"
  }
}
]==])
_pegium_new_emit([==[vscode/scripts/package.mjs]==] [==[vscode]==] [==[// Build an optimized, platform-specific .vsix that bundles the native server.
//
//   npm run package                       # for the host platform
//   npm run package -- --target linux-x64 # for a specific VS Code target
//   npm run publish                        # package + publish (needs VSCE_PAT)
//
// The server is built Release and stripped, in a dedicated tree kept apart from
// the Debug build/ that F5 uses, so the bundled binary is small and fast. The
// C++ server can rarely be cross-compiled, so each platform's .vsix is normally
// built on that platform's CI runner — see .github/workflows/release.yml.
import { execFileSync } from 'node:child_process';
import { chmodSync, copyFileSync, existsSync, mkdirSync, readFileSync, statSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createVSIX, publishVSIX } from '@vscode/vsce';

const extensionDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const projectRoot = path.resolve(extensionDir, '..');
const buildDir = path.join(projectRoot, 'build-release');
const pkg = JSON.parse(readFileSync(path.join(extensionDir, 'package.json'), 'utf8'));

const serverName = '@PEGIUM_NEW_LANGUAGE_ID@-lsp';
const isWindows = process.platform === 'win32';
const exe = isWindows ? `${serverName}.exe` : serverName;

const argv = process.argv.slice(2);
const doPublish = argv.includes('--publish');
const ti = argv.indexOf('--target');
const target = ti !== -1 ? argv[ti + 1] : hostTarget();

function hostTarget() {
    const os = isWindows ? 'win32' : process.platform === 'darwin' ? 'darwin' : 'linux';
    const arch =
        process.arch === 'x64' ? 'x64' : process.arch === 'arm64' ? 'arm64' : process.arch === 'arm' ? 'armhf' : process.arch;
    return `${os}-${arch}`;
}

function run(cmd, args) {
    execFileSync(cmd, args, { stdio: 'inherit' });
}

// Build the server Release. PEGIUM_OPTIMIZE_SIZE turns on LTO, hidden symbol
// visibility, dead-code stripping and identical-code folding to shrink the
// bundled server on GCC/Clang (using lld/gold/mold when present for folding); it
// is a harmless no-op on MSVC, which ships its symbols in a separate .pdb we do
// not bundle, so it is passed unconditionally.
console.log(`Building ${serverName} (Release)...`);
const configure = [
    '-S', projectRoot, '-B', buildDir,
    '-DCMAKE_BUILD_TYPE=Release', '-DPEGIUM_OPTIMIZE_SIZE=ON',
];
run('cmake', configure);
run('cmake', ['--build', buildDir, '--config', 'Release', '--target', serverName, '-j']);

let server;
for (const config of ['', 'Release', 'RelWithDebInfo']) {
    const candidate = path.join(buildDir, config, exe);
    if (existsSync(candidate)) {
        server = candidate;
        break;
    }
}
if (!server) {
    console.error(`Build did not produce ${exe}.`);
    process.exit(1);
}

// Bundle the server inside the extension so the .vsix is self-contained.
const binDir = path.join(extensionDir, 'bin');
mkdirSync(binDir, { recursive: true });
const dest = path.join(binDir, exe);
copyFileSync(server, dest);
if (!isWindows) {
    chmodSync(dest, 0o755);
    // Stripping symbols is the dominant size win (the Windows .exe keeps them in
    // a separate .pdb that we do not ship, so it is already lean).
    try {
        run('strip', [dest]);
    } catch {
        console.warn('strip not found — shipping an unstripped server');
    }
}
console.log(`Bundled ${exe}: ${(statSync(dest).size / 1048576).toFixed(1)} MB`);

const vsix = path.join(extensionDir, `${pkg.name}-${target}-${pkg.version}.vsix`);
await createVSIX({ cwd: extensionDir, target, packagePath: vsix, dependencies: false, allowMissingRepository: true });
console.log(`Packaged ${path.relative(projectRoot, vsix)} (target ${target})`);

if (doPublish) {
    await publishVSIX(vsix, { target });
    console.log(`Published ${target}`);
}
]==])
_pegium_new_emit([==[vscode/src/extension.ts]==] [==[vscode]==] [==[import * as childProcess from 'node:child_process';
import * as fs from 'node:fs';
import * as net from 'node:net';
import * as path from 'node:path';
import * as vscode from 'vscode';
import type { LanguageClientOptions, ServerOptions, StreamInfo } from 'vscode-languageclient/node';
import { LanguageClient } from 'vscode-languageclient/node';

let client: LanguageClient | undefined;
let outputChannel: vscode.OutputChannel | undefined;
let serverProcess: childProcess.ChildProcess | undefined;

function resolveServerCommand(context: vscode.ExtensionContext): string | undefined {
    const configured = vscode.workspace
        .getConfiguration()
        .get<string>('@PEGIUM_NEW_LANGUAGE_ID@.server.path');
    if (configured && configured.trim()) {
        return configured.trim();
    }

    const defaultName = process.platform === 'win32' ? '@PEGIUM_NEW_LANGUAGE_ID@-lsp.exe' : '@PEGIUM_NEW_LANGUAGE_ID@-lsp';

    // A packaged (published) extension ships the server next to it in bin/.
    const bundled = path.join(context.extensionPath, 'bin', defaultName);
    if (fs.existsSync(bundled)) {
        return makeExecutable(path.resolve(bundled));
    }

    // In development the server is built by CMake: single-config generators
    // (Ninja/Make) put it at the build root, multi-config ones (Visual Studio)
    // in a per-config subdirectory.
    const buildRoot = path.join(context.extensionPath, '..', 'build');
    for (const config of ['', 'Debug', 'Release', 'RelWithDebInfo']) {
        const candidate = path.join(buildRoot, config, defaultName);
        if (fs.existsSync(candidate)) {
            return path.resolve(candidate);
        }
    }

    return undefined;
}

// Packing into a .vsix can drop the executable bit, so restore it for the
// bundled server on POSIX platforms (no-op on Windows).
function makeExecutable(file: string): string {
    if (process.platform !== 'win32') {
        try {
            fs.chmodSync(file, 0o755);
        } catch {
            // best effort — the file may already be executable
        }
    }
    return file;
}

async function allocatePort(): Promise<number> {
    const probe = net.createServer();
    await new Promise<void>((resolve, reject) => {
        probe.once('error', reject);
        probe.listen(0, '127.0.0.1', () => resolve());
    });

    const address = probe.address();
    const port = typeof address === 'object' && address ? address.port : 0;
    await new Promise<void>((resolve, reject) => {
        probe.close((error) => {
            if (error) {
                reject(error);
                return;
            }
            resolve();
        });
    });

    if (!port) {
        throw new Error('Unable to allocate a localhost port for the @PEGIUM_NEW_CLASS@ server.');
    }
    return port;
}

function delay(milliseconds: number): Promise<void> {
    return new Promise((resolve) => {
        setTimeout(resolve, milliseconds);
    });
}

function stopServerProcess(): void {
    if (!serverProcess) {
        return;
    }
    const runningProcess = serverProcess;
    serverProcess = undefined;
    if (runningProcess.exitCode === null && !runningProcess.killed) {
        runningProcess.kill();
    }
}

function attachServerLogs(process: childProcess.ChildProcess, channel: vscode.OutputChannel): void {
    process.stdout?.on('data', (chunk: Buffer | string) => {
        channel.append(typeof chunk === 'string' ? chunk : chunk.toString('utf8'));
    });
    process.stderr?.on('data', (chunk: Buffer | string) => {
        channel.append(typeof chunk === 'string' ? chunk : chunk.toString('utf8'));
    });
    process.on('exit', (code, signal) => {
        channel.appendLine(`@PEGIUM_NEW_CLASS@ server exited (code=${code ?? 'null'}, signal=${signal ?? 'null'})`);
        if (serverProcess === process) {
            serverProcess = undefined;
        }
    });
    process.on('error', (error) => {
        channel.appendLine(`@PEGIUM_NEW_CLASS@ server process error: ${error.message}`);
        if (serverProcess === process) {
            serverProcess = undefined;
        }
    });
}

async function connectToSocket(port: number): Promise<net.Socket> {
    return await new Promise<net.Socket>((resolve, reject) => {
        const socket = net.createConnection({ host: '127.0.0.1', port });
        socket.once('connect', () => {
            socket.setNoDelay(true);
            resolve(socket);
        });
        socket.once('error', (error) => {
            socket.destroy();
            reject(error);
        });
    });
}

async function waitForServerSocket(
    port: number,
    process: childProcess.ChildProcess,
    channel: vscode.OutputChannel
): Promise<StreamInfo> {
    const deadline = Date.now() + 10_000;

    while (Date.now() < deadline) {
        if (process.exitCode !== null) {
            throw new Error(`@PEGIUM_NEW_CLASS@ server exited before accepting socket connections (code=${process.exitCode}).`);
        }

        try {
            const socket = await connectToSocket(port);
            channel.appendLine(`Connected to @PEGIUM_NEW_CLASS@ server on 127.0.0.1:${port}`);
            return { reader: socket, writer: socket };
        } catch {
            await delay(100);
        }
    }

    throw new Error(`Timed out while connecting to @PEGIUM_NEW_CLASS@ server on 127.0.0.1:${port}.`);
}

function createServerOptions(serverCommand: string, channel: vscode.OutputChannel): ServerOptions {
    return async () => {
        stopServerProcess();

        const port = await allocatePort();
        channel.appendLine(`Starting @PEGIUM_NEW_CLASS@ server: ${serverCommand} --port=${port}`);

        const child = childProcess.spawn(serverCommand, [`--port=${port}`], {
            cwd: path.dirname(serverCommand),
            env: process.env,
            stdio: ['ignore', 'pipe', 'pipe']
        });
        serverProcess = child;
        attachServerLogs(child, channel);

        try {
            return await waitForServerSocket(port, child, channel);
        } catch (error) {
            stopServerProcess();
            throw error;
        }
    };
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
    const serverCommand = resolveServerCommand(context);
    if (!serverCommand) {
        void vscode.window.showErrorMessage(
            'Unable to locate @PEGIUM_NEW_LANGUAGE_ID@-lsp. Build the project and/or set @PEGIUM_NEW_LANGUAGE_ID@.server.path.'
        );
        return;
    }

    outputChannel = vscode.window.createOutputChannel('@PEGIUM_NEW_CLASS@');
    context.subscriptions.push(outputChannel);
    context.subscriptions.push(new vscode.Disposable(() => {
        stopServerProcess();
    }));

    const serverOptions = createServerOptions(serverCommand, outputChannel);
    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: '@PEGIUM_NEW_LANGUAGE_ID@' }],
        outputChannel
    };

    client = new LanguageClient(
        '@PEGIUM_NEW_LANGUAGE_ID@',
        '@PEGIUM_NEW_CLASS@',
        serverOptions,
        clientOptions
    );

    await client.start();
}

export async function deactivate(): Promise<void> {
    if (client) {
        const runningClient = client;
        client = undefined;
        await runningClient.stop();
    }
    stopServerProcess();
}
]==])
_pegium_new_emit([==[vscode/syntaxes/@PEGIUM_NEW_LANGUAGE_ID@.tmLanguage.json]==] [==[vscode]==] [==[{
  "name": "@PEGIUM_NEW_LANGUAGE_ID@",
  "scopeName": "source.@PEGIUM_NEW_LANGUAGE_ID@",
  "fileTypes": [
    "@PEGIUM_NEW_EXT@"
  ],
  "patterns": [
    {
      "include": "#comments"
    },
    {
      "name": "keyword.control.@PEGIUM_NEW_LANGUAGE_ID@",
      "match": "\\b(person|Hello)\\b"
    }
  ],
  "repository": {
    "comments": {
      "patterns": [
        {
          "name": "comment.block.@PEGIUM_NEW_LANGUAGE_ID@",
          "begin": "/\\*",
          "beginCaptures": {
            "0": {
              "name": "punctuation.definition.comment.@PEGIUM_NEW_LANGUAGE_ID@"
            }
          },
          "end": "\\*/",
          "endCaptures": {
            "0": {
              "name": "punctuation.definition.comment.@PEGIUM_NEW_LANGUAGE_ID@"
            }
          }
        },
        {
          "begin": "//",
          "beginCaptures": {
            "1": {
              "name": "punctuation.whitespace.comment.leading.@PEGIUM_NEW_LANGUAGE_ID@"
            }
          },
          "end": "(?=$)",
          "name": "comment.line.@PEGIUM_NEW_LANGUAGE_ID@"
        }
      ]
    }
  }
}
]==])
_pegium_new_emit([==[vscode/tsconfig.json]==] [==[vscode]==] [==[{
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

_pegium_new_finish()
