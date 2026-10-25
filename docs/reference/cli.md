# Command line

Use the generated CLI to check a language document, inspect its syntax tree
or validate files from a script:

```bash
mylang_cli [--dump] [--json] [--root <dir>] [--help] <file>
```

| Option | Effect |
|---|---|
| `--dump` | Print the CST as text on stdout. |
| `--json` | Print the CST as JSON on stdout; takes precedence over `--dump`. |
| `--root <dir>` | Load supported files from a workspace folder before validating the file. |
| `--help`, `-h` | Print usage and the registered file extensions. |

Diagnostics and the final `: ok` or `: not clean` status go to stderr, so they
do not mix with a tree piped to another program. Diagnostic lines and columns
are 1-based; columns count bytes.

## Exit codes

| Code | Means |
|---|---|
| `0` | no error diagnostics, or help was requested |
| `1` | it carries at least one error diagnostic |
| `2` | invalid arguments, unreadable path or unsupported file |
| `3` | an internal error or output failure |

Warnings alone do not make the command fail.

## Load a workspace {#what-root-changes}

Without `--root`, only the named file is loaded. Pass a workspace folder when
references target declarations in other files:

```bash
./build/src/mylang_cli --root example example/hello.mylang
```

The CLI scans the folder before validating the file. It uses saved files;
an editor session may also contain unsaved changes.

## Inspect the syntax tree {#reading-the-tree}

`--dump` prints the CST with source offsets and retained comments. Trivia
declared as `ignore` is omitted; trivia declared as `hidden` remains visible.
Use it to check which rules and tokens matched.

`--json` preserves full leaf text, while the text dump abbreviates long leaves.

## Customize the CLI {#writing-your-own}

The scaffold already includes `cli/main.cpp`. It calls `run_cli` with your
language registration function:

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

One registration function may install several languages in the workspace. See
[Several languages](../recipes/multiple-languages.md).

## Document size

The CLI rejects documents larger than `cursiv::kMaxDocument` bytes because
source offsets use 32 bits. This is a usage error (exit code `2`).
