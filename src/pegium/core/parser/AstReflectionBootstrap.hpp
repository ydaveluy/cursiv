#pragma once

#include <cassert>
#include <optional>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/ParserRule.hpp>
#include <pegium/core/parser/RecoveryAnalysis.hpp>
#include <pegium/core/syntax-tree/AstNode.hpp>
#include <pegium/core/syntax-tree/AstReflection.hpp>
#include <pegium/core/utils/TypeIndexHash.hpp>

namespace pegium::parser {

struct AstReflectionInitContext;

namespace detail {

template <typename Expr>
concept HasInitImpl =
    requires(const Expr &expression, AstReflectionInitContext &ctx) {
      expression.init_impl(ctx);
    };

[[nodiscard]] inline std::type_index invalid_type() noexcept {
  return std::type_index(typeid(void));
}

[[nodiscard]] inline bool is_valid_type(std::type_index type) noexcept {
  return type != invalid_type();
}

/// The `NamedAstNode` mixin type index. It is a C++ base, not a grammar type, so
/// it is registered explicitly (via the per-type `derivesFromNamedAstNode` flag)
/// rather than discovered through the grammar — letting `is_a<NamedAstNode>` /
/// `ast_ptr_cast<NamedAstNode>` resolve names through the O(1) reflection
/// `isSubtype` instead of a per-node `dynamic_cast`.
[[nodiscard]] inline std::type_index named_ast_node_type() noexcept {
  return std::type_index(typeid(NamedAstNode));
}

struct AstNodeTypeInfo {
  std::type_index type = invalid_type();
  bool (*isInstance)(const AstNode &) noexcept = nullptr;
  const AstNode &(*probe)() noexcept = nullptr;
};

struct AssignmentReflectionInfo {
  const AstNodeTypeInfo *assignedAstType = nullptr;
  const AstNodeTypeInfo *referenceTargetType = nullptr;
};

template <typename T>
[[nodiscard]] bool is_ast_instance(const AstNode &node) noexcept {
  return dynamic_cast<const T *>(&node) != nullptr;
}

template <typename T>
[[nodiscard]] const AstNode &ast_probe() noexcept {
  static const T probe{};
  return probe;
}

template <typename T>
  requires std::derived_from<T, AstNode>
[[nodiscard]] consteval auto ast_probe_fn() noexcept
    -> const AstNode &(*)() noexcept {
  if constexpr (DefaultConstructibleAstNode<T>) {
    return &ast_probe<T>;
  } else {
    return nullptr;
  }
}

/// Reflection bootstrap needs probing only for concrete produced AST nodes.
/// Abstract or non-default-constructible AST supertypes can still participate
/// in metadata as reference targets or expected assignment types because their
/// RTTI-based `isInstance(...)` check remains valid without a probe object.
template <typename T>
  requires std::derived_from<T, AstNode>
[[nodiscard]] inline const AstNodeTypeInfo &ast_node_type_info() noexcept {
  static const AstNodeTypeInfo info{
      .type = std::type_index(typeid(T)),
      .isInstance = &is_ast_instance<T>,
      .probe = ast_probe_fn<T>(),
  };
  return info;
}

struct VisitedKey {
  const grammar::AbstractElement *element = nullptr;
  std::type_index expectedType = invalid_type();

  [[nodiscard]] bool operator==(const VisitedKey &) const noexcept = default;
};

struct VisitedKeyHash {
  [[nodiscard]] std::size_t operator()(const VisitedKey &key) const noexcept {
    const auto elementHash = std::hash<const void *>{}(key.element);
    const auto typeHash =
        utils::FastTypeIndexHash{}(key.expectedType);
    return elementHash ^ (typeHash + 0x9e3779b9U + (elementHash << 6U) +
                          (elementHash >> 2U));
  }
};

struct DirectSubtypeEdge {
  std::type_index subtype = invalid_type();
  std::type_index supertype = invalid_type();

  [[nodiscard]] bool operator==(const DirectSubtypeEdge &) const noexcept =
      default;
};

struct DirectSubtypeEdgeHash {
  [[nodiscard]] std::size_t
  operator()(const DirectSubtypeEdge &edge) const noexcept {
    const auto subtypeHash = utils::FastTypeIndexHash{}(edge.subtype);
    const auto supertypeHash = utils::FastTypeIndexHash{}(edge.supertype);
    return subtypeHash ^ (supertypeHash + 0x9e3779b9U + (subtypeHash << 6U) +
                          (subtypeHash >> 2U));
  }
};

class AstReflectionBootstrapState {
public:
  explicit AstReflectionBootstrapState(AstReflection &reflection) noexcept
      : _reflection(std::addressof(reflection)) {}

