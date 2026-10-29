// The kind a record answers the enclosing region with: the switch names every
// `WriteKind`, so a new one is a -Wswitch diagnostic here, for both engines.
#pragma once

#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

constexpr ActKind kind_as_act(WriteKind k) {
  switch (k) {
    case WriteKind::create:
    case WriteKind::close:
    case WriteKind::climb:
      return ActKind::create;
    case WriteKind::assign:
      return ActKind::assign;
    case WriteKind::append:
      return ActKind::append;
    case WriteKind::nest:
      return ActKind::nest;
    case WriteKind::flag:
      return ActKind::flag;
    case WriteKind::convert:
      break;
  }
  return ActKind::none;
}

}  // namespace cursiv
