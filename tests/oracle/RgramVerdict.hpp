// What the oracle reads in a document: whether the grammar derives it, and
// its tree where it has exactly one.
#pragma once

#include <string>

namespace cursiv {

struct RgramVerdict {
  bool accepted = false;
  // Spelled as `test::ast_of` spells a tree; empty where the text has
  // several, or where no tree was asked for.
  std::string tree{};
};

}  // namespace cursiv