  [[nodiscard]] bool beginVisit(const grammar::AbstractElement &element,
                                std::optional<std::type_index> expectedType) {
    return _visited
        .emplace(VisitedKey{.element = std::addressof(element),
                            .expectedType = expectedType.value_or(invalid_type())})
        .second;
  }

  void registerProducedType(const AstNodeTypeInfo &typeInfo,
                            std::optional<std::type_index> expectedType) {
    if (!is_valid_type(typeInfo.type)) {
      return;
    }

    registerKnownType(typeInfo.type);
    _producedTypesByType.try_emplace(typeInfo.type, std::addressof(typeInfo));

    if (expectedType.has_value() && is_valid_type(*expectedType) &&
        typeInfo.type != *expectedType) {
      addDirectSubtypeEdge(typeInfo.type, *expectedType);
    }
  }

  void registerAssignment(const AssignmentReflectionInfo &metadata) {
    if (metadata.assignedAstType != nullptr &&
        is_valid_type(metadata.assignedAstType->type)) {
      registerKnownType(metadata.assignedAstType->type);
    }

    if (metadata.referenceTargetType != nullptr &&
        is_valid_type(metadata.referenceTargetType->type)) {
      registerKnownType(metadata.referenceTargetType->type);
      _referenceAssignments.push_back(metadata.referenceTargetType);
    }
  }

  void finalize() {
    if (_finalized) {
      return;
    }
    _finalized = true;
    finalizeProducedTypeInheritanceEdges();
    finalizeReferenceInducedEdges();
    finalizeNamedMixinEdges();
  }

private:
  void registerKnownType(std::type_index type) {
    if (!is_valid_type(type) || !_knownTypes.insert(type).second) {
      return;
    }
    _reflection->registerType(type);
  }

  void addDirectSubtypeEdge(std::type_index subtype,
                            std::type_index supertype) {
    if (!is_valid_type(subtype) || !is_valid_type(supertype) ||
        subtype == supertype) {
      registerKnownType(subtype);
      registerKnownType(supertype);
      return;
    }

    if (const DirectSubtypeEdge edge{.subtype = subtype, .supertype = supertype};
        !_directSubtypeEdges.insert(edge).second) {
      return;
    }

    registerKnownType(subtype);
    registerKnownType(supertype);
    _reflection->registerSubtype(subtype, supertype);
  }

  // NamedAstNode is a C++ mixin, not a grammar type, so the grammar induces no
  // subtype edge to it. Probe each produced type once (a single dynamic_cast per
  // type at bootstrap, off the hot path) and record the `type -> NamedAstNode`
  // edge for matches, so name lookup resolves via the O(1) reflection isSubtype
  // (ast_ptr_cast) instead of a per-node dynamic_cast. Mirrors
  // finalizeReferenceInducedEdges.
  void finalizeNamedMixinEdges() {
    for (const auto &[producedType, typeInfo] : _producedTypesByType) {
      if (typeInfo == nullptr || typeInfo->probe == nullptr) {
        continue;
      }
      if (is_ast_instance<NamedAstNode>(typeInfo->probe())) {
        addDirectSubtypeEdge(producedType, named_ast_node_type());
      }
    }
  }

  // Two produced types can be related by C++ inheritance without the grammar
  // ever nesting one where the other is expected — e.g. sibling rules for
  // `Class` and `Exception : Class`. The grammar then induces no edge between
  // them, so `isSubtype`/`getAllSubTypes` (and thus `ast_ptr_cast`, name
  // lookup, and validation dispatch on a base type) would miss the relation.
  // Probe every produced instance against every other produced type and record
  // the real is-a edges, so reflection mirrors the true C++ hierarchy rather
  // than only the grammar-induced subset. O(produced^2) RTTI probes, once at
  // bootstrap and off the hot path — the same probe finalizeReferenceInduced /
  // NamedMixin edges already use, generalized to every produced pair.
  void finalizeProducedTypeInheritanceEdges() {
    for (const auto &[subtype, subtypeInfo] : _producedTypesByType) {
      if (subtypeInfo == nullptr || subtypeInfo->probe == nullptr) {
        continue;
      }
      const AstNode &probe = subtypeInfo->probe();
      for (const auto &[supertype, supertypeInfo] : _producedTypesByType) {
        if (supertypeInfo == nullptr || subtype == supertype ||
            supertypeInfo->isInstance == nullptr) {
          continue;
        }
        if (supertypeInfo->isInstance(probe)) {
          addDirectSubtypeEdge(subtype, supertype);
        }
      }
    }
  }

