// Command-line surface for a language: parses arguments, builds the
// document, and reports the verdict. Works on an erased `Document`, so
// nothing here is templated on a grammar.
#pragma once

#include <concepts>
#include <functional>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/workspace/Document.hpp"

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

// `help` wins over every other flag on the line. `root`, when set, is
// scanned into the workspace before the file is judged.
struct Invocation {
  std::string path;
  std::string root;
  bool dump = false;
  bool json = false;
  bool help = false;
};

/**
 * Parses the command line. Returns `std::nullopt` when the arguments do
 * not form a run, in which case the caller prints the usage and answers
 * `kUsage`. `--help` always returns a value, never the empty answer.
 */
[[nodiscard]] std::optional<Invocation> parse_args(int argc, char** argv);

/**
 * Prints the usage line, including the extensions `registry`'s languages
 * serve.
 */
void print_usage(std::string_view program,
                 const cursiv::ServiceRegistry& registry, std::ostream& out);

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
 * Prints the concrete syntax tree, indented, in source order. A node is named
 * by the AST class it built, or else by its terminal or data rule. `grammar`
 * is separate because the tree itself does not know what its element ids
 * mean.
 */
void dump_cst(const cursiv::Document& document, const cursiv::Grammar& grammar,
              std::ostream& out);

/**
 * The same tree as JSON, one line, per node `element`/`begin`/`end` plus
 * `type` (the AST class built there), `hidden`/`recovered` when set and
 * `text` on a leaf. Unlike `dump_cst`,
 * leaf text is not truncated, since this form is meant to be re-parsed.
 * Ill-formed UTF-8 is replaced by U+FFFD in the output only.
 */
void dump_cst_json(const cursiv::Document& document,
                   const cursiv::Grammar& grammar, std::ostream& out);

/**
 * Prints the tree (`--json` wins over `--dump` if both are set) to `out`,
 * and diagnostics plus the verdict line to `err`, so a script piping the
 * tree does not see them mixed in.
 * Flushes the output and throws std::ios_base::failure if writing fails.
 */
[[nodiscard]] int report(const cursiv::Document& document,
                         const cursiv::Grammar& grammar,
                         std::string_view displayPath, bool dump, bool json,
                         std::ostream& out, std::ostream& err);

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
