#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include <pegium/core/services/JsonValue.hpp>
#include <pegium/core/services/Diagnostic.hpp>
#include <pegium/core/syntax-tree/AstNode.hpp>
#include <pegium/core/utils/Cancellation.hpp>
#include <pegium/core/validation/ValidationAcceptor.hpp>

namespace pegium::validation {

namespace detail {

template <typename> struct ValidationCheckMethodTraits;

template <typename Class, typename Node>
struct ValidationCheckMethodTraits<
    void (Class::*)(const Node &, const ValidationAcceptor &)> {
  using ClassType = Class;
  using NodeType = Node;
  static constexpr bool acceptsCancellation = false;
};

template <typename Class, typename Node>
struct ValidationCheckMethodTraits<
    void (Class::*)(const Node &, const ValidationAcceptor &) const> {
  using ClassType = Class;
  using NodeType = Node;
  static constexpr bool acceptsCancellation = false;
};

template <typename Class, typename Node>
struct ValidationCheckMethodTraits<
    void (Class::*)(const Node &, const ValidationAcceptor &,
                    const utils::CancellationToken &)> {
  using ClassType = Class;
  using NodeType = Node;
  static constexpr bool acceptsCancellation = true;
};

template <typename Class, typename Node>
struct ValidationCheckMethodTraits<
    void (Class::*)(const Node &, const ValidationAcceptor &,
                    const utils::CancellationToken &) const> {
  using ClassType = Class;
  using NodeType = Node;
  static constexpr bool acceptsCancellation = true;
};

template <auto Method>
using ValidationCheckMethodClass =
    typename ValidationCheckMethodTraits<decltype(Method)>::ClassType;

template <auto Method>
using ValidationCheckMethodNode =
    typename ValidationCheckMethodTraits<decltype(Method)>::NodeType;

template <auto Method, typename Object>
concept BoundValidationCheckMethod =
    requires { typename ValidationCheckMethodTraits<decltype(Method)>; } &&
    std::derived_from<std::remove_cvref_t<Object>,
                      ValidationCheckMethodClass<Method>>;

template <auto Method>
inline constexpr bool kValidationCheckMethodAcceptsCancellation =
    ValidationCheckMethodTraits<decltype(Method)>::acceptsCancellation;

template <typename Node, typename Check>
concept TypedValidationCheckCallable =
    std::derived_from<std::remove_cvref_t<Node>, AstNode> &&
    (std::invocable<Check &, const Node &, const ValidationAcceptor &> ||
     std::invocable<Check &, const Node &, const ValidationAcceptor &,
                    const utils::CancellationToken &>);

template <typename Check>
concept NullableValidationCallable =
    requires(const Check &check) { static_cast<bool>(check); };

template <typename Check>
[[nodiscard]] bool hasValidationCallable(const Check &check) {
  if constexpr (NullableValidationCallable<Check>) {
    return static_cast<bool>(check);
  }
  return true;
}

template <typename Node, typename Check>
void invokeValidationCheck(Check &check, const Node &node,
                           const ValidationAcceptor &acceptor,
                           const utils::CancellationToken &cancelToken) {
  if constexpr (std::invocable<Check &, const Node &,
                               const ValidationAcceptor &,
                               const utils::CancellationToken &>) {
    std::invoke(check, node, acceptor, cancelToken);
  } else {
    std::invoke(check, node, acceptor);
  }
}

/// Runtime "is `node` an instance of `Node`?" test, captured as a plain
/// function pointer. Used only to dispatch a check whose target is an abstract
/// base the grammar never named (absent from reflection), where the check
/// cannot be pre-indexed by concrete subtype.
template <typename Node>
[[nodiscard]] bool nodeIsInstanceOf(const AstNode &node) noexcept {
  return dynamic_cast<const Node *>(&node) != nullptr;
}

} // namespace detail

/// Runtime is-instance predicate for a check's target type.
using ValidationCheckMatches = bool (*)(const AstNode &) noexcept;

/// Built-in category for inexpensive validation checks.
inline constexpr std::string_view kFastValidationCategory = "fast";
/// Built-in category for expensive validation checks.
inline constexpr std::string_view kSlowValidationCategory = "slow";
/// Built-in category for parser/linker diagnostics emitted by the runtime.
inline constexpr std::string_view kBuiltInValidationCategory = "built-in";

// Stable diagnostic codes the runtime itself emits (as opposed to
// language-specific validation checks) — compare `Diagnostic::code` against
// these instead of hardcoding the string. `Diagnostic::code` holds a
// `std::string`, so these are `std::string` rather than `string_view` — every
// call site builds a `DiagnosticCode`/`std::string` from one directly.
inline const std::string kParseConversionErrorCode = "parse.conversion";
inline const std::string kParseIncompleteCode = "parse.incomplete";
inline const std::string kParseInsertedCode = "parse.inserted";
inline const std::string kParseDeletedCode = "parse.deleted";
inline const std::string kParseReplacedCode = "parse.replaced";
inline const std::string kParseRecoveredCode = "parse.recovered";
inline const std::string kLinkingUnresolvedReferenceCode =
    "linking.unresolved-reference";

/// Type-erased validation check runnable on any AST node.
using ValidationCheck =
    std::function<void(const AstNode &, const ValidationAcceptor &,
                       const utils::CancellationToken &)>;
/// Type-erased validation preparation run before or after AST traversal.
using ValidationPreparation =
    std::function<void(const AstNode &, const ValidationAcceptor &,
                       std::span<const std::string>,
                       const utils::CancellationToken &)>;

/// Registry of validation checks grouped by target type and category.
class ValidationRegistry {
public:
  /// Type-erased registration payload for one validation check.
  struct ValidationCheckRegistration {
    std::type_index targetType = std::type_index(typeid(AstNode));
    ValidationCheck check;
    /// Runtime is-instance test for `targetType`, used only when the target is
    /// an abstract base the grammar never named (absent from reflection) and
    /// the check therefore cannot be pre-indexed by concrete subtype. Null for
    /// the empty registration.
    ValidationCheckMatches matches = nullptr;
    /// When true, the check runs only for nodes whose dynamic type is exactly
    /// `targetType`; when false (default), it also runs for every subtype.
    /// Set by `makeExactValidationCheck` / `registerExactCheck`.
    bool exactTypeOnly = false;
  };

