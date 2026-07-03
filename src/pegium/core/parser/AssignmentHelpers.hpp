#pragma once

/// Helper traits and utilities for assigning parser results into AST members.

#include <cassert>
#include <concepts>
#include <memory>
#include <optional>
#include <variant>
#include <pegium/core/parser/OrderedChoice.hpp>
#include <pegium/core/parser/ValueBuildContext.hpp>
#include <pegium/core/references/Linker.hpp>
#include <pegium/core/syntax-tree/AstNode.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace pegium::parser::helpers {

template <typename T> struct IsVariant : std::false_type {};
template <typename... Ts> struct IsVariant<std::variant<Ts...>> : std::true_type {};

/// Outer container a member's core attribute value is wrapped in.
enum class MemberWrapper { None, Optional, Vector };

/// Category of a member's fully-unwrapped core attribute value (or, for a
/// reference member, of the member itself -- see the `MemberShape` comment).
enum class MemberCoreKind {
  Scalar,
  String,
  Enum,
  Variant,
  AstPtr,
  Reference,
  MultiReference,
  PointerHolder,
};

template <auto Member> struct MemberShape;

/// Every angle from which the assignment machinery inspects a member's
/// declared type, folded into one trait. `Unwrapped`/`IsMany` answer the
/// question the admissibility gate asks (what `MemberTraits` exposes as
/// `AttrType`/`IsMany`); `IsReference`/`IsMultiReference`/`ReferenceTarget`
/// answer the question reflection asks (mirroring
/// `AssignmentReferenceMemberInfo` in AssignmentSupport.hpp). The two are
/// computed separately because they unwrap differently: a
/// `std::vector<Reference<T>>` gate-unwraps to the *literal* `Reference<T>`
/// (a vector's element type is a single unwrap step, never itself
/// re-unwrapped), while it reference-unwraps all the way to the pointee `T`.
template <typename Class, typename Type, Type Class::*Member>
struct MemberShape<Member> {
  using ClassType = Class;
  using MemberType = Type;

private:
  // The gate-facing unwrap chain (what `AttrType<feature>` resolves to): T ->
  // itself; T* (AstNode) -> pointee; std::vector<T> -> T (one level, not
  // recursed -- so std::vector<Reference<T>> stops at the literal
  // Reference<T>); std::optional<T> -> recurse into T; Reference<T> /
  // MultiReference<T> -> std::string; std::vector<T*> (AstNode) -> pointee,
  // many.
  template <typename T> struct GateUnwrap {
    using type = T;
    static constexpr bool isMany = false;
  };
  template <typename T>
    requires std::derived_from<T, AstNode>
  struct GateUnwrap<T *> {
    using type = T;
    static constexpr bool isMany = false;
  };
  template <typename T> struct GateUnwrap<std::vector<T>> {
    using type = T;
    static constexpr bool isMany = true;
  };
  template <typename T> struct GateUnwrap<std::optional<T>> : GateUnwrap<T> {};
  template <typename T> struct GateUnwrap<Reference<T>> {
    using type = std::string;
    static constexpr bool isMany = false;
  };
  template <typename T> struct GateUnwrap<MultiReference<T>> {
    using type = std::string;
    static constexpr bool isMany = false;
  };
  template <typename T>
    requires std::derived_from<T, AstNode>
  struct GateUnwrap<std::vector<T *>> {
    using type = T;
    static constexpr bool isMany = true;
  };

  // Reference detection: recurses through std::optional and has a dedicated
  // std::vector<Reference<T>> case, unlike GateUnwrap above -- so it sees
  // through to a reference's pointee where GateUnwrap either already
  // collapsed to std::string or stopped at the literal Reference<T>.
  template <typename T> struct RefProbe {
    static constexpr bool isReference = false;
    static constexpr bool isMulti = false;
    using target = void;
  };
  template <typename T> struct RefProbe<Reference<T>> {
    static constexpr bool isReference = true;
    static constexpr bool isMulti = false;
    using target = T;
  };
  template <typename T> struct RefProbe<MultiReference<T>> {
    static constexpr bool isReference = true;
    static constexpr bool isMulti = true;
    using target = T;
  };
  template <typename T> struct RefProbe<std::vector<Reference<T>>> {
    static constexpr bool isReference = true;
    static constexpr bool isMulti = true;
    using target = T;
  };
  template <typename T> struct RefProbe<std::optional<T>> : RefProbe<T> {};

