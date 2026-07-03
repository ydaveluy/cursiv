#pragma once

#include <cassert>
#include <pegium/core/grammar/Nest.hpp>
#include <pegium/core/parser/AssignmentHelpers.hpp>
#include <pegium/core/parser/Introspection.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <pegium/core/parser/AstReflectionBootstrap.hpp>
#include <pegium/core/syntax-tree/AstArena.hpp>
#include <string_view>
#include <type_traits>

namespace pegium::parser {

template <typename T, auto feature>
  requires std::is_member_pointer_v<decltype(feature)> &&
           DefaultConstructibleAstNode<T>
struct Nest final : grammar::Nest {
  static constexpr bool nullable = true;
  static constexpr bool isFailureSafe = true;

  AstNode *getValue(AstNode *current, AstArena &arena) const override {
    auto *newNode = arena.create<T>();
    ValueBuildContext context{};
    context.arena = &arena;
    using FeatureType = helpers::AttrType<feature>;
    static_assert(std::derived_from<FeatureType, AstNode>,
                  "nest<>() requires an AST-pointer feature: the member's "
                  "attribute type must derive from AstNode");
    assert(current != nullptr);
    auto *featureNode = static_cast<FeatureType *>(current);
    helpers::AssignmentHelper<helpers::MemberType<feature>>{}(
        newNode, feature, featureNode, context);
    return newNode;
  }

  std::string_view getTypeName() const noexcept override {
    static constexpr auto typeName = detail::type_name_v<T>;
    return typeName;
  }

  std::string_view getFeature() const noexcept override {
    return detail::member_name_v<feature>;
  }

  constexpr bool isNullable() const noexcept override {
    return nullable;
  }

private:
  friend struct detail::ParseAccess;
  friend struct detail::InitAccess;

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    if constexpr (ExpectParseModeContext<Context>) {
      (void)ctx;
      return true;
    } else {
      ctx.leaf(ctx.cursor(), this);
      return true;
    }
  }

  void init_impl(AstReflectionInitContext &ctx) const {
    ctx.registerProducedType(detail::ast_node_type_info<T>());
  }
};

/// Create a new parser-managed AST shell of type `T` and assign the given
/// feature with the current value.
///
/// `T` must be default-constructible for the same reason as `create<T>()`:
/// nested nodes are materialized first and populated afterwards.
template <typename T, auto feature>
  requires std::is_member_pointer_v<decltype(feature)> &&
           DefaultConstructibleAstNode<T> &&
           std::derived_from<helpers::AttrType<feature>, AstNode>
static constexpr auto nest() {
  return Nest<T, feature>();
}

/// Create a new parser-managed AST shell and assign the given feature with the
/// current value. The instance type is deduced from the feature.
template <auto feature>
  requires std::is_member_pointer_v<decltype(feature)> &&
           DefaultConstructibleAstNode<helpers::ClassType<feature>> &&
           std::derived_from<helpers::AttrType<feature>, AstNode>
static constexpr auto nest() {
  return Nest<helpers::ClassType<feature>, feature>();
}

} // namespace pegium::parser
