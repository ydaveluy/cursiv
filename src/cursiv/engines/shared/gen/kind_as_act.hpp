// The kind a record answers the enclosing region with: a new `WriteKind` is a
// compile error here, for both engines.
#pragma once

#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

consteval ActKind kind_as_act(WriteKind k) {
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
      return ActKind::none;
  }
}
template <WriteKind K>
inline constexpr ActKind kKindAsAct = kind_as_act(K);

}  // namespace cursiv