  // The outer container, from the raw member type alone (not recursive).
  template <typename T> struct WrapperOf {
    static constexpr MemberWrapper value = MemberWrapper::None;
  };
  template <typename T> struct WrapperOf<std::vector<T>> {
    static constexpr MemberWrapper value = MemberWrapper::Vector;
  };
  template <typename T> struct WrapperOf<std::optional<T>> {
    static constexpr MemberWrapper value = MemberWrapper::Optional;
  };

  template <typename T> struct IsUniquePtr : std::false_type {};
  template <typename T>
  struct IsUniquePtr<std::unique_ptr<T>> : std::true_type {};

public:
  /// The gate-facing unwrapped type and whether the member is a "many"
  /// (append) target -- what `AttrType<feature>`/`IsMany<feature>` resolve
  /// to.
  using Unwrapped = typename GateUnwrap<Type>::type;
  static constexpr bool IsMany = GateUnwrap<Type>::isMany;

  static constexpr MemberWrapper Wrapper = WrapperOf<Type>::value;

  /// Reference detection and the reference's pointee (void if not one).
  static constexpr bool IsReference = RefProbe<Type>::isReference;
  static constexpr bool IsMultiReference = RefProbe<Type>::isMulti;
  using ReferenceTarget = typename RefProbe<Type>::target;

  /// The category `Unwrapped` falls into -- for a reference member (which
  /// `Unwrapped` cannot itself express, see the unwrap-chain comment above),
  /// the category the member itself denotes.
  static constexpr MemberCoreKind CoreKind =
      IsReference ? (IsMultiReference ? MemberCoreKind::MultiReference
                                       : MemberCoreKind::Reference)
      : IsVariant<Unwrapped>::value ? MemberCoreKind::Variant
      : std::derived_from<Unwrapped, AstNode> ? MemberCoreKind::AstPtr
      : std::is_enum_v<Unwrapped>              ? MemberCoreKind::Enum
      : std::same_as<Unwrapped, std::string>   ? MemberCoreKind::String
      : (std::is_pointer_v<Unwrapped> || IsUniquePtr<Unwrapped>::value)
          ? MemberCoreKind::PointerHolder
          : MemberCoreKind::Scalar;
};

template <auto Member> struct MemberTraits;

template <typename Class, typename Type, Type Class::*Member>
struct MemberTraits<Member> {
  using ClassType = Class;
  using MemberType = Type;
  using AttrType = typename MemberShape<Member>::Unwrapped;
  static constexpr bool IsMany = MemberShape<Member>::IsMany;
};

template <auto Member>
using ClassType = typename MemberTraits<Member>::ClassType;
template <auto Member>
using MemberType = typename MemberTraits<Member>::MemberType;

template <auto Member> using AttrType = typename MemberTraits<Member>::AttrType;
template <auto Member>
static constexpr bool IsMany = MemberTraits<Member>::IsMany;

inline void register_handle(const ValueBuildContext &context,
                            const ReferenceHandle &handle) {
  if (context.references != nullptr) {
    assert(handle);
    context.references->push_back(handle);
  }
}

template <typename T, typename Ref, typename Node>
void initialize_reference(Ref &reference, Node &node, std::string refText,
                          CstNodeView refNode,
                          const ValueBuildContext &context) {
  assert(context.linker != nullptr);
  assert(context.assignment != nullptr);
  assert(context.assignment->isReference());
  assert(context.assignment->getType() == std::type_index(typeid(T)));
  reference.initialize(node, std::move(refText), refNode,
                       *context.assignment, *context.linker);
}

template <typename U>
[[nodiscard]] std::string make_reference_text(U &&value) {
  using Value = std::remove_cvref_t<U>;
  if constexpr (std::same_as<Value, std::string>) {
    return std::forward<U>(value);
  } else if constexpr (std::same_as<Value, std::string_view>) {
    return std::string(value);
  } else if constexpr (std::same_as<Value, const char *>) {
    return std::string(value);
  } else {
    return std::string{std::forward<U>(value)};
  }
}

