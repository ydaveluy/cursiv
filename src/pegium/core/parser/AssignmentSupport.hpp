#pragma once

/// Assignment implementation machinery used by parser-side assignment expressions.

#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <type_traits>
#include <variant>
#include <vector>

#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/FeatureValue.hpp>
#include <pegium/core/parser/AssignmentHelpers.hpp>
#include <pegium/core/parser/FeatureValueSupport.hpp>
#include <pegium/core/parser/Introspection.hpp>
#include <pegium/core/parser/NodeParseHelpers.hpp>
#include <pegium/core/parser/OrderedChoice.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/RawValueTraits.hpp>
#include <pegium/core/parser/AstReflectionBootstrap.hpp>
#include <pegium/core/parser/ValueBuildContext.hpp>
#include <pegium/core/syntax-tree/AstNode.hpp>
#include <pegium/core/syntax-tree/Reference.hpp>

namespace pegium::parser::detail {

template <typename Member> struct AssignmentReferenceMemberInfo {
  static constexpr bool isReference = false;
  static constexpr bool isMulti = false;
  using TargetType = void;
};

template <typename T> struct AssignmentReferenceMemberInfo<Reference<T>> {
  static constexpr bool isReference = true;
  static constexpr bool isMulti = false;
  using TargetType = T;
};

template <typename T> struct AssignmentReferenceMemberInfo<MultiReference<T>> {
  static constexpr bool isReference = true;
  static constexpr bool isMulti = true;
  using TargetType = T;
};

template <typename T>
struct AssignmentReferenceMemberInfo<std::vector<Reference<T>>> {
  static constexpr bool isReference = true;
  static constexpr bool isMulti = true;
  using TargetType = T;
};

template <typename T>
struct AssignmentReferenceMemberInfo<std::optional<T>>
    : AssignmentReferenceMemberInfo<T> {};

template <auto feature> struct AssignmentReferenceSupport {
  using ReferenceInfo =
      AssignmentReferenceMemberInfo<std::remove_cvref_t<helpers::MemberType<feature>>>;

  static constexpr bool isReference = ReferenceInfo::isReference;
  static constexpr bool isMultiReference = ReferenceInfo::isMulti;

  [[nodiscard]] static const AstNodeTypeInfo *
  getReferenceTargetTypeInfo() noexcept {
    if constexpr (isReference) {
      return std::addressof(
          ast_node_type_info<typename ReferenceInfo::TargetType>());
    }
    return nullptr;
  }
};

template <typename E, typename = void> struct AssignmentElementValueType {
  using type = void;
};

template <typename E>
struct AssignmentElementValueType<
    E, std::void_t<typename std::remove_cvref_t<E>::type>> {
  using type = typename std::remove_cvref_t<E>::type;
};

template <typename E>
using AssignmentElementValueType_t = typename AssignmentElementValueType<E>::type;

template <typename T> struct AssignmentIsAstPtr : std::false_type {};
template <typename T>
  requires std::derived_from<T, AstNode>
struct AssignmentIsAstPtr<T *> : std::true_type {
  using type = T;
};

template <typename T> struct AssignmentIsVectorAstPtr : std::false_type {};
template <typename T>
  requires std::derived_from<T, AstNode>
struct AssignmentIsVectorAstPtr<std::vector<T *>> : std::true_type {
  using type = T;
};

template <typename T> struct AssignmentIsOptional : std::false_type {};
template <typename T>
struct AssignmentIsOptional<std::optional<T>> : std::true_type {
  using type = T;
};

enum class OrderedChoiceAssignStatus { Assigned, AssignFailed, NoSelectable };

template <auto feature>
[[nodiscard]] inline std::runtime_error
ordered_choice_no_selectable_error(const CstNodeView &node) {
  std::string message =
      "OrderedChoice has no assignable selected value for " +
      std::string(detail::member_name_v<feature>) +
      " nodeText=\"" + std::string(node.getText()) + "\"" +
      " recovered=" + (node.isRecovered() ? "true" : "false") +
      " leaf=" + (node.isLeaf() ? "true" : "false");
  std::size_t childIndex = 0;
  for (const auto child : node) {
    std::ostringstream grammarStream;
    grammarStream << *child.getGrammarElement();
    message += " child[" + std::to_string(childIndex++) + "]={text=\"" +
               std::string(child.getText()) + "\", grammar=\"" +
               grammarStream.str() + "\"";
    message += ", hidden=";
    message += child.isHidden() ? "true" : "false";
    message += ", recovered=";
    message += child.isRecovered() ? "true" : "false";
    message += ", leaf=";
    message += child.isLeaf() ? "true" : "false";
    message += "}";
  }
  return std::runtime_error(message);
}

template <auto feature>
[[nodiscard]] inline std::runtime_error
ordered_choice_not_assignable_error() {
  return std::runtime_error("OrderedChoice value is not assignable to " +
                            std::string(detail::member_name_v<feature>));
}

template <auto feature>
[[nodiscard]] inline std::runtime_error assignment_not_assignable_error() {
  return std::runtime_error("Assignment value is not assignable to " +
                            std::string(detail::member_name_v<feature>));
}

template <auto feature> struct AssignmentConversionSupport {
public:
  template <typename ClassType, typename AttrType, typename Value>
  static bool assign_reference_target(ClassType *astNode,
                                      AttrType ClassType::*member,
                                      Value &&value,
                                      const ValueBuildContext &context,
                                      const CstNodeView *sourceNode) {
    using RawValueType = std::remove_cvref_t<Value>;
    if constexpr (pegium::is_reference_v<AttrType>) {
      if constexpr (std::same_as<RawValueType, std::string> ||
                    std::same_as<RawValueType, std::string_view>) {
        helpers::AssignmentHelper<AttrType>{}(
            astNode, member, std::string{std::forward<Value>(value)}, context,
            sourceNode != nullptr ? *sourceNode : CstNodeView{});
        return true;
      }
      return false;
    }
    return false;
  }

  template <typename ClassType, typename AttrType, typename Value>
  static bool assign_optional_target(ClassType *astNode,
                                     AttrType ClassType::*member, Value &&value,
                                     const ValueBuildContext &context) {
    using RawValueType = std::remove_cvref_t<Value>;
    if constexpr (AssignmentIsOptional<AttrType>::value) {
      using OptionalValueType = typename AssignmentIsOptional<AttrType>::type;
      // Optional references (std::optional<Reference<T>> /
      // std::optional<MultiReference<T>>) are routed to assign_reference_target
      // upstream by assign_direct_strict, so they never reach this helper.
      static_assert(!pegium::is_reference_v<OptionalValueType>,
                    "optional references are handled by assign_reference_target");

      if constexpr (std::same_as<RawValueType, OptionalValueType>) {
        if constexpr (std::same_as<AttrType, std::optional<OptionalValueType>> &&
                      !std::derived_from<OptionalValueType, AstNode>) {
          (astNode->*member) = std::forward<Value>(value);
          return true;
        }
        helpers::AssignmentHelper<AttrType>{}(astNode, member,
                                              std::forward<Value>(value),
                                              context);
        return true;
      } else if constexpr (std::is_enum_v<OptionalValueType> &&
                           std::is_integral_v<RawValueType>) {
        helpers::AssignmentHelper<AttrType>{}(
            astNode, member, static_cast<OptionalValueType>(value), context);
        return true;
      } else if constexpr (std::same_as<OptionalValueType, std::string> &&
                           std::same_as<RawValueType, std::string_view>) {
        if constexpr (std::same_as<AttrType, std::optional<std::string>>) {
          auto &target = astNode->*member;
          target.emplace();
          target->assign(value.data(), value.size());
          return true;
        }
        helpers::AssignmentHelper<AttrType>{}(astNode, member,
                                              std::string{value}, context);
        return true;
      }
      return false;
    }
    return false;
  }

  template <typename ClassType, typename AttrType, typename Value>
  static bool assign_scalar_target(ClassType *astNode,
                                   AttrType ClassType::*member, Value &&value,
                                   const ValueBuildContext &context) {
    using TargetValueType = helpers::AttrType<feature>;
    using RawValueType = std::remove_cvref_t<Value>;
    if constexpr (std::same_as<RawValueType, TargetValueType>) {
      if constexpr (std::same_as<AttrType, TargetValueType> &&
                    !std::derived_from<TargetValueType, AstNode>) {
        astNode->*member = std::forward<Value>(value);
      } else {
        using NormalizedValue = std::remove_cvref_t<Value>;
        helpers::AssignmentHelper<AttrType>{}(astNode, member,
                                              NormalizedValue{
                                                  std::forward<Value>(value)},
                                              context);
      }
      return true;
    } else if constexpr (std::is_enum_v<TargetValueType> &&
                         std::is_integral_v<RawValueType>) {
      helpers::AssignmentHelper<AttrType>{}(
          astNode, member, static_cast<TargetValueType>(value), context);
      return true;
    } else if constexpr (std::same_as<TargetValueType, std::string> &&
                         std::same_as<RawValueType, std::string_view>) {
      if constexpr (std::same_as<AttrType, std::string>) {
        (astNode->*member).assign(value.data(), value.size());
      } else {
        helpers::AssignmentHelper<AttrType>{}(astNode, member,
                                              std::string{value}, context);
      }
      return true;
    } else if constexpr (std::same_as<TargetValueType, std::nullptr_t> &&
                         std::same_as<RawValueType, std::nullptr_t>) {
      helpers::AssignmentHelper<AttrType>{}(astNode, member, nullptr, context);
      return true;
    } else if constexpr (std::convertible_to<RawValueType, TargetValueType>) {
      // The assign<> admissibility gate accepts any value convertible to the
      // attribute type (e.g. a NUMBER terminal feeding a wider integer field),
      // so honour that here by converting explicitly rather than rejecting it.
      helpers::AssignmentHelper<AttrType>{}(
          astNode, member,
          static_cast<TargetValueType>(std::forward<Value>(value)), context);
      return true;
    } else if constexpr (std::constructible_from<TargetValueType, RawValueType>) {
      helpers::AssignmentHelper<AttrType>{}(
          astNode, member, TargetValueType(std::forward<Value>(value)), context);
      return true;
    }
    return false;
  }

  template <typename ClassType, typename AttrType, typename Value>
  static bool assign_direct_strict(ClassType *astNode,
                                   AttrType ClassType::*member, Value &&value,
                                   const ValueBuildContext &context,
                                   const CstNodeView *sourceNode = nullptr) {
    if constexpr (pegium::is_reference_v<AttrType>) {
      return assign_reference_target(astNode, member, std::forward<Value>(value),
                                     context, sourceNode);
    } else if constexpr (AssignmentIsOptional<AttrType>::value) {
      return assign_optional_target(astNode, member, std::forward<Value>(value),
                                    context);
    } else if constexpr (AssignmentIsAstPtr<AttrType>::value ||
                         AssignmentIsVectorAstPtr<AttrType>::value) {
      // AST-pointer members are assigned exclusively via assign_ast_ptr_to_target:
      // convert_raw_to_target routes AST-pointer raw values there before reaching
      // this strict path, so anything arriving here is a non-AST value that
      // cannot target an AST-pointer member.
      return false;
    } else {
      return assign_scalar_target(astNode, member, std::forward<Value>(value),
                                  context);
    }
  }

  template <typename VariantType, typename Value, std::size_t I = 0>
  static bool assign_variant_exact(VariantType &target, Value &&value) {
    if constexpr (I == std::variant_size_v<VariantType>) {
      return false;
    } else {
      using Alternative = std::variant_alternative_t<I, VariantType>;
      if constexpr (std::same_as<Alternative, std::remove_cvref_t<Value>>) {
        target = std::forward<Value>(value);
        return true;
      } else if constexpr (std::is_enum_v<Alternative> &&
                           std::is_integral_v<std::remove_cvref_t<Value>>) {
        target = static_cast<Alternative>(value);
        return true;
      }
      return assign_variant_exact<VariantType, Value, I + 1>(
          target, std::forward<Value>(value));
    }
  }

  template <typename ClassType, typename VariantType, typename Value,
            std::size_t I = 0>
  static bool assign_variant_from_ast_ptr(ClassType *astNode,
                                          VariantType &target, Value &&value) {
    if constexpr (I == std::variant_size_v<VariantType>) {
      return false;
    } else {
      using Alternative = std::variant_alternative_t<I, VariantType>;
      if constexpr (AssignmentIsAstPtr<Alternative>::value) {
        using AlternativePointee =
            typename AssignmentIsAstPtr<Alternative>::type;
        if constexpr (std::derived_from<AlternativePointee, AstNode>) {
          auto *castedPtr =
              value != nullptr ? dynamic_cast<AlternativePointee *>(value)
                               : nullptr;
          if (castedPtr != nullptr) {
            castedPtr->setContainer(*astNode);
            target = Alternative(castedPtr);
            return true;
          }
        }
      }
      return assign_variant_from_ast_ptr<ClassType, VariantType, Value, I + 1>(
          astNode, target, std::forward<Value>(value));
    }
  }

  template <typename ClassType, typename AttrType, typename RawAstPtr>
  static bool assign_ast_ptr_to_target(ClassType *astNode,
                                       AttrType ClassType::*member,
                                       RawAstPtr rawValue,
                                       const ValueBuildContext &context) {
    using RawPtrType = std::remove_cvref_t<RawAstPtr>;
    using RawPointeeType = typename AssignmentIsAstPtr<RawPtrType>::type;

    if constexpr (AssignmentIsAstPtr<AttrType>::value ||
                  AssignmentIsVectorAstPtr<AttrType>::value) {
      using TargetPointeeType =
          typename std::conditional_t<AssignmentIsAstPtr<AttrType>::value,
                                      AssignmentIsAstPtr<AttrType>,
                                      AssignmentIsVectorAstPtr<AttrType>>::type;

      if constexpr (std::derived_from<RawPointeeType, TargetPointeeType>) {
        helpers::AssignmentHelper<AttrType>{}(astNode, member, rawValue,
                                              context);
        return true;
      } else if constexpr (std::same_as<RawPointeeType, AstNode> &&
                           std::derived_from<TargetPointeeType, AstNode>) {
        auto *castedPtr =
            rawValue != nullptr ? dynamic_cast<TargetPointeeType *>(rawValue)
                                : nullptr;
        if (castedPtr == nullptr) {
          return false;
        }
        helpers::AssignmentHelper<AttrType>{}(astNode, member, castedPtr,
                                              context);
        return true;
      }
    }
    return false;
  }

  template <typename ClassType, typename AttrType, typename RawValue>
  static bool convert_raw_to_target(ClassType *astNode,
                                    AttrType ClassType::*member,
                                    RawValue &&rawValue,
                                    const ValueBuildContext &context,
                                    const CstNodeView *sourceNode = nullptr) {
    using RawType = std::remove_cvref_t<RawValue>;
    using TargetValueType = helpers::AttrType<feature>;

    if constexpr (AssignmentIsAstPtr<RawType>::value &&
                  std::derived_from<typename AssignmentIsAstPtr<RawType>::type,
                                    AstNode>) {
      if constexpr (IsStdVariant<TargetValueType>::value) {
        TargetValueType converted{};
        if (!assign_variant_from_ast_ptr(astNode, converted,
                                         std::forward<RawValue>(rawValue))) {
          return false;
        }
        return assign_direct_strict(astNode, member, std::move(converted),
                                    context, sourceNode);
      }
      return assign_ast_ptr_to_target(astNode, member,
                                      std::forward<RawValue>(rawValue),
                                      context);
    }

    if constexpr (IsStdVariant<TargetValueType>::value) {
      TargetValueType converted{};
      bool assigned =
          assign_variant_exact(converted, std::forward<RawValue>(rawValue));
      if constexpr (std::same_as<RawType, std::string_view>) {
        if (!assigned) {
          assigned = assign_variant_exact(converted, std::string{rawValue});
        }
      }
      if (!assigned) {
        return false;
      }
      if constexpr (std::same_as<AttrType, TargetValueType>) {
        astNode->*member = std::move(converted);
        return true;
      }
      return assign_direct_strict(astNode, member, std::move(converted), context,
                                  sourceNode);
    }

    return assign_direct_strict(astNode, member, std::forward<RawValue>(rawValue),
                                context, sourceNode);
  }
};

template <auto feature, typename Element> struct AssignmentRuntimeSupport {
private:
  using ConversionSupport = AssignmentConversionSupport<feature>;

  [[nodiscard]] static bool
  ordered_choice_contains_recovery(const CstNodeView &node) {
    if (node.isRecovered()) {
      return true;
    }
    return std::ranges::any_of(node, [](const CstNodeView &child) {
      return ordered_choice_contains_recovery(child);
    });
  }

  template <typename ClassType, typename AttrType>
  static OrderedChoiceAssignStatus
  assign_ordered_choice_value(const Element &element, ClassType *astNode,
                              AttrType ClassType::*member,
                              const CstNodeView &node,
                              const ValueBuildContext &context) {
    for (const auto view : node) {
      const auto &child = view.node();
      if (child.isHidden) {
        continue;
      }
      const auto *selectedGrammarElement = child.grammarElement;
      assert(selectedGrammarElement &&
             "OrderedChoice selected node must have a grammar element");
      OrderedChoiceAssignStatus status = OrderedChoiceAssignStatus::NoSelectable;
      const bool matched = detail::visit_selected_ordered_choice_raw_value(
          element, selectedGrammarElement, view, context,
          [&astNode, member, &context, &node, &status]<typename RawValue>(
              RawValue &&rawValue) {
            status = ConversionSupport::convert_raw_to_target(
                         astNode, member,
                         std::forward<RawValue>(rawValue), context, &node)
                         ? OrderedChoiceAssignStatus::Assigned
                         : OrderedChoiceAssignStatus::AssignFailed;
          });
      if (matched) {
        return status;
      }
    }
    return OrderedChoiceAssignStatus::NoSelectable;
  }

  template <typename ClassType, typename AttrType>
  static void execute_assignment_value(const Element &element, AstNode *current,
                                       AttrType ClassType::*,
                                       const CstNodeView &node,
                                       const ValueBuildContext &context) {
    assert(current);
    auto *astNode = static_cast<ClassType *>(current);

    if constexpr (IsOrderedChoice<Element>::value) {
      const auto status =
          assign_ordered_choice_value(element, astNode, feature, node, context);
      if (status == OrderedChoiceAssignStatus::NoSelectable) [[unlikely]] {
        if (ordered_choice_contains_recovery(node)) {
          return;
        }
        throw detail::ordered_choice_no_selectable_error<feature>(node);
      }
      if (status == OrderedChoiceAssignStatus::AssignFailed) [[unlikely]] {
        throw detail::ordered_choice_not_assignable_error<feature>();
      }
      return;
    } else if constexpr (std::is_same_v<bool, AttrType> &&
                         !std::is_same_v<bool,
                                         AssignmentElementValueType_t<Element>>) {
      helpers::AssignmentHelper<AttrType>{}(astNode, feature, true, context);
    } else {
      if (ConversionSupport::convert_raw_to_target(
              astNode, feature,
              detail::extract_raw_value(element, node, context), context,
              &node)) {
        return;
      }
      throw detail::assignment_not_assignable_error<feature>();
    }
  }

public:
  static void execute(const Element &element, AstNode *current,
                      const CstNodeView &node,
                      const ValueBuildContext &context) {
    execute_assignment_value(element, current, feature, node, context);
  }

  static grammar::FeatureValue getValue(const AstNode *current) {
    return FeatureValueSupport::template get_value<feature>(current, feature);
  }
};

template <auto feature, typename Element> struct AssignmentParseSupport {
private:
  using ReferenceSupport = AssignmentReferenceSupport<feature>;

  template <ParseModeContext Context>
  [[gnu::always_inline]] static bool
  parse_element(Context &ctx, const grammar::Assignment *assignment,
                const Element &element) {
    if constexpr (IsOrderedChoice<Element>::value) {
      return detail::parse_wrapped_node(ctx, assignment, element);
    }
    return detail::parse_overriding_first_child(ctx, assignment, element);
  }

public:
  template <ParseModeContext Context>
  [[gnu::always_inline]] static bool
  parse(Context &ctx, const grammar::Assignment *assignment,
        const Element &element) {
    if constexpr (ExpectParseModeContext<Context>) {
      if constexpr (ReferenceSupport::isReference) {
        if (ctx.reachedAnchor()) {
          ctx.addReference(assignment);
          return true;
        }
      }
      auto assignmentGuard = ctx.with_assignment(assignment);
      (void)assignmentGuard;
      return parse_element(ctx, assignment, element);
    }
    return parse_element(ctx, assignment, element);
  }
};

} // namespace pegium::parser::detail
