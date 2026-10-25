// What a directive does to the tree, with the grammar erased: the node an
// element creates, the member an assignment writes, and a built node's
// value members read back as text. A write it cannot model is refused,
// never guessed -- the caller weighs a `false` return as nothing.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/AstArena.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

namespace cursiv::fuzzer {

// A value read back: its text, or the fact that no reader reaches it.
enum class ValueKind : std::uint8_t { text, unreadable };

// One operator of a precedence table: the element that spells it, the step it
// sits on -- tightest binds hardest -- and which way a chain of equals groups.
struct InfixOp {
  ElementId element = kNoElement;
  std::uint8_t prec = 0;
  bool right = false;
};

struct GrammarActions {
  virtual ~GrammarActions() = default;

  // The node this element creates, or null when it creates none.
  [[nodiscard]] virtual AstNode* make(ElementId, AstArena&) const = 0;
  // The operand rule of a precedence table, and its operators; `kNoElement`
  // and an empty span for every element that is not one.
  [[nodiscard]] virtual ElementId primary(ElementId) const = 0;
  [[nodiscard]] virtual std::span<const InfixOp> operators(ElementId) const = 0;
  // Left operand, operator text and right operand, into a node the same
  // element made.
  virtual bool fold(ElementId, ElementId, AstNode& node, AstNode* left,
                    std::string_view op, AstNode* right) const = 0;

  virtual bool write_node(ElementId, AstNode& owner, AstNode* child,
                          AstArena&) const = 0;
  // `payload` is the element that spelled the text: a directive spread
  // over a choice reaches one member through several rules, and the
  // conversion is the rule's, not the directive's.
  virtual bool write_text(ElementId, ElementId payload, AstNode& owner,
                          std::string_view text, AstArena&) const = 0;

  // What a leaf denotes: its own converter applied, or the raw text. A
  // data rule's value is its parts joined by what they mean, not by their
  // bytes.
  [[nodiscard]] virtual std::string_view denote(
      ElementId, std::string_view text) const = 0;

  // Every value member this node declares, in one order for every node of the
  // grammar. A member holding a node is left to `AstReflection`.
  virtual void visitValues(
      const AstNode& node,
      cursiv::function_ref<void(std::string_view property, std::uint32_t index,
                                ValueKind, std::string_view text)>
          yield) const = 0;
};

}  // namespace cursiv::fuzzer