template <typename T>
[[nodiscard]] std::string make_reference_text(const Reference<T> &reference) {
  return reference.getRefText();
}

template <typename T>
[[nodiscard]] std::string
make_reference_text(const MultiReference<T> &reference) {
  return reference.getRefText();
}

template <typename T, typename Ref, typename Node, typename U>
void initialize_reference_from_value(Ref &reference, Node &node, U &&value,
                                     CstNodeView refNode,
                                     const ValueBuildContext &context) {
  initialize_reference<T>(reference, node,
                          make_reference_text(std::forward<U>(value)), refNode,
                          context);
}

// Generic
template <typename T> struct AssignmentHelper {
  template <typename Node, typename Base, typename U>
    requires std::derived_from<Node, AstNode> && std::derived_from<Node, Base>
  void operator()(Node *node, T Base::*member, U &&value,
                  const ValueBuildContext & /*context*/) const {

    if constexpr (std::is_convertible_v<U, T>) {
      node->*member = std::forward<U>(value);
    } else if constexpr (std::is_constructible_v<T, U>) {
      node->*member = T{std::forward<U>(value)};
    } else {
      static_assert(false, "not convertible or constructible");
    }
    // set the container
    if constexpr (std::derived_from<T, AstNode>) {
      (node->*member).setContainer(*node);
    }
  }
};

template <typename T> struct AssignmentHelper<Reference<T>> {
  template <typename Node, typename Base, typename U>
    requires std::derived_from<Node, AstNode> && std::derived_from<Node, Base>
  void operator()(Node *node, Reference<T> Base::*member, U &&value,
                  const ValueBuildContext &context,
                  CstNodeView sourceNode = {}) const {
    initialize_reference_from_value<T>(node->*member, *node,
                                       std::forward<U>(value), sourceNode,
                                       context);
    helpers::register_handle(context, ReferenceHandle::direct(&(node->*member)));
  }
};

template <typename T> struct AssignmentHelper<MultiReference<T>> {
  template <typename Node, typename Base, typename U>
    requires std::derived_from<Node, AstNode> && std::derived_from<Node, Base>
  void operator()(Node *node, MultiReference<T> Base::*member, U &&value,
                  const ValueBuildContext &context,
                  CstNodeView sourceNode = {}) const {
    initialize_reference_from_value<T>(node->*member, *node,
                                       std::forward<U>(value), sourceNode,
                                       context);
    helpers::register_handle(context, ReferenceHandle::direct(&(node->*member)));
  }
};

template <typename T> struct AssignmentHelper<std::vector<Reference<T>>> {
  template <typename Node, typename Base, typename U>
    requires std::derived_from<Node, AstNode> && std::derived_from<Node, Base>
  void operator()(Node *node, std::vector<Reference<T>> Base::*member,
                  U &&value, const ValueBuildContext &context,
                  CstNodeView sourceNode = {}) const {
    auto &referencesVector = node->*member;
    referencesVector.emplace_back();
    const auto index = referencesVector.size() - 1;
    initialize_reference_from_value<T>(referencesVector.back(), *node,
                                       std::forward<U>(value), sourceNode,
                                       context);
    helpers::register_handle(context,
                             ReferenceHandle::indexed(&referencesVector, index));
  }
};

template <typename T>
struct AssignmentHelper<std::optional<Reference<T>>> {
  template <typename Node, typename Base, typename U>
    requires std::derived_from<Node, AstNode> && std::derived_from<Node, Base>
  void operator()(Node *node, std::optional<Reference<T>> Base::*member,
                  U &&value, const ValueBuildContext &context,
                  CstNodeView sourceNode = {}) const {
    auto &target = node->*member;
    target.emplace();
    // When no source node is provided and the value is itself a Reference,
    // inherit the source node from the value.
    if constexpr (std::same_as<std::remove_cvref_t<U>, Reference<T>>) {
      if (!sourceNode.valid()) {
        sourceNode = value.getRefNode();
      }
    }
    initialize_reference_from_value<T>(*target, *node, std::forward<U>(value),
                                       sourceNode, context);
    helpers::register_handle(context, ReferenceHandle::optional(&(node->*member)));
  }
};

