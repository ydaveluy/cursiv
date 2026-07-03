#pragma once

#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/parser/AssignmentSupport.hpp>
#include <pegium/core/parser/Introspection.hpp>
#include <pegium/core/parser/OrderedChoice.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <pegium/core/parser/RawValueTraits.hpp>
#include <pegium/core/parser/AstReflectionBootstrap.hpp>
#include <pegium/core/parser/ValueBuildContext.hpp>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <string_view>

namespace pegium::parser {

/// Feature assignment keyed on the parse-expression shape only: everything
/// feature-specific (name, reference flags, reflection metadata, execute /
/// getValue) lives in the static AssignmentDescriptor the factories pass in,
/// so assignments of different features through the same element type share
/// one instantiation.
template <NonNullableExpression Element>
struct Assignment final : grammar::Assignment {
private:
  static constexpr bool assignment_has_required_raw_values =
      detail::expression_raw_compliant_v<Element>;

  static_assert(
      assignment_has_required_raw_values,
      "Assignment requires getRawValue(node): direct expression must provide "
      "a specific typed value (not grammar::RuleValue); for OrderedChoice, "
      "each choice must provide getRawValue(node) with a specific typed value.");

public:
  static constexpr bool nullable = false;
  static constexpr bool isFailureSafe =
      !IsOrderedChoice<Element>::value &&
      std::remove_cvref_t<Element>::isFailureSafe;

  explicit constexpr Assignment(
      Element &&element, AssignmentOperator ope,
      const detail::AssignmentDescriptor<Element> &ops)
    requires(!std::is_lvalue_reference_v<Element>)
      : _element(std::move(element)), _ops(&ops), _operator{ope} {}
  explicit constexpr Assignment(
      Element element, AssignmentOperator ope,
      const detail::AssignmentDescriptor<Element> &ops)
    requires(std::is_lvalue_reference_v<Element>)
      : _element(element), _ops(&ops), _operator{ope} {}

  constexpr Assignment(Assignment &&) noexcept = default;
  constexpr Assignment(const Assignment &) = default;
  constexpr Assignment &operator=(Assignment &&) noexcept = default;
  constexpr Assignment &operator=(const Assignment &) = default;

  const grammar::AbstractElement *getElement() const noexcept override {
    return std::addressof(_element);
  }
  std::string_view getFeature() const noexcept override {
    return _ops->featureName;
  }
  constexpr AssignmentOperator getOperator() const noexcept override {
    return _operator;
  }
  [[nodiscard]] bool isReference() const noexcept override {
    return _ops->isReference;
  }
  [[nodiscard]] bool isMultiReference() const noexcept override {
    return _ops->isMultiReference;
  }
  [[nodiscard]] std::type_index getType() const noexcept override {
    const auto &typeInfo = _ops->reflectionInfo();
    if (typeInfo.referenceTargetType != nullptr) {
      return typeInfo.referenceTargetType->type;
    }
    if (typeInfo.assignedAstType != nullptr) {
      return typeInfo.assignedAstType->type;
    }
    return detail::invalid_type();
  }
  constexpr bool isNullable() const noexcept override {
    return nullable;
  }
  bool probeRecoverable(RecoveryContext &ctx) const {
    return attempt_fast_probe(ctx, _element) ||
           probe_locally_recoverable(_element, ctx);
  }

  bool probeRecoverableAtEntry(RecoveryContext &ctx) const {
    if (attempt_fast_probe(ctx, _element)) {
      return true;
    }
    return probe_recoverable_at_entry(_element, ctx);
  }

  bool probeRecoverableAtEntryConsumesVisible(RecoveryContext &ctx) const {
    if (attempt_fast_probe(ctx, _element)) {
      return true;
    }
    return probe_recoverable_at_entry_consumes_visible(_element, ctx);
  }

  void execute(AstNode *current, const CstNodeView &node,
               const ValueBuildContext &context) const override {
    _ops->execute(_element, current, node, context.withAssignment(*this));
  }
  grammar::FeatureValue getValue(const AstNode *ast) const override {
    return _ops->getValue(ast);
  }

private:
  friend struct detail::ParseAccess;
  friend struct detail::FastProbeAccess;
  friend struct detail::InitAccess;

  ExpressionHolder<Element> _element;
  const detail::AssignmentDescriptor<Element> *_ops;
  AssignmentOperator _operator;

  template <StrictParseModeContext Context>
  bool fast_probe_impl(Context &ctx) const {
    return attempt_fast_probe(ctx, _element);
  }

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    return detail::AssignmentParseSupport<Element>::parse(ctx, this, _element,
                                                          *_ops);
  }

  void init_impl(AstReflectionInitContext &ctx) const {
    const auto &typeInfo = _ops->reflectionInfo();
    ctx.registerAssignment(typeInfo);
    if (typeInfo.assignedAstType != nullptr) {
      auto childContext = ctx.withExpectedType(typeInfo.assignedAstType->type);
      parser::init(_element, childContext);
      return;
    }
    auto childContext = ctx.withoutExpectedType();
    parser::init(_element, childContext);
  }
};

/// Assign an element to a member of the current object
/// @tparam feature the member pointer
/// @tparam Element the parse expression
/// @param element the expression
/// @return the assignment
template <auto feature, NonNullableExpression Element>
  requires(helpers::IsValidAssignment<feature, Element>::value &&
           !helpers::IsMany<feature>)
static constexpr auto assign(Element &&element) {
  return Assignment<Element>(std::forward<Element>(element),
                             pegium::grammar::AssignmentOperator::Assign,
                             detail::assignment_descriptor<feature, Element>);
}

/// Append an element to a member of the current object
/// @tparam feature the member pointer
/// @tparam Element the parse expression
/// @param element the expression
/// @return the assignment
template <auto feature, NonNullableExpression Element>
  requires helpers::IsValidAssignment<feature, Element>::value &&
           helpers::IsMany<feature>
static constexpr auto append(Element &&element) {
  return Assignment<Element>(std::forward<Element>(element),
                             pegium::grammar::AssignmentOperator::Append,
                             detail::assignment_descriptor<feature, Element>);
}

/// Enable a member of the current object
/// @tparam feature the boolean member pointer
/// @tparam Element the parse expression
/// @param element the expression
/// @return the assignment
template <auto feature, NonNullableExpression Element>
  requires std::same_as<bool, helpers::AttrType<feature>>
static constexpr auto enable_if(Element &&element) {
  return Assignment<Element>(std::forward<Element>(element),
                             pegium::grammar::AssignmentOperator::EnableIf,
                             detail::assignment_descriptor<feature, Element>);
}
} // namespace pegium::parser
