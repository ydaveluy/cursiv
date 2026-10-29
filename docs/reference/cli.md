# Command line

The command-line tool of a scaffolded project checks one file and can print
its syntax tree. Use it to validate files from a script:

```bash
mylang_cli [--dump] [--json] [--root <dir>] [--help] <file>
```

| Option | Effect |
|---|---|
| `--dump` | Print the CST as text on stdout. |
| `--json` | Print the CST as JSON on stdout; takes precedence over `--dump`. |
| `--root <dir>` | Load supported files from a workspace folder before validating the file. |
| `--help`, `-h` | Print usage and the registered file extensions. |

Diagnostics and the final `<file>: ok` or `<file>: not clean` line go to
stderr, so you can pipe the tree from stdout. Lines and columns are 1-based;
columns count bytes.

## Exit codes

| Code | Means |
|---|---|
| `0` | No error diagnostic (warnings allowed), or help was requested |
| `1` | At least one error diagnostic |
| `2` | Invalid arguments, unreadable path, unsupported file, or a file of 4 GiB or more |
| `3` | An internal error or output failure |

## Load a workspace

Without `--root`, only the named file is loaded. Pass the workspace folder when
references target declarations in other files:

```bash
./build/src/mylang_cli --root example example/hello.mylang
```

The CLI reads files from disk, so it does not see unsaved changes in an
editor.

## Inspect the syntax tree

`--dump` prints one line per node of the syntax tree, with its source offsets.
A node is named by the AST class it built, otherwise by its terminal; a
keyword or a punctuation mark shows only its text:

```text
Module [0..35)
   [0..6) 'module'
  ID [7..8) 'm'
  Definition [9..29)
```

Comments and other `hidden` trivia appear between the tokens; `ignored`
trivia does not. The text dump shortens long token text. `--json` keeps the
full text and gives each node its `element` (terminal or data rule) and its
`type` (AST class).

## Customize the CLI

The scaffold's `cli/main.cpp` calls `run_cli` with your language registration
function:

```cpp title="cli/main.cpp"
#include "cursiv/cli/cli.hpp"
#include "cursiv/core/services/CoreLanguage.hpp"
#include "mylang/core/CoreServices.hpp"

constexpr auto kRegister =
    &cursiv::register_language<mylang::MyLangCoreServices,
                               mylang::MyLangSharedCoreServices,
                               mylang::install_core_module>;

int main(int argc, char** argv) {
  return cursiv::run_cli<mylang::MyLangSharedCoreServices>(argc, argv,
                                                         kRegister);
}
```

A registration function may install several languages; see
[Several languages](../recipes/multiple-languages.md).