template <typename T>
struct AssignmentHelper<std::optional<MultiReference<T>>> {
  template <typename Node, typename Base, typename U>
    requires std::derived_from<Node, AstNode> && std::derived_from<Node, Base>
  void operator()(Node *node, std::optional<MultiReference<T>> Base::*member,
                  U &&value, const ValueBuildContext &context,
                  CstNodeView sourceNode = {}) const {
    auto &target = node->*member;
    target.emplace();
    // When no source node is provided and the value is itself a MultiReference,
    // inherit the source node from the value.
    if constexpr (std::same_as<std::remove_cvref_t<U>, MultiReference<T>>) {
      if (!sourceNode.valid()) {
        sourceNode = value.getRefNode();
      }
    }
    initialize_reference_from_value<T>(*target, *node, std::forward<U>(value),
                                       sourceNode, context);
    helpers::register_handle(context, ReferenceHandle::optional(&(node->*member)));
  }
};

template <typename T>
  requires std::derived_from<T, AstNode>
struct AssignmentHelper<T *> {
  template <typename Node, typename Base, typename U>
    requires std::derived_from<Node, AstNode> &&
             std::derived_from<Node, Base> && std::derived_from<U, T>
  void operator()(Node *node, T *Base::*member, U *value,
                  const ValueBuildContext & /*context*/) const {
    node->*member = value;
    if (value != nullptr) {
      value->setContainer(*node);
    }
  }
};

template <typename T> struct AssignmentHelper<std::vector<T>> {
  template <typename Node, typename Base, typename U>
    requires std::derived_from<Node, AstNode> && std::derived_from<Node, Base>
  void operator()(Node *node, std::vector<T> Base::*member, U &&value,
                  const ValueBuildContext &) const {

    static_assert(!std::derived_from<T, AstNode>,
                  "An AstNode must be stored as a raw pointer (T *)");
    if constexpr (std::is_convertible_v<U, T>) {
      (node->*member).emplace_back(std::forward<U>(value));
    } else if constexpr (std::is_constructible_v<T, U>) {
      (node->*member).emplace_back(T{std::forward<U>(value)});
    } else {
      static_assert(false, "not convertible or constructible");
    }
  }
};

template <typename T>
  requires std::derived_from<T, AstNode>
struct AssignmentHelper<std::vector<T *>> {
  template <typename Node, typename Base, typename U>
    requires std::derived_from<Node, AstNode> &&
             std::derived_from<Node, Base> && std::derived_from<U, T>
  void operator()(Node *node, std::vector<T *> Base::*member, U *value,
                  const ValueBuildContext & /*context*/) const {
    if (value != nullptr) {
      value->setContainer(*node);
    }
    (node->*member).push_back(value);
  }
};

/// Named admissibility cases for assigning a grammar element that produces
/// `ElementType` into a member whose *unwrapped* attribute type is `AttrType`
/// (see `MemberShape`'s `GateUnwrap` — `Reference<T>`->`std::string`,
/// `T*`->`T`, `std::optional<T>`->inner, `std::vector<T>`->`T`,
/// scalar->itself).
/// `IsValidAssignment` admits an assignment iff one of these cases holds, and
/// each names a conversion the runtime dispatch in AssignmentSupport.hpp knows
/// how to perform. Expressing the gate as this case list (rather than an ad hoc
/// boolean soup) is what keeps it from silently drifting out of sync with the
/// dispatch and admitting an assignment the runtime then throws on.
template <typename AttrType, typename ElementType>
concept AstPointerUpcast = std::derived_from<AttrType, AstNode> &&
                           std::derived_from<ElementType, AttrType>;

template <typename AttrType, typename ElementType>
concept ConvertibleScalar = std::convertible_to<ElementType, AttrType>;

template <typename AttrType, typename ElementType>
concept ReferenceFromString =
    pegium::is_reference_v<AttrType> &&
    (std::convertible_to<ElementType, std::string> ||
     std::convertible_to<ElementType, std::string_view>);

