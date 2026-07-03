#pragma once

/// Parser rule template implementing precedence-aware infix parsing
/// (Pratt/precedence-climbing).
///
/// `InfixOperator<Element, Assoc>` (declaring one operator, e.g. via
/// `LeftAssociation(...)`) stays entirely in-class below: its `fast_probe_impl`
/// and `parse_impl` are thin, mode-agnostic calls into
/// `parser::attempt_fast_probe`/`detail::parse_wrapped_node`/
/// `detail::parse_overriding_first_child`, shared across every mode, so
/// splitting it would separate no meaningful logic.
///
/// The `InfixRule` wrapper (the type-erased `Ops` table, ctor/copy/move/
/// reset, and the public grammar::InfixRule API) and its nested
/// `Model<Element, Operators...>` mostly stay in-class too: `Model::rule`/
/// `ruleTracked` (the strict entry points), the nested `ParseEngine`'s
/// `rule`/`parse_tail_strict`/`try_match_operator_strict` (the strict Pratt
/// loop), and `has_operator_fast_probe` are defined in-class. That last one
/// is genuinely shared, not strict-only: the erased recovery engine's
/// `operatorFastProbe` trampoline (built by `make_tail_ops`) calls it too, so
/// it stays wherever both callers can reach it rather than being forced into
/// either mode-specific partial.
///
/// Recovery-mode members (`InfixRule::probeRecoverable`, `Model::recover`,
/// `Model::probeRecoverable`, and `ParseEngine::recover`/`make_tail_ops`/
/// `try_match_operator_editable` — the last one is recovery-only despite
/// living alongside `try_match_operator_strict`, since its only caller is the
/// `matchOperatorEditable` trampoline feeding the erased tail-recovery engine
/// in InfixTailRecoveryEngine.cpp) are declared here and defined out-of-class
/// in InfixRuleRecovery.hpp. Expect-mode members (`Model::expect` and
/// `ParseEngine::expect`/`parse_tail_expect`) are declared here and defined
/// out-of-class in InfixRuleExpect.hpp; the expect-mode operator matching
/// itself (merging an operator's blocked frontier, replaying the winning
/// operator) already lives externally in InfixRuleSupport.hpp's
/// `InfixRuleExpectSupport`, so `parse_tail_expect` is mostly a thin loop
/// calling into that support struct. A translation unit that drives an
/// InfixRule through recovery or expect mode must include the matching
/// partial header in addition to this one.

#include <array>
#include <concepts>
#include <memory>
#include <optional>
#include <pegium/core/grammar/Assignment.hpp>
#include <pegium/core/grammar/InfixRule.hpp>
#include <pegium/core/grammar/ParserRule.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/ExpectFrontier.hpp>
#include <pegium/core/parser/InfixRuleSupport.hpp>
#include <pegium/core/parser/InfixTailRecoveryEngine.hpp>
#include <pegium/core/parser/NodeParseHelpers.hpp>
#include <pegium/core/parser/ParseAttempt.hpp>
#include <pegium/core/parser/RecoveryContext.hpp>
#include <pegium/core/parser/ParseExpression.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <pegium/core/parser/ParserRule.hpp>
#include <pegium/core/parser/RawValueTraits.hpp>
#include <pegium/core/parser/RecoveryCandidate.hpp>
#include <pegium/core/parser/RecoveryUtils.hpp>
#include <pegium/core/parser/RecoveryTrace.hpp>
#include <pegium/core/parser/StepTrace.hpp>
#include <pegium/core/parser/AstReflectionBootstrap.hpp>
#include <pegium/core/parser/ValueBuildContext.hpp>
#include <string>
#include <type_traits>
#include <utility>

namespace pegium::parser {
template <NonNullableExpression Element,
          grammar::InfixOperator::Associativity Assoc>
struct InfixOperator : grammar::InfixOperator {
private:
  static constexpr bool operator_has_required_raw_values =
      detail::expression_raw_compliant_v<Element>;

public:
  static constexpr bool nullable = false;
  static constexpr bool isFailureSafe =
      !IsOrderedChoice<Element>::value &&
      std::remove_cvref_t<Element>::isFailureSafe;
  static constexpr Associativity kAssociativity = Assoc;
  using ElementType = Element;

