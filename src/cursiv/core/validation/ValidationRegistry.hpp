#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <stdexcept>
#include <type_traits>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "cursiv/core/validation/ValidationCategory.hpp"

// Which check runs on which node, and in which pass: a check is filed under
// a tag (the node, read off the method's signature) and a category (the
// pass, given to `registerChecks`). Three laws guard the split, checked
// where each is judged: the method's shape at compile time; the watched
// type and the reserved `built_in` category at registration.

namespace cursiv {

struct Document;
struct ValidationCheckRegistration;

namespace detail {

// What a check is, read off its pointer. Two shapes differing only in the third
// parameter: a check that walks a whole subtree wants to be interruptible, one
// that looks at a single node has nothing to poll. A pointer matching neither
// leaves `node` undeclared, which is law 1.
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
concept IsCheck = requires { typename check_traits<M>::node; } &&
                  std::is_base_of_v<AstNode, typename check_traits<M>::node>;

// The class of any method pointer, check-shaped or not, so a mis-shaped one
// reaches the laws below instead of failing deduction with no message.
template <class M>
struct method_class;
template <class R, class V, class... A>
struct method_class<R (V::*)(A...)> {
  using type = V;
};
template <class R, class V, class... A>
struct method_class<R (V::*)(A...) const> {
  using type = V;
};
template <auto... Ms>
using validator_of = typename method_class<decltype((Ms, ...))>::type;

}  // namespace detail

// One check, erased. The acceptor is non-const because that is the shape a
// discovered method takes, and bridging the two would need a `const_cast`.
using ValidationCheck = std::function<void(const AstNode&, ValidationAcceptor&,
                                           const CancellationToken&)>;

// A document-level check, run once per pass around the per-node walk. This is
// where cross-node state lives: the per-node checks are copies fanning out over
// documents, so a check that has to see the whole document at once -- two
// declarations wearing one name -- belongs here, its state on its own stack.
using ValidationPreparation = std::function<void(
    const Document&, ValidationAcceptor&, const CancellationToken&)>;

// A hook declares its category at registration, as a per-node check declares
// one in its return type; it is never handed the pass's list to filter itself
// against. The incremental build has to know which categories a hook belongs
// to, or a hooks-only language would never be owed a pass.
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
// path passes: a document is not `Validated` until everything it owes has run.
using ValidationCategories = std::span<const ValidationCategory* const>;

namespace detail {

// One discovered check. It is read from several documents at once, so what the
// validator carries must be safe to read concurrently: the check holds it by
// address, and any memo behind it answers for its own threading.
template <auto M, class V>
[[nodiscard]] ValidationCheckRegistration make_check(const V& held,
                                                     bool exactTypeOnly) {
  // Law 1 -- the shape.
  static_assert(
      IsCheck<decltype(M)>,
      "this looks like a validation check and is not shaped like one. A check "
      "is a CONST member function taking `(const T&, "
      "cursiv::ValidationAcceptor&)` -- optionally followed by `const "
      "cursiv::CancellationToken&` -- where T is an AST class of the grammar");

  using Traits = check_traits<decltype(M)>;
  using Node = typename Traits::node;

  // The base itself is "every node": the one target no grammar names, and the
  // entry carries a null tag rather than its own.
  constexpr bool kEveryNode = std::same_as<Node, AstNode>;

  return ValidationCheckRegistration{
      kEveryNode ? nullptr : &cursiv::ast_type_tag<Node>, exactTypeOnly,
      [held = &held](const AstNode& node, ValidationAcceptor& accept,
                     const CancellationToken& token) {
        // The downcast is the filing's proof: the registry put this closure
        // under the tags of `Node`'s family and nowhere else.
        const Node& typed = static_cast<const Node&>(node);
        if constexpr (Traits::interruptible)
          (held->*M)(typed, accept, token);
        else
          (held->*M)(typed, accept);
      }};
}

}  // namespace detail

class ValidationRegistry {
 public:
  virtual ~ValidationRegistry() noexcept = default;

  // Files one discovered check under `category`. Called at bootstrap, on one
  // thread; after that the registry is read concurrently and never written.
  virtual void registerCheck(ValidationCheckRegistration,
                             const ValidationCategory* category = &fast) = 0;

  // ...and a language's whole table in one statement, which is how a module
  // reads. The category is stated here and nowhere else: it is a property of
  // when a check should run, which is the registration's business and not the
  // check's.
  void registerChecks(std::initializer_list<ValidationCheckRegistration> checks,
                      const ValidationCategory* category = &fast) {
    for (const ValidationCheckRegistration& check : checks)
      registerCheck(check, category);
  }

  // The instance is the container's, bound by address and never copied: a
  // validator is a service, defined once in the language's own services, so it
  // may hold per-build state and need not be copyable. The rvalue overloads
  // are deleted, so a temporary cannot bind and dangle.
  template <auto Method, class Object>
  [[nodiscard]] static ValidationCheckRegistration makeValidationCheck(
      const Object& validator) {
    return detail::make_check<Method>(validator, /*exactTypeOnly=*/false);
  }
  template <auto Method, class Object>
  static ValidationCheckRegistration makeValidationCheck(const Object&&) =
      delete;

  template <auto Method, class Object>
  [[nodiscard]] static ValidationCheckRegistration makeExactValidationCheck(
      const Object& validator) {
    return detail::make_check<Method>(validator, /*exactTypeOnly=*/true);
  }
  template <auto Method, class Object>
  static ValidationCheckRegistration makeExactValidationCheck(const Object&&) =
      delete;

  // `built_in` is refused: it names the runtime's own half. Same bootstrap law
  // as `registerCheck` -- registered once, on one thread, read forever.
  virtual void registerBeforeDocument(
      ValidationPreparation, const ValidationCategory* category = &fast) = 0;
  virtual void registerAfterDocument(
      ValidationPreparation, const ValidationCategory* category = &fast) = 0;
  // ...and what was filed, in registration order, for the validator to run
  // around the walk.
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

  // Zero is a real answer a caller acts on: a language with no check has no AST
  // walk to pay for.
  [[nodiscard]] virtual std::size_t checkCount() const noexcept = 0;

  // The category names in use, deduplicated, in first-registration order. What
  // a client is offered when it asks what it may ask for.
  [[nodiscard]] virtual std::span<const ValidationCategory* const> categories()
      const noexcept = 0;
};

}  // namespace cursiv
