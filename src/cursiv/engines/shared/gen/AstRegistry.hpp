// The AST registries of one grammar as plain data: the one place the emitters
// read a grammar's types, members and rows, so that everything past it is
// compiled once.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/syntax-tree/AstMetaData.hpp"
#include "cursiv/engines/shared/gen/AstRowGen.hpp"

namespace cursiv {

struct AstRegistry {
  AstRegistry() = default;
  AstRegistry(AstRegistry&&) = default;
  AstRegistry& operator=(AstRegistry&&) = default;

  struct Type {
    std::string_view name;
    bool built = false;
    std::string source;
  };
  struct Field {
    std::string_view ptr;
    std::vector<std::string> narrows;
    std::ptrdiff_t offset = 0;
  };

  std::vector<AstRowGen> rows;
  std::vector<Type> types;
  std::vector<Field> fields;
  std::vector<detail::FieldDesc> descs;
  // The targets every `descs` entry points into.
  std::vector<std::uint16_t> targets;
  std::vector<std::uint64_t> derives;
  std::vector<std::uint64_t> accepts;
  std::size_t typeWords = 0;
  std::size_t convCount = 0;
  std::string_view embeds;
  bool boundTypes = false;
  // Emitted rule spellings borrowed by rows; deque growth preserves their views.
  std::deque<std::string> sources;
  bool anyNest = false;
  bool anyRef = false;
  bool anyVariantField = false;
};

}  // namespace cursiv
