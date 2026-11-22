#pragma once

#include <optional>
#include <string>
#include <string_view>

// URIs, in the one form everything else compares and keys on. Two spellings of
// the same file must normalize to the same string, or a document is opened
// twice and linked to itself.

namespace cursiv {

// Stable for comparisons and map keys: a local file is `file:///path`, a
// leading drive letter is lowered with its colon escaped, and the scheme and
// host are lower case.
[[nodiscard]] std::string normalizeUri(std::string_view uri);

[[nodiscard]] bool isFileUri(std::string_view uri);

// The local path a `file:` URI names, if it names one.
[[nodiscard]] std::optional<std::string> fileUriToPath(std::string_view uri);

// A relative path is taken against the working directory. An empty path names
// no file: `std::invalid_argument`.
[[nodiscard]] std::string pathToFileUri(std::string_view path);

// Leading-dot form, lower-cased: `"Calc"` -> `".calc"`. Empty passes through.
// The registration side of `uriExtension`'s convention -- a metadata spelling
// and a URI spelling must map to the same row.
[[nodiscard]] std::string normalizeExtension(std::string_view extension);

// The POSIX extension of the decoded path, lower-cased: none for ".env", "."
// and "..", ".ws" for ".env.ws".
[[nodiscard]] std::string uriExtension(std::string_view uri);

// The POSIX basename of the decoded path, case kept; trailing separators are
// skipped, and a path of none but them gives an empty name.
[[nodiscard]] std::string uriFileName(std::string_view uri);

[[nodiscard]] bool equalsUri(std::string_view left, std::string_view right);

// Whether `child` sits under `parent`: same scheme and authority, and the
// path segment by segment, a trailing `/` not counting.
[[nodiscard]] bool containsUri(std::string_view parent,
                                std::string_view child);

// `to` expressed relative to `from`, which names a directory, `..` segments
// included; the normalized absolute URI of `to` when the two are not in one
// scheme and authority.
[[nodiscard]] std::string relativeUri(std::string_view from,
                                       std::string_view to);

}  // namespace cursiv