  static_assert(
      operator_has_required_raw_values,
      "InfixOperator requires getRawValue(node): direct element must provide "
      "a specific typed value (not grammar::RuleValue); for OrderedChoice, "
      "each choice must provide getRawValue(node) with a specific typed value.");

  explicit constexpr InfixOperator(Element &&element)
    requires(!std::is_lvalue_reference_v<Element>)
      : _element(std::move(element)) {}

  explicit constexpr InfixOperator(Element element)
    requires(std::is_lvalue_reference_v<Element>)
      : _element(element) {}

  constexpr InfixOperator(InfixOperator &&) noexcept = default;
  constexpr InfixOperator(const InfixOperator &) = default;
  constexpr InfixOperator &operator=(InfixOperator &&) noexcept = default;
  constexpr InfixOperator &operator=(const InfixOperator &) = default;

  Associativity getAssociativity() const noexcept override {
    return kAssociativity;
  }
  std::int32_t getPrecedence() const noexcept override { return _precedence; }
  const grammar::AbstractElement *getOperator() const noexcept override {
    return std::addressof(_element);
  }

  template <typename Visitor>
  bool visitRawValue(const CstNodeView &node, const ValueBuildContext &context,
                     Visitor &&visitor) const {
    return detail::InfixOperatorValueSupport<Element>::visit_raw_value(
        _element, node, context, std::forward<Visitor>(visitor));
  }

  constexpr void setPrecedence(std::int32_t p) noexcept { _precedence = p; }
  constexpr bool isNullable() const noexcept override {
    return nullable;
  }

  const char *terminal(const char *begin) const noexcept
    requires TerminalCapableExpression<Element>
  {
    return _element.terminal(begin);
  }

private:
  friend struct detail::ParseAccess;
  friend struct detail::FastProbeAccess;
  friend struct detail::InitAccess;

  void init_impl(AstReflectionInitContext &ctx) const { parser::init(_element, ctx); }

  // Strict-mode (ParseContext/TrackedParseContext) and recovery-mode operator
  // lookaheads share one fast-probe body. `has_operator_fast_probe` sits on the
  // strict infix hot path: when the wrapped operator expression already exposes
  // a specialized fast probe (for example literals or ordered choices of
  // literals), this avoids a checkpoint for every operator lookahead.
  template <typename Context>
    requires StrictParseModeContext<Context> || RecoveryParseModeContext<Context>
  bool fast_probe_impl(Context &ctx) const {
    return parser::attempt_fast_probe(ctx, _element);
  }

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    if constexpr (IsOrderedChoice<Element>::value) {
      return detail::parse_wrapped_node(ctx, this, _element);
    }
    return detail::parse_overriding_first_child(ctx, this, _element);
  }

  ExpressionHolder<Element> _element;
  std::int32_t _precedence = -1;
};

template <NonNullableExpression Element>
constexpr auto LeftAssociation(Element &&element) {
  return InfixOperator<Element, grammar::InfixOperator::Associativity::Left>{
      std::forward<Element>(element)};
}
template <NonNullableExpression Element>
constexpr auto RightAssociation(Element &&element) {
  return InfixOperator<Element, grammar::InfixOperator::Associativity::Right>{
      std::forward<Element>(element)};
}

// -----------------------------------------------------------------------------
// Concept: InfixOperatorExpression
// -----------------------------------------------------------------------------
template <class E>
concept InfixOperatorExpression =
    std::derived_from<std::remove_cvref_t<E>, grammar::InfixOperator>;

// -----------------------------------------------------------------------------
// InfixRule wrapper
// -----------------------------------------------------------------------------
template <typename T, auto Left, auto Op, auto Right>
  requires DefaultConstructibleAstNode<T>