template <typename AttrType, typename ElementType>
concept ConstructibleScalar = std::constructible_from<AttrType, ElementType>;

/// A pointer-holder attribute (`T*` for a non-AstNode `T`, `std::unique_ptr<T>`,
/// or a `std::variant<..., T*, ...>` alternative) is built from a pointer to the
/// produced value — the runtime hands it `E*` and constructs the holder. The
/// `!is_fundamental_v<ElementType>` guard is what fixes the historical bug: the
/// bare `constructible_from<AttrType, ElementType*>` also matched `std::string`
/// from `char` (since `std::string` is constructible from `char*`) and
/// reference members (whose attribute type is `std::string`), admitting an
/// assignment the runtime then threw on. A produced value is never legitimately
/// stored by taking the address of a *fundamental* value, so excluding those
/// keeps every real pointer-holder case and drops only the accident.
/// The `!std::same_as<AttrType, bool>` guard keeps `bool` out: `bool` is
/// constructible from any pointer, but it is not a pointer-holder — a bool
/// target is handled by the scalar-bool set-true rule below, not by storing a
/// pointer to the value.
template <typename AttrType, typename ElementType>
concept PointerHolderFromValue = std::constructible_from<AttrType, ElementType *> &&
                                 !std::is_fundamental_v<ElementType> &&
                                 !std::same_as<AttrType, bool>;

/// The non-variant admissibility cases (a variant target is handled separately
/// by `variant_alternative_admits`, which mirrors the runtime's exact-match
/// dispatch rather than this convertible/constructible chain).
template <typename AttrType, typename ElementType>
concept AdmissibleAssignment = AstPointerUpcast<AttrType, ElementType> ||
                               ConvertibleScalar<AttrType, ElementType> ||
                               ReferenceFromString<AttrType, ElementType> ||
                               ConstructibleScalar<AttrType, ElementType> ||
                               PointerHolderFromValue<AttrType, ElementType>;

/// The reason `classify_assignment` admits assigning a produced `ElementType`
/// to a member: which of the six named concepts above fired, which of
/// `variant_alternative_admits`'s four internal rules fired, or the
/// bool-member set-true special case -- or `Inadmissible` if none did.
/// `WriteExact` and `ConvertScalar` both witness `ConvertibleScalar`; they are
/// split only to record whether the produced value already has the member's
/// exact type (no conversion happens) or a genuinely different, merely
/// convertible one.
enum class AssignmentAction {
  WriteExact,
  ConvertScalar,
  ConstructScalar,
  EnumFromIntegral,
  StringFromView,
  ReferenceFromText,
  AstPointerUpcast,
  VariantExact,
  VariantFromAstPointer,
  PointerHolderFromValue,
  BoolSetTrue,
  Inadmissible,
};

/// Which of `variant_alternative_admits`'s four rules admits placing a
/// produced `ElementType` into `Variant`, mirroring the runtime variant
/// dispatch (`assign_variant_exact` + `assign_variant_from_ast_ptr` + the
/// `string_view`->`std::string` retry in `convert_raw_to_target`). The
/// runtime never *converts* into an alternative (e.g. `bool`->`int`), it
/// exact-matches; so the generic convertible/constructible chain would
/// over-admit here and the runtime would then throw. An `ElementType` is
/// admissible iff it exactly matches an alternative, is an integral feeding
/// an enum alternative, is a `string_view` feeding a `std::string`
/// alternative, or is an AstNode value whose pointer upcasts to an
/// ast-pointer alternative.
template <typename Variant, typename ElementType, std::size_t I = 0>
[[nodiscard]] constexpr AssignmentAction variant_alternative_action() {
  if constexpr (I == std::variant_size_v<Variant>) {
    return AssignmentAction::Inadmissible;
  } else {
    using Alt = std::variant_alternative_t<I, Variant>;
    if constexpr (std::same_as<Alt, ElementType>) {
      return AssignmentAction::VariantExact;
    } else if constexpr (std::is_enum_v<Alt> && std::is_integral_v<ElementType>) {
      return AssignmentAction::EnumFromIntegral;
    } else if constexpr (std::same_as<Alt, std::string> &&
                         std::same_as<ElementType, std::string_view>) {
      return AssignmentAction::StringFromView;
    } else if constexpr (std::is_pointer_v<Alt> &&
                         std::derived_from<std::remove_pointer_t<Alt>, AstNode> &&
                         std::derived_from<ElementType, std::remove_pointer_t<Alt>>) {
      return AssignmentAction::VariantFromAstPointer;
    } else {
      return variant_alternative_action<Variant, ElementType, I + 1>();
    }
  }
}

