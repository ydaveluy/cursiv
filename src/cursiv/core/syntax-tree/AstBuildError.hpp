#pragma once

#include <cstdint>

// Why a build produced no tree, on `ParseResult::error`. `none` with a null
// `value` cannot happen: the builder promotes it to `no_entry` on a repaired
// tape, to `malformed_cst` otherwise.

namespace cursiv {

enum class AstBuildError : std::uint8_t {
  none,
  // The entry rule matched nothing, or the repairs gave up every node it
  // would have built: fatal, there is nothing to build from.
  no_entry,
  // The document was rejected after its entry matched; the tree still exists,
  // and completion needs it.
  not_accepted,
  // A node the tree owed could not be materialised, or the records were not
  // the builder's to read.
  malformed_cst
};

}  // namespace cursiv