struct InfixRule final : grammar::InfixRule {
  struct Ops {
    AstNode *(*getValue)(const void *, const CstNodeView &, AstNode *,
                         const ValueBuildContext &);
    bool (*rule)(const void *, ParseContext &);
    bool (*ruleTracked)(const void *, TrackedParseContext &);
    bool (*fastProbeTracked)(const void *, TrackedParseContext &);
    bool (*recover)(const void *, RecoveryContext &);
    bool (*expect)(const void *, ExpectContext &);
    bool (*probeRecoverable)(const void *, RecoveryContext &);
    void (*init)(const void *, AstReflectionInitContext &);
    const grammar::AbstractElement *(*elem)(const void *) noexcept;
    const grammar::InfixOperator *(*getOperator)(const void *,
                                                 std::size_t) noexcept;
    std::size_t (*operatorCount)(const void *) noexcept;
    void (*destroy)(void *) noexcept;
    void *(*clone)(const void *);
    // Re-point a Model's captured owner after the enclosing InfixRule is copied
    // or moved: the Model stores `_owner` (the grammar::InfixRule passed to
    // ctx.exit), which must follow the new InfixRule instance, not stay pinned
    // to the copied-from / moved-from one (which may be destroyed).
    void (*rebindOwner)(void *, const grammar::InfixRule *) noexcept;
  };

  static constexpr bool nullable = false;
  static constexpr bool isFailureSafe = false;
  using type = T;


  // ------------------- ctor -------------------
  template <NonNullableExpression Element, InfixOperatorExpression... Operator>
    requires(IsParserRule<Element>)
  constexpr InfixRule(std::string_view name, Element &&element,
                      Operator &&...oper)
      : _name{name} {
    using W = Model<Element, Operator...>;

    reset();
    _ops = W::opsTable;

    _obj = new W(this, std::forward<Element>(element),
                 std::forward<Operator>(oper)...);
  }

  ~InfixRule() override { reset(); }

  // ------------------- API grammar::InfixRule -------------------
  std::string_view getTypeName() const noexcept override {
    static constexpr auto typeName = detail::type_name_v<T>;
    return typeName;
  }

  const grammar::InfixOperator *
  getOperator(std::size_t index) const noexcept override {
    assert(_obj && _ops.getOperator && "Missing element wrapper!");
    return _ops.getOperator(_obj, index);
  }

  std::size_t operatorCount() const noexcept override {
    assert(_obj && _ops.operatorCount && "Missing element wrapper!");
    return _ops.operatorCount(_obj);
  }
  constexpr bool isNullable() const noexcept override {
    return nullable;
  }

  const grammar::AbstractElement *getElement() const noexcept override {
    assert(_obj && _ops.elem && "Missing element wrapper!");
    return _ops.elem(_obj);
  }
  std::string_view getName() const noexcept override { return _name; }
  InfixRule(const InfixRule &other)
      : _name(other._name), _ops(other._ops) {
    if (other._obj) {
      _obj = _ops.clone(other._obj);
      _ops.rebindOwner(_obj, this);
    }
  }

  InfixRule(InfixRule &&other) noexcept { move_from(std::move(other)); }

  InfixRule &operator=(const InfixRule &other) {
    if (this == &other)
      return *this;

    reset();

    _name = other._name;
    _ops = other._ops;

    if (other._obj) {
      _obj = _ops.clone(other._obj);
      _ops.rebindOwner(_obj, this);
    }
    return *this;
  }

  InfixRule &operator=(InfixRule &&other) noexcept {
    if (this == &other)
      return *this;
    reset();
    move_from(std::move(other));
    return *this;
  }

  AstNode *getValue(const CstNodeView &node, AstNode *lhsNode,
                    const ValueBuildContext &context) const override {
    assert(_obj && _ops.getValue && "Missing element wrapper!");
    return _ops.getValue(_obj, node, lhsNode, context);
  }

  /// Recovery-mode; defined out-of-class in InfixRuleRecovery.hpp.
  bool probeRecoverable(RecoveryContext &ctx) const;

private:
  friend struct detail::ParseAccess;
  friend struct detail::FastProbeAccess;
  friend struct detail::InitAccess;