  virtual ~ValidationRegistry() noexcept = default;

  template <typename Node, typename Check>
    requires detail::TypedValidationCheckCallable<std::remove_cvref_t<Node>,
                                                  std::remove_cvref_t<Check>>
  void registerCheck(Check &&check,
      std::string_view category = kFastValidationCategory) {
    registerTypedCheck(
        makeValidationCheck<std::remove_cvref_t<Node>>(std::forward<Check>(check)),
        category);
  }

  /// Like `registerCheck`, but the check runs only for nodes whose dynamic type
  /// is exactly `Node` — not for its subtypes.
  template <typename Node, typename Check>
    requires detail::TypedValidationCheckCallable<std::remove_cvref_t<Node>,
                                                  std::remove_cvref_t<Check>>
  void registerExactCheck(Check &&check,
      std::string_view category = kFastValidationCategory) {
    registerTypedCheck(
        makeExactValidationCheck<std::remove_cvref_t<Node>>(
            std::forward<Check>(check)),
        category);
  }

  template <typename Node, typename Check>
    requires detail::TypedValidationCheckCallable<std::remove_cvref_t<Node>,
                                                  std::remove_cvref_t<Check>>
  [[nodiscard]] static ValidationCheckRegistration
  makeValidationCheck(Check &&check) {
    return makeTypedCheckRegistration<std::remove_cvref_t<Node>>(
        std::forward<Check>(check), /*exactTypeOnly=*/false);
  }

  /// Like `makeValidationCheck`, but the check runs only for nodes whose
  /// dynamic type is exactly `Node` — not for its subtypes. The result is the
  /// same `ValidationCheckRegistration` type, so both may be mixed in a single
  /// `registerChecks({...})` call.
  template <typename Node, typename Check>
    requires detail::TypedValidationCheckCallable<std::remove_cvref_t<Node>,
                                                  std::remove_cvref_t<Check>>
  [[nodiscard]] static ValidationCheckRegistration
  makeExactValidationCheck(Check &&check) {
    return makeTypedCheckRegistration<std::remove_cvref_t<Node>>(
        std::forward<Check>(check), /*exactTypeOnly=*/true);
  }