  void finalizeReferenceInducedEdges() {
    for (const auto *targetTypeInfo : _referenceAssignments) {
      if (targetTypeInfo == nullptr ||
          !is_valid_type(targetTypeInfo->type) ||
          targetTypeInfo->isInstance == nullptr) {
        continue;
      }
      for (const auto &[producedType, typeInfo] : _producedTypesByType) {
        if (typeInfo == nullptr || producedType == targetTypeInfo->type ||
            typeInfo->probe == nullptr) {
          continue;
        }
        if (targetTypeInfo->isInstance(typeInfo->probe())) {
          addDirectSubtypeEdge(producedType, targetTypeInfo->type);
        }
      }
    }
  }

  AstReflection *_reflection = nullptr;
  std::unordered_set<VisitedKey, VisitedKeyHash> _visited;
  std::unordered_map<std::type_index, const AstNodeTypeInfo *,
                     utils::FastTypeIndexHash, utils::FastTypeIndexEqual>
      _producedTypesByType;
  std::vector<const AstNodeTypeInfo *> _referenceAssignments;
  std::unordered_set<std::type_index, utils::FastTypeIndexHash,
                     utils::FastTypeIndexEqual>
      _knownTypes;
  std::unordered_set<DirectSubtypeEdge, DirectSubtypeEdgeHash>
      _directSubtypeEdges;
  bool _finalized = false;
};

struct InitAccess {
  template <typename Expr>
    requires HasInitImpl<Expr>
  static void init(const Expr &expression, AstReflectionInitContext &ctx) {
    expression.init_impl(ctx);
  }

  template <typename Expr>
    requires(!HasInitImpl<Expr>)
  static void init(const Expr &, AstReflectionInitContext &) {
    /* no init step: this expression type has no init_impl */
  }
};

} // namespace detail

struct AstReflectionInitContext {
  detail::AstReflectionBootstrapState *state = nullptr;
  std::optional<std::type_index> expectedType;

  [[nodiscard]] bool beginVisit(const grammar::AbstractElement &element) const {
    assert(state != nullptr);
    return state->beginVisit(element, expectedType);
  }

  void registerProducedType(const detail::AstNodeTypeInfo &typeInfo) const {
    assert(state != nullptr);
    state->registerProducedType(typeInfo, expectedType);
  }

  void registerAssignment(
      const detail::AssignmentReflectionInfo &metadata) const {
    assert(state != nullptr);
    state->registerAssignment(metadata);
  }

  [[nodiscard]] AstReflectionInitContext
  withExpectedType(std::type_index nextExpectedType) const noexcept {
    auto copy = *this;
    copy.expectedType = nextExpectedType;
    return copy;
  }

  [[nodiscard]] AstReflectionInitContext withoutExpectedType() const noexcept {
    auto copy = *this;
    copy.expectedType = std::nullopt;
    return copy;
  }
};

template <typename Expr>
  requires std::derived_from<std::remove_cvref_t<Expr>, grammar::AbstractElement>
inline void init(const Expr &expression, AstReflectionInitContext &ctx) {
  if (!ctx.beginVisit(expression)) {
    return;
  }
  detail::InitAccess::init(expression, ctx);
}

inline void bootstrapAstReflection(const grammar::ParserRule &entryRule,
                                   AstReflection &reflection) {
  detail::AstReflectionBootstrapState state{reflection};
  AstReflectionInitContext initContext{.state = std::addressof(state)};
  entryRule.init(initContext);
  state.finalize();
  // Cache the grammar-derived word-character table on the entry rule while
  // registration is still single-threaded: concurrent parses afterwards only
  // read it (`word_char_mask_for`).
  entryRule.initializeRecoveryWordCharTable(
      detail::derive_word_char_mask(entryRule).table);
}

} // namespace pegium::parser