  template <ParseModeContext Context> bool parse_impl(Context &ctx) const {
    if constexpr (std::same_as<std::remove_cvref_t<Context>, ParseContext>) {
      assert(_obj && _ops.rule && "Missing element wrapper!");
      return _ops.rule(_obj, ctx);
    } else if constexpr (std::same_as<std::remove_cvref_t<Context>,
                                      TrackedParseContext>) {
      assert(_obj && _ops.ruleTracked && "Missing tracked infix wrapper!");
      return _ops.ruleTracked(_obj, ctx);
    } else if constexpr (RecoveryParseModeContext<Context>) {
      // `.recover` is null whenever recovery is compiled out; reachable
      // both via an outer `Wrapper` (already gated) and via a direct
      // `parser::parse(infixRule, recoveryCtx)` on a raw `InfixRule`
      // reference, so this dispatch has to know about the compiled-out
      // case itself. Gating on the compile-time flag keeps a
      // recovery-capable build's codegen unchanged.
      if constexpr (detail::kRecoveryCompiledIn) {
        assert(_obj && _ops.recover && "Missing element wrapper!");
        return _ops.recover(_obj, ctx);
      } else {
        return false;
      }
    } else {
      assert(_obj && _ops.expect && "Missing element wrapper!");
      return _ops.expect(_obj, ctx);
    }
  }

  bool fast_probe_impl(TrackedParseContext &ctx) const {
    assert(_obj && _ops.fastProbeTracked && "Missing tracked infix fast-probe wrapper!");
    return _ops.fastProbeTracked(_obj, ctx);
  }

  void init_impl(AstReflectionInitContext &ctx) const {
    ctx.registerProducedType(detail::ast_node_type_info<T>());
    assert(_obj && _ops.init && "Missing infix init wrapper!");
    _ops.init(_obj, ctx);
  }

  void reset() noexcept {
    if (!_obj) {
      return;
    }
    _ops.destroy(_obj);
    _obj = nullptr;
    _ops = {};
  }

  void move_from(InfixRule &&other) noexcept {
    _name = std::exchange(other._name, {});
    _obj = std::exchange(other._obj, nullptr);
    _ops = std::exchange(other._ops, {});
    if (_obj) {
      _ops.rebindOwner(_obj, this);
    }
  };

  template <NonNullableExpression Element, InfixOperatorExpression... Operators>
  struct Model {
    using Assoc = grammar::InfixOperator::Associativity;
    using Traits =
        detail::InfixRuleModelTraits<T, Left, Op, Right, Element, Operators...>;
    static constexpr std::int32_t kMaxPrecedence = Traits::kMaxPrecedence;
    using OpType = typename Traits::OpType;
    using LeftPointeeType = typename Traits::LeftPointeeType;
    using RightPointeeType = typename Traits::RightPointeeType;

  private:
    struct ParseEngine;
    using ValueBuilder = detail::InfixRuleValueBuilder<
        Model, T, Left, Op, Right, OpType, LeftPointeeType, RightPointeeType,
        Operators...>;
    using OperatorCatalog = detail::InfixRuleOperatorCatalog<Model, Operators...>;
    using ExpectSupport = detail::InfixRuleExpectSupport<Model, Operators...>;

  public:
    explicit Model(const grammar::InfixRule *owner, Element &&elem,
                   Operators &&...opsIn)
        : _owner(owner), primary(detail::move_if_owned<Element>(elem)),
          ops(OperatorCatalog::make_ops(detail::move_if_owned<Operators>(opsIn)...)) {}

    static AstNode *getValue(const void *self, const CstNodeView &node,
                             AstNode *lhsNode,
                             const ValueBuildContext &context) {
      return ValueBuilder::getValue(static_cast<const Model *>(self), node,
                                    lhsNode, context);
    }

