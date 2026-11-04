// How far one name is from another, counting a transposition as one fault:
// what a misspelling costs to become a name in scope.
#pragma once

#include <cstddef>
#include <string_view>

namespace cursiv {

// The optimal string alignment distance, over bytes. Names past 64 bytes are
// not compared; a distance past `limit` returns its saturating successor.
[[nodiscard]] std::size_t edit_distance(std::string_view a, std::string_view b,
                                        std::size_t limit);

}  // namespace cursiv