/// True iff a produced value of type `ElementType` can be placed into
/// `Variant` — see `variant_alternative_action` for which rule admits it.
template <typename Variant, typename ElementType>
[[nodiscard]] constexpr bool variant_alternative_admits() {
  return variant_alternative_action<Variant, ElementType>() !=
         AssignmentAction::Inadmissible;
}

/// Which action, if any, admits assigning an element producing `ElementType`
/// to member `feature`. Restates `assignment_admitted`'s gate at the same
/// granularity the runtime dispatch operates at: a `std::variant` attribute
/// checks only the exact-match variant rules (`variant_alternative_action`);
/// otherwise a scalar `bool` member accepts any element (the set-true rule in
/// `AssignmentRuntimeSupport::execute_assignment_value`, keyed on the *raw*
/// member type so `std::vector<bool>`/`std::optional<bool>`, which have no
/// set-true path, do not wrongly qualify) ahead of the five named concepts,
/// checked in their current disjunction order (the gate itself assigns no
/// priority among them — only one needs to hold — so this order is a
/// documentation choice, not a semantic one; a Raw type satisfying more than
/// one concept still classifies as whichever is checked first).
template <auto feature, typename ElementType>
[[nodiscard]] consteval AssignmentAction classify_assignment() {
  using Attr = AttrType<feature>;
  if constexpr (IsVariant<Attr>::value) {
    return variant_alternative_action<Attr, ElementType>();
  } else if constexpr (std::same_as<MemberType<feature>, bool>) {
    return AssignmentAction::BoolSetTrue;
  } else if constexpr (AstPointerUpcast<Attr, ElementType>) {
    return AssignmentAction::AstPointerUpcast;
  } else if constexpr (ConvertibleScalar<Attr, ElementType>) {
    if constexpr (std::same_as<ElementType, Attr>) {
      return AssignmentAction::WriteExact;
    } else {
      return AssignmentAction::ConvertScalar;
    }
  } else if constexpr (ReferenceFromString<Attr, ElementType>) {
    return AssignmentAction::ReferenceFromText;
  } else if constexpr (ConstructibleScalar<Attr, ElementType>) {
    return AssignmentAction::ConstructScalar;
  } else if constexpr (PointerHolderFromValue<Attr, ElementType>) {
    return AssignmentAction::PointerHolderFromValue;
  } else {
    return AssignmentAction::Inadmissible;
  }
}

/// Whether assigning an element producing `ElementType` to member `feature` is
/// admissible — true iff `classify_assignment` names an action for it.
template <auto feature, typename ElementType>
[[nodiscard]] constexpr bool assignment_admitted() {
  return classify_assignment<feature, ElementType>() !=
         AssignmentAction::Inadmissible;
}

template <auto feature, typename Element, typename = void>
struct IsValidAssignmentImpl : std::false_type {};

template <auto feature, typename Element>
struct IsValidAssignmentImpl<feature, Element,
                             std::void_t<typename Element::type>>
    : std::bool_constant<
          assignment_admitted<feature, typename Element::type>()> {};

template <auto feature, typename... Element>
struct IsValidAssignmentImpl<feature, OrderedChoice<Element...>, void>
    : std::bool_constant<(IsValidAssignmentImpl<feature,
                                               std::remove_cvref_t<Element>>::value &&
                          ...)> {};

template <auto feature, typename Element>
struct IsValidAssignment
    : IsValidAssignmentImpl<feature, std::remove_cvref_t<Element>> {};

} // namespace pegium::parser::helpers