    static bool rule(const void *self, ParseContext &ctx) {
      return ParseEngine::rule(static_cast<const Model *>(self), ctx);
    }
    static bool ruleTracked(const void *self, TrackedParseContext &ctx) {
      return ParseEngine::rule(static_cast<const Model *>(self), ctx);
    }
    static bool fastProbeTracked(const void *self, TrackedParseContext &ctx) {
      const auto *model = static_cast<const Model *>(self);
      return parser::attempt_fast_probe(ctx, model->primary);
    }
    /// Recovery-mode; defined out-of-class in InfixRuleRecovery.hpp.
    static bool recover(const void *self, RecoveryContext &ctx);
    /// Expect-mode; defined out-of-class in InfixRuleExpect.hpp.
    static bool expect(const void *self, ExpectContext &ctx);
    /// Recovery-mode; defined out-of-class in InfixRuleRecovery.hpp.
    static bool probeRecoverable(const void *self, RecoveryContext &ctx);
    static void init(const void *self, AstReflectionInitContext &ctx) {
      const auto *model = static_cast<const Model *>(self);
      parser::init(model->primary, ctx);
      std::apply(
          [&ctx](const auto &...operatorExpression) {
            (parser::init(operatorExpression, ctx), ...);
          },
          model->ops);
    }

    static const grammar::AbstractElement *elem(const void *self) noexcept {
      return std::addressof(static_cast<const Model *>(self)->primary);
    }

    static const grammar::InfixOperator *
    getOperator(const void *self, std::size_t index) noexcept {
      return OperatorCatalog::getOperator(static_cast<const Model *>(self), index);
    }

    static std::size_t operatorCount(const void *) noexcept {
      return OperatorCatalog::operatorCount();
    }

    static void destroy(void *self) noexcept {
      delete static_cast<Model *>(self);
    }
    static void *clone(const void *self) {
      return new Model(*static_cast<const Model *>(self));
    }
    static void rebindOwner(void *self,
                            const grammar::InfixRule *owner) noexcept {
      static_cast<Model *>(self)->_owner = owner;
    }

    using Ops = InfixRule::Ops;

    static constexpr Ops make_ops() noexcept {
      Ops result{
          .getValue = &Model::getValue,
          .rule = &Model::rule,
          .ruleTracked = &Model::ruleTracked,
          .fastProbeTracked = &Model::fastProbeTracked,
          .expect = &Model::expect,
          .init = &Model::init,
          .elem = &Model::elem,
          .getOperator = &Model::getOperator,
          .operatorCount = &Model::operatorCount,
          .destroy = &Model::destroy,
          .clone = &Model::clone,
          .rebindOwner = &Model::rebindOwner,
      };
      // Unlike the generic `Wrapper` (ParseExpression.hpp), an `InfixRule` builds
      // this table eagerly in its own constructor (`_ops = W::opsTable;`), not
      // lazily through a recursive parse dispatch — so `.recover`/
      // `.probeRecoverable` get instantiated the moment an `InfixRule` is
      // constructed, regardless of whether it is ever wrapped by another rule.
      // Taking their address instantiates their bodies, which drag in the whole
      // recovery-mode tail-parsing engine for the primary element and every
      // operator; skip that when recovery is compiled out.
      if constexpr (detail::kRecoveryCompiledIn) {
        result.recover = &Model::recover;
        result.probeRecoverable = &Model::probeRecoverable;
      }
      return result;
    }

    inline static constexpr Ops opsTable = make_ops();

  private:
    struct ParseEngine {
      template <StrictParseModeContext Context>
      static bool rule(const Model *model, Context &ctx) {
        const auto expressionStart = ctx.cursor();
        if (!parse(model->primary, ctx)) {
          return false;
        }
        ctx.skip();
        return parse_tail_strict(model, ctx, /*minPrecedence=*/0,
                                 expressionStart);
      }

      /// Recovery-mode; defined out-of-class in InfixRuleRecovery.hpp.
      static bool recover(const Model *model, RecoveryContext &ctx);

      /// Type-erased view consumed by the shared infix tail engine
      /// (InfixTailRecoveryEngine.cpp). The trampolines are static (one set
      /// per Model instantiation); the model/owner pointers are per instance,
      /// so the table is built on the stack at each recovery descent.
      /// Recovery-mode; defined out-of-class in InfixRuleRecovery.hpp.
      [[nodiscard]] static detail::InfixTailOps
      make_tail_ops(const Model *model) noexcept;

      /// Expect-mode; defined out-of-class in InfixRuleExpect.hpp.
      static bool expect(const Model *model, ExpectContext &ctx);

