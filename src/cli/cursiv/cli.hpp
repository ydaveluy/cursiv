// Command-line support for a language: parses arguments, builds the document,
// and reports the verdict.
#pragma once

#include <concepts>
#include <functional>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

// Exit code is the verdict: 0 clean, 1 the document has an error
// diagnostic, 2 misuse (bad arguments or path), 3 an internal or output error.
inline constexpr int kClean = 0;
inline constexpr int kDiagnostics = 1;
inline constexpr int kUsage = 2;
inline constexpr int kFatal = 3;

// Misuse, not a document fault: its own type so it maps to `kUsage`
// instead of being reported as `kFatal`.
class UsageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Loads a workspace folder for cross-file references. File system errors
// from the workspace manager are reported as `UsageError`.
void initializeRoot(std::string_view root, cursiv::SharedCoreServices& shared);

// Reads `path`, adds it to the workspace and builds it fully. Throws
// `UsageError` when no language claims the file or it cannot be read. The
// reference stays valid while `shared.documents` holds the document.
[[nodiscard]] cursiv::Document& buildDocumentFromPath(
    std::string_view path, cursiv::SharedCoreServices& shared);

[[nodiscard]] bool hasErrorDiagnostics(
    const cursiv::Document& document) noexcept;

// Prints one `path:line:column: severity: message [source:code]` line per
// diagnostic, sorted by range; line and column are 1-based bytes.
void printDiagnostics(const cursiv::Document& document,
                       std::string_view displayPath, std::ostream& out);

// Runs a language's own command, such as `generate`, with `runCli`'s exit
// codes: `command`'s answer, `kUsage` for a `UsageError`, `kFatal` for any
// other exception or a failed write. Errors go to stderr.
[[nodiscard]] int runCommand(const std::function<int()>& command);

// A file command runs on the built document. It may accept an output directory
// and load the file's folder when the caller omits --root.
struct CliCommand {
  std::string_view name;
  std::function<int(const Document&, std::string_view path,
                    std::optional<std::string_view> destination)>
      run;
  bool acceptsDestination = false;
  bool loadSiblings = false;
};

namespace detail {

// Registers the languages before parsing the arguments, so the usage line can
// name what they serve.
[[nodiscard]] int run_cli_on(
    int argc, char** argv,
    const std::function<void(SharedCoreServices&)>& registerLanguageServices,
    SharedCoreServices& sharedServices,
    std::span<const CliCommand> commands = {});

}  // namespace detail

// `TShared` is the workspace container; a language with its own
// workspace-wide services passes its type. One registrar may install several
// languages over one workspace.
template <class TShared = SharedCoreServices>
  requires std::derived_from<TShared, SharedCoreServices>
[[nodiscard]] int runCli(
    int argc, char** argv,
    const std::function<void(SharedCoreServices&)>& registerLanguageServices,
    std::initializer_list<CliCommand> commands = {}) {
  return runCommand([&] {
    TShared sharedServices;
    return detail::run_cli_on(argc, argv, registerLanguageServices,
                              sharedServices, commands);
  });
}

}  // namespace cursiv
