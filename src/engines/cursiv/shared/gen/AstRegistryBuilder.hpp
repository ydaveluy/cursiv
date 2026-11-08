// The registries of a grammar's AST, settled from the rows it publishes. A row
// names its class, member, conversion and narrowing by the address of a
// constant descriptor; the indices are assigned here, once: built classes,
// named families, targets, then owners.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/shared/gen/AstRegistry.hpp"
#include "cursiv/shared/gen/AstRowGen.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

namespace cursiv::engine {

// A type is told by its address alone; its tag, where it is a node class,
// answers subtyping.
struct AstTypeDesc {
  const AstTypeTag* tag = nullptr;
};

// A conversion is told by its address alone: the payload source and the type
// it lands in.
struct AstConvDesc {};

struct AstFieldDesc {
  std::string_view name;
  std::ptrdiff_t (*offset)() = nullptr;
  FieldNature nature = FieldNature::value;
  FieldStorage storage = FieldStorage::direct;
  const AstTypeDesc* owner = nullptr;
  const AstTypeDesc* const* targets = nullptr;
  std::uint32_t targetCount = 0;
};

struct AstRowDesc {
  AstRowGen gen{};
  // The class every row but a conversion is declared on: the one a creation,
  // a close or a nest names.
  const AstTypeDesc* decl = nullptr;
  bool built = false;
  bool named = false;
  const AstFieldDesc* field = nullptr;
  const AstConvDesc* conv = nullptr;
  // The class a site narrows its member to.
  const AstTypeDesc* narrow = nullptr;
  const AstTypeDesc* convType = nullptr;
  const AstTypeDesc* textType = nullptr;
  const AstTypeDesc* valueType = nullptr;
  // Whether an override fills the converter slot of `convType`, of `textType`.
  bool convOverride = false;
  bool textOverride = false;
};

AstRegistry build_ast_registry(std::span<const AstRowDesc* const> rows,
    std::span<const AstTypeDesc* const> bindings,
    std::span<const AstTypeDesc* const> rules, std::string_view grammar);

}  // namespace cursiv::engine
