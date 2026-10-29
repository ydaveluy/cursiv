// What one published action states, read once from its row type: the indices
// the AST runs are settled by, and the names the record facet answers with.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstMetaData.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

// What a text converter is handed: nothing, the record's text or its parts.
enum class TextInput : std::uint8_t { none, text, parts };

struct AstRowGen {
  WriteKind kind = WriteKind::create;
  std::string_view feature;
  std::string_view rule;
  // The class the row is declared on: the one a creation, a close or a nest
  // names.
  std::uint16_t declType = detail::kNoIndex;
  std::uint16_t fieldSlot = kNoField;
  std::uint16_t convSlot = detail::kNoIndex;
  std::uint8_t narrowSlot = kNoNarrow;
  // Converter rules, spelled as aliases into the grammar's bindings: `void`
  // for a source that names no rule; an override only where one fills the
  // rule's slot.
  std::string_view convName;
  std::string_view convOver;
  std::size_t convRank = 0;
  bool convJoins = false;
  bool convTextParts = true;
  bool convParts = false;
  // A write whose payload passes on the value its record holds.
  bool convPasses = false;
  bool dataRule = false;
  std::string_view textConv;
  std::string_view textOver;
  TextInput textInput = TextInput::none;
  // A conversion row: the rule passes its value on, or converts to a value
  // other than text, which `valueRule` names.
  bool passes = false;
  bool valued = false;
  std::string_view valueRule;

  // The kind the record answers the enclosing region with: the switch names
  // every `WriteKind`, so a new one is a -Wswitch diagnostic here.
  constexpr ActKind act() const {
    switch (kind) {
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
  // Hands a node back: an opening, or a close, which adopts what a call
  // inside built.
  constexpr bool makes() const {
    return opens() || kind == WriteKind::close;
  }
  // Opens a node, not merely hands one back: a close never begins a second
  // node over the span.
  constexpr bool opens() const {
    return kind == WriteKind::create || kind == WriteKind::climb;
  }
  constexpr bool writes() const {
    return kind == WriteKind::assign || kind == WriteKind::append;
  }
  constexpr bool effects() const {
    return kind == WriteKind::nest || kind == WriteKind::flag;
  }
  constexpr bool converts() const { return kind == WriteKind::convert; }
};

}  // namespace cursiv
