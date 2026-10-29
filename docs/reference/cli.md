# Command line

The generated CLI checks a document, prints its syntax tree, or validates
files from a script:

```bash
mylang_cli [--dump] [--json] [--root <dir>] [--help] <file>
```

| Option | Effect |
|---|---|
| `--dump` | Print the CST as text on stdout. |
| `--json` | Print the CST as JSON on stdout; takes precedence over `--dump`. |
| `--root <dir>` | Load supported files from a workspace folder before validating the file. |
| `--help`, `-h` | Print usage and the registered file extensions. |

Diagnostics and the final `: ok` or `: not clean` go to stderr, so a tree on
stdout can be piped. Lines and columns are 1-based; columns count bytes.

## Exit codes

| Code | Means |
|---|---|
| `0` | No error diagnostic (warnings allowed), or help was requested |
| `1` | At least one error diagnostic |
| `2` | Invalid arguments, unreadable path, unsupported file, or a file of 4 GiB or more |
| `3` | An internal error or output failure |

## Load a workspace {#what-root-changes}

Without `--root`, only the named file is loaded. Pass the workspace folder when
references target declarations in other files:

```bash
./build/src/mylang_cli --root example example/hello.mylang
```

The CLI reads saved files only, unlike an editor session.

## Inspect the syntax tree {#reading-the-tree}

`--dump` shows which rules and tokens matched, with source offsets. Trivia
declared `hidden`, such as comments, appears; `ignored` trivia does not. The
text dump abbreviates long leaves; `--json` keeps their full text.

## Customize the CLI {#writing-your-own}

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
