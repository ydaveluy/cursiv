# Examples

Choose an example to run or adapt for your own language. Each includes a
grammar, AST, services, tests, CLI and VS Code extension.

| Example | Demonstrates | Files |
|---|---|---|
| [Arithmetics](arithmetics/) | Expressions and operator precedence | `.calc` |
| [Statemachine](statemachine/) | Declarations, references and keyword documentation | `.statemachine` |
| [Domainmodel](domainmodel/) | Packages, qualified names and formatting | `.dmodel` |
| [Requirements](requirements/) | Two languages with cross-file references | `.req`, `.tst` |

## Build and run

Run from the repository root with GCC and Ninja installed. See the
[prerequisites](../docs/learn/scaffold.md) for supported compilers.

```sh
cmake --preset gcc-release
cmake --build --preset gcc-release --target statemachine_cli --parallel
./build/examples/statemachine/src/statemachine_cli \
  examples/statemachine/example/doors.statemachine
```

The CLI reports `: ok` when there are no error diagnostics. Use `--dump` to
inspect the syntax tree or `--root <folder>` to load other files for references.
See [Command line](../docs/reference/cli.md).

## Try an editor session

Install Node.js, npm and the recommended VS Code extensions, including CMake
Tools and clangd. Native servers support VS Code 1.86 or later; WASI servers
require 1.101 or later. Open the repository root, configure a CMake preset,
choose **Run Statemachine Extension** in Run and Debug, then press **F5**.

The task builds the server and client. A development window opens on the
example documents, with completion, navigation and diagnostics available.
Choose another example's launch configuration to try that language.

Each `vscode/` directory is an independent npm package. To use a specific native
server, set `cursiv.<example>.serverPath`, such as
`cursiv.statemachine.serverPath`. Otherwise the client selects a server from the
active build or packaged files.

## Package an extension

Build the threaded WASI server using the
[WebAssembly guide](../docs/recipes/wasm.md). CMake copies the module and runner
into that example's `vscode/server/` directory. Then package the client:

```sh
cd examples/statemachine/vscode
npm ci
npm run package
```

See [Editor integration](../docs/learn/editor-integration.md) for distribution
requirements.

## Advanced: test an LSP request

To inspect an editor request without launching VS Code:

```sh
cmake --build --preset gcc-release --target statemachine_lsp --parallel
node tools/lsp-session.mjs \
  build/examples/statemachine/src/statemachine_lsp \
  examples/statemachine/example/doors.statemachine --hover-at 10:2
```

The script opens the document, prints diagnostics and requests a hover. It also
supports navigation and rename; see its option list. Positions are zero-based
UTF-16 coordinates on the last document passed.
