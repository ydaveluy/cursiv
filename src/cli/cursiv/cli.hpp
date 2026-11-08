// Command-line surface for a language: parses arguments, builds the
// document, and reports the verdict. Works on an erased `Document`, so
// nothing here is templated on a grammar.
#pragma once

#include <concepts>
#include <functional>
#include <ostream>
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

/**
 * Scans `root` into the workspace, as an editor's `initialized`
 * notification would, so cross-file references resolve the same way.
 * Throws `UsageError` when `root` is not a directory.
 */
void initialize_root(std::string_view root, cursiv::SharedCoreServices& shared);

/**
 * Reads `path`, adds it to the workspace, and builds it fully so its
 * diagnostics are complete. Throws `UsageError` when no language claims
 * the file or the path cannot be read. The returned reference remains valid
 * while the document is held in `shared.documents`.
 */
[[nodiscard]] cursiv::Document& build_document_from_path(
    std::string_view path, cursiv::SharedCoreServices& shared);

[[nodiscard]] bool has_error_diagnostics(
    const cursiv::Document& document) noexcept;

/**
 * Prints one `path:line:column: severity: message [source:code]` line per
 * diagnostic, sorted by range. Line and column are 1-based byte offsets,
 * not the LSP layer's UTF-16 columns.
 */
void print_diagnostics(const cursiv::Document& document,
                       std::string_view displayPath, std::ostream& out);

/**
 * Runs a command of the language's own, such as `generate`, with the exit
 * codes of `run_cli`: `command`'s answer once stdout is flushed, `kUsage` for
 * a `UsageError`, and `kFatal` for any other exception or a failed write.
 * The error is printed on stderr.
 */
[[nodiscard]] int run_command(const std::function<int()>& command);

namespace detail {

// Non-templated so this compiles once; the template below only picks the
// shared container's type. Registers the languages before parsing the
// arguments, so the usage line can name what they serve.
[[nodiscard]] int run_cli_on(
    int argc, char** argv,
    const std::function<bool(SharedCoreServices&)>& registerLanguageServices,
    SharedCoreServices& sharedServices);

}  // namespace detail

/**
 * `TShared` is the workspace container the run is made on; a language
 * with its own workspace-wide services passes its own type. One
 * registrar may install several languages, a project that declares two
 * grammars over one workspace being the case that needs it.
 */
template <class TShared = SharedCoreServices>
  requires std::derived_from<TShared, SharedCoreServices>
[[nodiscard]] int run_cli(
    int argc, char** argv,
    const std::function<bool(SharedCoreServices&)>& registerLanguageServices) {
  TShared sharedServices;
  return detail::run_cli_on(argc, argv, registerLanguageServices,
                            sharedServices);
}

}  // namespace cursiv
