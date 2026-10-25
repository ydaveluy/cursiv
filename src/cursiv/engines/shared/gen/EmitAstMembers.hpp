// The member-access aliases of one grammar's node types, as C++ source: what
// the generated module names to reach a type's fields and references.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cursiv/core/syntax-tree/AstMetaData.hpp"

namespace cursiv {

// The fields in the order the classes declare them: a base's members before
// its derived class's, each class's by position in the class.
std::vector<std::size_t> member_order(std::span<const detail::FieldDesc> fields,
                                      std::span<const std::ptrdiff_t> offsets,
                                      std::span<const std::uint64_t> derives,
                                      std::size_t words);

// The same reflection tables for both engines. Spellings are already C++
// qualified; the inheritance matrix decides applicability before runtime.
std::string emit_ast_members(const std::vector<std::string>& types,
                             const std::vector<std::string>& tags,
                             std::span<const detail::FieldDesc> fields,
                             std::span<const std::size_t> order,
                             std::span<const std::uint64_t> derives,
                             std::size_t words);

}  // namespace cursiv
