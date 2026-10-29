#pragma once

#include "cursiv/core/syntax-tree/AstNode.hpp"

namespace tlexbackends {
struct Document : cursiv::Extends<cursiv::AstNode> {
  vector<string_view> items;
};
}

namespace tcaptures {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
}

namespace tthrough {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view text;
};
}

namespace trulethrough {
struct Node : cursiv::Extends<cursiv::AstNode> {
  string_view text;
  vector<string_view> parts;
};
}
