# Command line

The command-line tool of a scaffolded project checks one file and can print
its syntax tree. Use it to validate files from a script:

```bash
mylang_cli [--dump] [--json] [--root <dir>] [--help] [--] <file>
```

| Option | Effect |
|---|---|
| `--dump` | Print the CST as text on stdout. |
| `--json` | Print the CST as JSON on stdout; takes precedence over `--dump`. |
| `--root <dir>`, `-r <dir>` | Load supported files from a workspace folder before validating the file. |
| `--help`, `-h` | List commands, file names and extensions. |
| `--` | End option parsing so the input file name can start with a dash. |

Diagnostics and the final `<file>: ok` or `<file>: not clean` line go to
stderr, so you can pipe the tree from stdout. Lines and columns are 1-based;
columns count bytes.

## Exit codes

| Code | Means |
|---|---|
| `0` | No error diagnostic (warnings allowed), or help was requested |
| `1` | At least one error diagnostic |
| `2` | Invalid arguments, unreadable path, unsupported file, or a file past the document size limit (just under 4 GiB) |
| `3` | An internal error or a stream write that reports failure |

On POSIX, closing the read end of a pipe can terminate the process with
`SIGPIPE` before it returns an exit code.

## Load a workspace

Without `--root`, only the named file is loaded. Pass the workspace folder when
references target declarations in other files:

```bash
./build/src/mylang_cli --root example example/hello.mylang
```

The CLI uses the saved files on disk. If its workspace scan cannot read a
selected file or directory, it reports the path and exits with code `2`.

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

The dump includes `hidden` trivia such as comments and omits `ignored`
trivia. Use `--json` for untruncated leaf text and half-open byte ranges
(`begin`, `end`). Each node has an `element` string, empty for anonymous
elements, and a `type` when it built an AST node. The output is `null` if
there is no CST entry.

## Customize the CLI

The scaffold's `src/cli.cpp` calls `runCli` with your language registration
function:

```cpp title="src/cli.cpp"
#include "cursiv/cli.hpp"
#include "cursiv/services/CoreLanguage.hpp"
#include "mylang/CoreServices.hpp"

constexpr auto kRegister =
    &cursiv::registerLanguage<mylang::MyLangCoreServices,
                               mylang::MyLangSharedCoreServices,
                               mylang::install_core_module>;

int main(int argc, char** argv) {
  return cursiv::runCli<mylang::MyLangSharedCoreServices>(argc, argv,
                                                         kRegister);
}
```

A registration function may install several languages; see
[Several languages](../recipes/multiple-languages.md).

Pass file commands to `runCli` alongside the language registrar. Each command
receives the built document, the input path and an optional destination.
For example, the domainmodel CLI registers its Java generator like this:

```cpp
return cursiv::runCli<dm::DomainModelSharedCoreServices>(
    argc, argv, kRegister,
    {{.name = "generate", .run = generate,
      .acceptsDestination = true, .loadSiblings = true}});
```

`generate --help` lists the command's options. With `loadSiblings` the
workspace defaults to the input file's directory. An explicit `--root`
selects another directory. With `acceptsDestination`, the command accepts
`-d <dir>` and `--destination <dir>`.

For a command with its own argument syntax, `runCommand` runs a callable
with the same exit codes and checks writes to stdout and stderr.