    private:
      template <StrictParseModeContext Context>
      static bool parse_tail_strict(const Model *model, Context &ctx,
                                    std::int32_t minPrecedence,
                                    const char *lhsStart) {
        if (minPrecedence > kMaxPrecedence) {
          return true;
        }
        while (true) {
          if (!has_operator_fast_probe(model, ctx, minPrecedence)) {
            return true;
          }
          const auto nodeCheckpoint = ctx.mark();
          (void)ctx.enter();
          std::int32_t nextMinPrecedence = 0;
          if (!try_match_operator_strict(model, ctx, minPrecedence,
                                         nextMinPrecedence)) {
            ctx.rewind(nodeCheckpoint);
            return true;
          }
          ctx.skip();
          const auto rhsStart = ctx.cursor();
          if (!parse(model->primary, ctx)) {
            ctx.rewind(nodeCheckpoint);
            return true;
          }
          ctx.skip();
          (void)parse_tail_strict(model, ctx, nextMinPrecedence, rhsStart);
          assert(model->_owner && "The owner must be set");
          ctx.exit(lhsStart, model->_owner);
        }
      }

      /// Expect-mode; defined out-of-class in InfixRuleExpect.hpp.
      static bool parse_tail_expect(const Model *model, ExpectContext &ctx,
                                    std::int32_t minPrecedence,
                                    const char *lhsStart);

      template <std::size_t I = 0, ParseModeContext Context>
      static bool has_operator_fast_probe(const Model *model, Context &ctx,
                                          std::int32_t minPrecedence) {
        const auto &op = std::get<I>(model->ops);
        if (constexpr auto precedence =
                static_cast<std::int32_t>(sizeof...(Operators) - I);
            precedence >= minPrecedence &&
            parser::attempt_fast_probe(ctx, op)) {
          return true;
        }
        if constexpr (I + 1 == sizeof...(Operators)) {
          return false;
        } else {
          return has_operator_fast_probe<I + 1>(model, ctx, minPrecedence);
        }
      }

      template <std::size_t I = 0, StrictParseModeContext Context>
      static bool try_match_operator_strict(const Model *model, Context &ctx,
                                            std::int32_t minPrecedence,
                                            std::int32_t &nextMinPrecedence) {
        const auto &op = std::get<I>(model->ops);
        constexpr auto precedence =
            static_cast<std::int32_t>(sizeof...(Operators) - I);
        if (precedence < minPrecedence) {
          return false;
        }
        const auto operatorCheckpoint = ctx.mark();
        if (parser::attempt_fast_probe(ctx, op) &&
            parser::attempt_parse_strict(ctx, op)) {
          if (!detail::infix_operator_shadowed<std::remove_cvref_t<decltype(op)>,
                                               Operators...>(ctx.cursor(),
                                                             ctx.end)) {
            nextMinPrecedence =
                detail::infix_next_min_precedence<decltype(op)>(precedence);
            return true;
          }
          ctx.rewind(operatorCheckpoint);
        }
        if constexpr (I + 1 == sizeof...(Operators)) {
          return false;
        } else {
          return try_match_operator_strict<I + 1>(model, ctx, minPrecedence,
                                                  nextMinPrecedence);
        }
      }

      /// Recovery-only despite sitting alongside `try_match_operator_strict`:
      /// its only caller is the `matchOperatorEditable` trampoline built by
      /// `make_tail_ops`, feeding the erased tail-recovery engine in
      /// InfixTailRecoveryEngine.cpp. Defined out-of-class in
      /// InfixRuleRecovery.hpp.
      template <std::size_t I = 0>
      static bool try_match_operator_editable(const Model *model,
                                              RecoveryContext &ctx,
                                              std::int32_t minPrecedence,
                                              std::int32_t &nextMinPrecedence);

    };

  public:
    const grammar::InfixRule *_owner = nullptr;
    ExpressionHolder<Element> primary;
    std::tuple<Operators...> ops;
  };

  // ------------------- data -------------------
  std::string _name{};

  void *_obj = nullptr;
  Ops _ops{};
};

} // namespace pegium::parser
