#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "cursiv/validation/ValidationCategory.hpp"

// Which check runs on which node, and in which pass: a check is filed under a
// tag, read off its signature, and a category, given at registration. The
// shape is checked at compile time; the type and category at registration.

namespace cursiv {

struct Document;
struct ValidationCheckRegistration;

// One check, erased. The acceptor is non-const because that is the shape a
// discovered method takes, and bridging the two would need a `const_cast`.
using ValidationCheck = std::function<void(const AstNode&, ValidationAcceptor&,
                                           const CancellationToken&)>;

// A document-level check, run once per pass around the per-node walk. A check
// that must see the whole document at once, such as duplicate names, goes
// here, with its state on its own stack.
using ValidationPreparation = std::function<void(
    const Document&, ValidationAcceptor&, const CancellationToken&)>;

// A hook declares its category at registration, as a per-node check does in
// its return type: the incremental build must know which pass it belongs to.
struct ValidationPreparationEntry {
  const ValidationCategory* category = &fast;
  ValidationPreparation check;
};

// A registration the registry cannot honour. A throw, not an assert: the
// failure mode is a check that silently never runs, so the guard must survive
// `-DNDEBUG`.
class ValidationRegistrationError : public std::invalid_argument {
 public:
  using std::invalid_argument::invalid_argument;
};

// One discovered check, ready to be filed.
struct ValidationCheckRegistration {
  // The AST type it watches. Null is every node -- a check written over the
  // base, the one target no reflection can enumerate because a grammar's type
  // list does not contain it.
  const AstTypeTag* target = nullptr;
  // When set, the check runs only for nodes whose type is exactly `target`;
  // otherwise it runs for the subtypes too, which is what a check written over
  // a base means.
  bool exactTypeOnly = false;
  ValidationCheck check;
};

// Which categories a pass runs. Empty is every one, which is what the build
// path passes: a document is not `Validated` until all its checks have run.
using ValidationCategories = std::span<const ValidationCategory* const>;

class ValidationRegistry {
 public:
  virtual ~ValidationRegistry() noexcept = default;

  // Files one discovered check under `category`. Called at bootstrap, on one
  // thread; after that the registry is read concurrently and never written.
  virtual void registerCheck(ValidationCheckRegistration,
                             const ValidationCategory* category = &fast) = 0;

  // A language's whole table in one call, under one category.
  void registerChecks(std::initializer_list<ValidationCheckRegistration> checks,
                      const ValidationCategory* category = &fast) {
    for (const ValidationCheckRegistration& check : checks)
      registerCheck(check, category);
  }

  // The instance is the container's, bound by address and never copied. The
  // rvalue overloads are deleted, so a temporary cannot bind and dangle.
  template <auto Method, class Object>
  [[nodiscard]] static ValidationCheckRegistration makeValidationCheck(
      const Object& validator) {
    return make_check<Method>(validator, /*exactTypeOnly=*/false);
  }
  template <auto Method, class Object>
  static ValidationCheckRegistration makeValidationCheck(const Object&&) =
      delete;

  template <auto Method, class Object>
  [[nodiscard]] static ValidationCheckRegistration makeExactValidationCheck(
      const Object& validator) {
    return make_check<Method>(validator, /*exactTypeOnly=*/true);
  }
  template <auto Method, class Object>
  static ValidationCheckRegistration makeExactValidationCheck(const Object&&) =
      delete;

  // `built_in` is refused: it names the runtime's own half. Same rule as
  // `registerCheck`: registered once, on one thread, then only read.
  virtual void registerBeforeDocument(
      ValidationPreparation, const ValidationCategory* category = &fast) = 0;
  virtual void registerAfterDocument(
      ValidationPreparation, const ValidationCategory* category = &fast) = 0;
  // What was filed, in registration order, for the validator to run around
  // the walk.
  [[nodiscard]] virtual std::span<const ValidationPreparationEntry>
  checksBefore() const noexcept = 0;
  [[nodiscard]] virtual std::span<const ValidationPreparationEntry>
  checksAfter() const noexcept = 0;

  // Every check applicable to `node` whose category `categories` admits; one
  // that throws does not abort the pass. Const and shared, the documents of a
  // build being validated in parallel against this one registry.
  virtual void runChecks(const AstNode& node, ValidationAcceptor&,
                         ValidationCategories,
                         const CancellationToken&) const = 0;

  // What a check that threw becomes; a cancellation never comes here. By
  // default, an error "`context`: what it threw" on `node` (null for a document
  // check) or the document, and a warning to the sink. May rethrow `failure`.
  virtual void handleException(std::exception_ptr failure,
                               std::string_view context,
                               ValidationAcceptor& accept,
                               const AstNode* node) const = 0;

  // Zero is a real answer a caller acts on: a language with no check has no AST
  // walk to pay for.
  [[nodiscard]] virtual std::size_t checkCount() const noexcept = 0;

  // The category names in use, deduplicated, in first-registration order. What
  // a client is offered when it asks what it may ask for.
  [[nodiscard]] virtual std::span<const ValidationCategory* const> categories()
      const noexcept = 0;

 private:
  // A check's shape, read off its pointer: the third parameter is a token for a
  // check that walks a subtree, absent for one that looks at a single node.
  template <class M>
  struct check_traits {};
  template <class V, class Node>
  struct check_traits<void (V::*)(const Node&, ValidationAcceptor&) const> {
    using validator = V;
    using node = Node;
    static constexpr bool interruptible = false;
  };
  template <class V, class Node>
  struct check_traits<void (V::*)(const Node&, ValidationAcceptor&,
                                  const CancellationToken&) const> {
    using validator = V;
    using node = Node;
    static constexpr bool interruptible = true;
  };

  template <class M>
  static constexpr bool is_check = requires {
    typename check_traits<M>::node;
    requires std::is_base_of_v<AstNode, typename check_traits<M>::node>;
  };

  // One discovered check. It is read from several documents at once, so what
  // the validator carries must be safe to read concurrently: the check holds it
  // by address, and any memo behind it answers for its own threading.
  template <auto M, class V>
  [[nodiscard]] static ValidationCheckRegistration make_check(
      const V& held, bool exactTypeOnly) {
    static_assert(
        is_check<decltype(M)>,
        "this looks like a validation check and is not shaped like one. A "
        "check is a CONST member function taking `(const T&, "
        "cursiv::ValidationAcceptor&)` -- optionally followed by `const "
        "cursiv::CancellationToken&` -- where T is an AST class of the "
        "grammar");

    using Traits = check_traits<decltype(M)>;
    using Node = typename Traits::node;

    // The base itself is "every node": the one target no grammar names, and
    // the entry carries a null tag rather than its own.
    constexpr bool kEveryNode = std::same_as<Node, AstNode>;

    return ValidationCheckRegistration{
        kEveryNode ? nullptr : &cursiv::ast_type_tag<Node>, exactTypeOnly,
        [held = &held](const AstNode& node, ValidationAcceptor& accept,
                       const CancellationToken& token) {
          // The downcast is the filing's proof: the registry put this closure
          // under the tags of `Node`'s family and nowhere else.
          const auto& typed = *detail::ast_cast_unchecked<const Node>(&node);
          if constexpr (Traits::interruptible)
            (held->*M)(typed, accept, token);
          else
            (held->*M)(typed, accept);
        }};
  }
};

}  // namespace cursiv