  // Binds @p Method to a validator owned elsewhere (e.g. by the service
  // container, as `services.validator`). The registry does NOT take ownership:
  // @p validator must outlive the registry's checks. Binding by reference —
  // rather than copying the validator — keeps every check on the one owned
  // instance, so a validator may safely hold per-build state and need not be
  // copyable. A temporary cannot bind here, which rules out a dangling check.
  template <auto Method, typename Object>
    requires detail::BoundValidationCheckMethod<Method, Object>
  [[nodiscard]] static ValidationCheckRegistration
  makeValidationCheck(Object &validator) {
    using Node =
        std::remove_cvref_t<detail::ValidationCheckMethodNode<Method>>;
    return makeTypedCheckRegistration<Node>(
        makeBoundCheckLambda<Method>(validator), /*exactTypeOnly=*/false);
  }

  /// Exact-type-only variant of the method-binding `makeValidationCheck`.
  template <auto Method, typename Object>
    requires detail::BoundValidationCheckMethod<Method, Object>
  [[nodiscard]] static ValidationCheckRegistration
  makeExactValidationCheck(Object &validator) {
    using Node =
        std::remove_cvref_t<detail::ValidationCheckMethodNode<Method>>;
    return makeTypedCheckRegistration<Node>(
        makeBoundCheckLambda<Method>(validator), /*exactTypeOnly=*/true);
  }

  void registerChecks(
      std::initializer_list<ValidationCheckRegistration> checks,
      std::string_view category = kFastValidationCategory) {
    for (const auto &check : checks) {
      registerTypedCheck(check, category);
    }
  }

  // Validation categories currently registered.
  [[nodiscard]] virtual std::vector<std::string>
  getAllValidationCategories() const = 0;

  virtual void registerBeforeDocument(ValidationPreparation check) = 0;
  virtual void registerAfterDocument(ValidationPreparation check) = 0;

  /// Returns the registered "before document" preparation hooks, in
  /// registration order.
  [[nodiscard]] virtual std::span<const ValidationPreparation>
  checksBefore() const noexcept = 0;

  /// Returns the registered "after document" preparation hooks, in
  /// registration order.
  [[nodiscard]] virtual std::span<const ValidationPreparation>
  checksAfter() const noexcept = 0;

  /// Runs every check applicable to `node` (by dynamic type) whose category
  /// is enabled by `categories` (empty = every category). Caught exceptions
  /// become validation diagnostics, except `OperationCancelled` which
  /// propagates.
  ///
  /// The `categories` span is only read during the call; the registry keeps no
  /// per-call state, so it is safe to share across documents validated in
  /// parallel.
  virtual void runChecks(const AstNode &node,
                         const ValidationAcceptor &acceptor,
                         std::span<const std::string> categories,
                         const utils::CancellationToken &cancelToken) const = 0;

private:
  // Shared body of the four factory overloads: type-erase `check` for `Node`
  // and stamp the dispatch scope. The static_cast in the stored check is sound
  // because dispatch only ever calls it for a node that is-a `Node`.
  template <typename Node, typename Check>
  [[nodiscard]] static ValidationCheckRegistration
  makeTypedCheckRegistration(Check &&check, bool exactTypeOnly) {
    using StoredCheck = std::remove_cvref_t<Check>;
    if (!detail::hasValidationCallable(check)) {
      return {};
    }

    auto sharedCheck =
        std::make_shared<StoredCheck>(std::forward<Check>(check));
    return ValidationCheckRegistration{
        .targetType = std::type_index(typeid(Node)),
        .check = [sharedCheck](const AstNode &node,
                               const ValidationAcceptor &acceptor,
                               const utils::CancellationToken &cancelToken) {
          detail::invokeValidationCheck(
              *sharedCheck, static_cast<const Node &>(node), acceptor,
              cancelToken);
        },
        .matches = &detail::nodeIsInstanceOf<Node>,
        .exactTypeOnly = exactTypeOnly};
  }

  // Wraps @p Method bound to @p validator (non-owning) as a check callable,
  // shared by the subtype and exact-type method-binding factories.
  template <auto Method, typename Object>
  [[nodiscard]] static auto makeBoundCheckLambda(Object &validator) {
    using Node =
        std::remove_cvref_t<detail::ValidationCheckMethodNode<Method>>;
    auto *bound = std::addressof(validator);
    return [bound](const Node &node, const ValidationAcceptor &acceptor,
                   const utils::CancellationToken &cancelToken) {
      if constexpr (detail::kValidationCheckMethodAcceptsCancellation<Method>) {
        std::invoke(Method, *bound, node, acceptor, cancelToken);
      } else {
        std::invoke(Method, *bound, node, acceptor);
      }
    };
  }

  virtual void registerTypedCheck(ValidationCheckRegistration registration,
                                  std::string_view category) = 0;
};

} // namespace pegium::validation
