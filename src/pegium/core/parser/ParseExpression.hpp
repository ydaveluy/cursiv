#pragma once

/// Core parser expression concepts, wrappers, and dispatch helpers.

#include <cassert>
#include <concepts>
#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/parser/ParseMode.hpp>
#include <string>
#include <type_traits>
#include <utility>

namespace pegium::parser {

struct ParseContext;
struct TrackedParseContext;
struct RecoveryContext;
struct ExpectContext;
struct AstReflectionInitContext;

template <typename Expr>
  requires std::derived_from<std::remove_cvref_t<Expr>, grammar::AbstractElement>
void init(const Expr &expression, AstReflectionInitContext &ctx);

namespace detail {

struct InitAccess;

template <typename T> struct IsTerminalAtom : std::false_type {};

template <typename T>
inline constexpr bool IsTerminalAtom_v =
    IsTerminalAtom<std::remove_cvref_t<T>>::value;

} // namespace detail

// An expression is failure-safe when `parse(expr, ctx) == false` guarantees that the
// caller does not need to rewind the CST/cursor state to discard partial work.

template <typename E>
concept Expression =
    std::derived_from<std::remove_cvref_t<E>, grammar::AbstractElement> &&
    requires {
      { std::remove_cvref_t<E>::nullable } -> std::convertible_to<bool>;
      { std::remove_cvref_t<E>::isFailureSafe } -> std::convertible_to<bool>;
    } &&
    requires(const std::remove_cvref_t<E> &expression, ParseContext &ctx,
             RecoveryContext &recoveryCtx,
             ExpectContext &expectCtx) {
      { probe(expression, ctx) } -> std::same_as<bool>;
      { parse(expression, ctx) } -> std::same_as<bool>;
      { parse(expression, recoveryCtx) } -> std::same_as<bool>;
      { parse(expression, expectCtx) } -> std::same_as<bool>;
    };

template <typename E>
concept TerminalCapableExpression =
    Expression<E> &&
    requires(const std::remove_cvref_t<E> &expression, const char *begin) {
      { expression.terminal(begin) } noexcept -> std::same_as<const char *>;
    };

template <typename E>
concept RecoveryProbeCapableExpression =
    Expression<E> &&
    requires(const std::remove_cvref_t<E> &expression, RecoveryContext &ctx) {
      { expression.probeRecoverable(ctx) } -> std::same_as<bool>;
    };

template <typename E>
concept EntryRecoveryProbeCapableExpression =
    Expression<E> &&
    requires(const std::remove_cvref_t<E> &expression, RecoveryContext &ctx) {
      { expression.probeRecoverableAtEntry(ctx) } -> std::same_as<bool>;
    };

template <typename E>
concept EntryRecoveryConsumesVisibleProbeCapableExpression =
    Expression<E> &&
    requires(const std::remove_cvref_t<E> &expression, RecoveryContext &ctx) {
      { expression.probeRecoverableAtEntryConsumesVisible(ctx) } ->
          std::same_as<bool>;
    };

template <Expression E>
using ExpressionHolder = std::conditional_t<std::is_lvalue_reference_v<E>, E,
                                            std::remove_cvref_t<E>>;

template <typename E>
concept NonNullableExpression =
    Expression<E> && !std::remove_cvref_t<E>::nullable;

template <typename E>
concept NonNullableTerminalCapableExpression =
    TerminalCapableExpression<E> && !std::remove_cvref_t<E>::nullable;

template <typename E>
concept TerminalAtom =
    TerminalCapableExpression<E> && detail::IsTerminalAtom_v<E>;

template <Expression E, StrictParseModeContext Context>
bool attempt_fast_probe(Context &ctx, const E &expression);

template <Expression E, typename Context>
bool probe_match_here(const E &expression, Context &ctx);

struct Wrapper {
  struct Ops {
    using ParseFn = bool (*)(const void *, ParseContext &);
    using TrackedParseFn = bool (*)(const void *, TrackedParseContext &);
    using TrackedFastProbeFn = bool (*)(const void *, TrackedParseContext &);
    using TrackedProbeMatchHereFn =
        bool (*)(const void *, TrackedParseContext &);
    using RecoveryProbeMatchHereFn =
        bool (*)(const void *, RecoveryContext &);
    using RecoveryParseFn = bool (*)(const void *, RecoveryContext &);
    using ExpectParseFn = bool (*)(const void *, ExpectContext &);
    using RecoveryProbeFn = bool (*)(const void *, RecoveryContext &);
    using RecoveryEntryProbeFn = bool (*)(const void *, RecoveryContext &);
    using RecoveryEntryConsumesVisibleProbeFn =
        bool (*)(const void *, RecoveryContext &);
    using TerminalFn = const char *(*)(const void *, const char *) noexcept;
    using ElemFn = const grammar::AbstractElement *(*)(const void *) noexcept;
    using InitFn = void (*)(const void *, AstReflectionInitContext &);
    using DeleteFn = void (*)(void *) noexcept;
    using CloneFn = void *(*)(const void *);

    ParseFn parse = nullptr;
    TrackedParseFn parseTracked = nullptr;
    TrackedFastProbeFn fastProbeTracked = nullptr;
    TrackedProbeMatchHereFn probeMatchHereTracked = nullptr;
    RecoveryProbeMatchHereFn probeMatchHereRecover = nullptr;
    RecoveryParseFn parseRecover = nullptr;
    ExpectParseFn parseExpect = nullptr;
    RecoveryProbeFn probeRecoverable = nullptr;
    RecoveryEntryProbeFn probeRecoverableAtEntry = nullptr;
    RecoveryEntryConsumesVisibleProbeFn probeRecoverableAtEntryConsumesVisible =
        nullptr;
    TerminalFn terminal = nullptr;
    ElemFn elem = nullptr;
    InitFn init = nullptr;
    DeleteFn destroy = nullptr;
    CloneFn clone = nullptr;
  };

  Wrapper() = default;

  Wrapper(const Wrapper &other) noexcept : _ops(other._ops) {
    if (other._obj) {
      _obj = _ops->clone(other._obj);
    }
  }

  Wrapper(Wrapper &&other) noexcept { move_from(std::move(other)); }

  Wrapper &operator=(const Wrapper &other) {
    if (this == &other)
      return *this;

    // Clone into a local first so a throwing clone leaves *this unchanged;
    // publish _ops/_obj only after the allocation succeeds.
    auto *cloned = other._obj ? other._ops->clone(other._obj) : nullptr;
    reset();
    _ops = other._ops;
    _obj = cloned;
    return *this;
  }

  Wrapper &operator=(Wrapper &&other) noexcept {
    if (this == &other)
      return *this;
    reset();
    move_from(std::move(other));
    return *this;
  }

  ~Wrapper() { reset(); }

  template <Expression Element> void set(Element &&element) {
    using W = Model<Element>;

    // Construct before publishing (like operator=): a throwing constructor then
    // leaves *this unchanged instead of in a half-set (_ops set, _obj null)
    // state that reset() would not clean up.
    auto *obj = new W(std::forward<Element>(element));
    reset();
    _ops = &W::ops;
    _obj = obj;
  }

  bool has_terminal() const noexcept {
    return _obj != nullptr && _ops->terminal != nullptr;
  }

  bool has_recovery_probe() const noexcept {
    return _obj != nullptr && _ops->probeRecoverable != nullptr;
  }

  bool has_entry_recovery_probe() const noexcept {
    return _obj != nullptr && _ops->probeRecoverableAtEntry != nullptr;
  }

  bool has_entry_recovery_consumes_visible_probe() const noexcept {
    return _obj != nullptr &&
           _ops->probeRecoverableAtEntryConsumesVisible != nullptr;
  }

  const char *try_terminal(const char *begin) const noexcept {
    return has_terminal() ? _ops->terminal(_obj, begin) : nullptr;
  }

  bool probe_recoverable(RecoveryContext &ctx) const {
    return has_recovery_probe() ? _ops->probeRecoverable(_obj, ctx) : false;
  }

  bool probe_recoverable_at_entry(RecoveryContext &ctx) const {
    return has_entry_recovery_probe()
               ? _ops->probeRecoverableAtEntry(_obj, ctx)
               : false;
  }

  bool probe_recoverable_at_entry_consumes_visible(RecoveryContext &ctx) const {
    return has_entry_recovery_consumes_visible_probe()
               ? _ops->probeRecoverableAtEntryConsumesVisible(_obj, ctx)
               : false;
  }

  bool fast_probe(TrackedParseContext &ctx) const {
    return _ops->fastProbeTracked(_obj, ctx);
  }

  bool probe_match_here(TrackedParseContext &ctx) const {
    return _ops->probeMatchHereTracked(_obj, ctx);
  }

  bool probe_match_here(RecoveryContext &ctx) const {
    // `probeMatchHereRecover` is null whenever recovery is compiled out
    // (PEGIUM_DISABLE_RECOVERY), and this accessor is reachable directly —
    // via a rule's `probeMatchHere` (AbstractRule.hpp) — bypassing the
    // higher-level null checks such as `has_recovery_probe()`. Branching on
    // the compile-time flag rather than the (always non-null when it is
    // true) function pointer keeps this identical, instruction for
    // instruction, to a plain unconditional call in a recovery-capable
    // build.
    if constexpr (detail::kRecoveryCompiledIn) {
      return _ops->probeMatchHereRecover(_obj, ctx);
    } else {
      return false;
    }
  }

  const grammar::AbstractElement *element() const noexcept {
    assert(_obj && _ops && "Missing element wrapper!");
    return _ops->elem(_obj);
  }

  void init(AstReflectionInitContext &ctx) const {
    assert(_obj && _ops && "Missing init wrapper!");
    _ops->init(_obj, ctx);
  }

  void reset() noexcept {
    if (!_obj) {
      return;
    }
    _ops->destroy(_obj);
    _obj = nullptr;
    _ops = nullptr;
  }

private:
  friend struct detail::ParseAccess;

  void move_from(Wrapper &&other) noexcept {
    _obj = std::exchange(other._obj, nullptr);
    _ops = std::exchange(other._ops, nullptr);
  }

  template <ParseModeContext Context>
  bool parse_impl(Context &ctx) const {
    assert(_obj && _ops && "Missing element wrapper!");
    if constexpr (std::same_as<std::remove_cvref_t<Context>, ParseContext>) {
      return _ops->parse(_obj, ctx);
    } else if constexpr (std::same_as<std::remove_cvref_t<Context>,
                                      TrackedParseContext>) {
      return _ops->parseTracked(_obj, ctx);
    } else if constexpr (RecoveryParseModeContext<Context>) {
      // `parseRecover` is null whenever recovery is compiled out; this is
      // reached both through `ParserRule::recover`/`DataTypeRule`'s
      // recovery `parse_impl` and through any direct
      // `parser::parse(rule, recoveryCtx)` call that bypasses those
      // overrides entirely (e.g. a test constructing a `RecoveryContext` by
      // hand), so this dispatch has to know about the compiled-out case
      // itself, not rely only on the rule-level call sites. Gating on the
      // compile-time flag (rather than a runtime null check on the always
      // non-null pointer) keeps a recovery-capable build's codegen
      // unchanged.
      if constexpr (detail::kRecoveryCompiledIn) {
        return _ops->parseRecover(_obj, ctx);
      } else {
        return false;
      }
    } else {
      return _ops->parseExpect(_obj, ctx);
    }
  }

  template <class T> struct Model {

    explicit Model(T &&v)
      requires(!std::is_lvalue_reference_v<T>)
        : value(std::move(v)) {}

    explicit Model(T v)
      requires(std::is_lvalue_reference_v<T>)
        : value(v) {}

    static bool parse(const void *self, ParseContext &ctx) {
      return parser::parse(static_cast<const Model *>(self)->value, ctx);
    }
    static bool parseTracked(const void *self, TrackedParseContext &ctx) {
      return parser::parse(static_cast<const Model *>(self)->value, ctx);
    }
    static bool fastProbeTracked(const void *self, TrackedParseContext &ctx) {
      return parser::attempt_fast_probe(ctx,
                                        static_cast<const Model *>(self)->value);
    }
    static bool probeMatchHereTracked(const void *self,
                                      TrackedParseContext &ctx) {
      return parser::probe_match_here(static_cast<const Model *>(self)->value,
                                      ctx);
    }
    static bool probeMatchHereRecover(const void *self, RecoveryContext &ctx) {
      return parser::probe_match_here(static_cast<const Model *>(self)->value,
                                      ctx);
    }
    static bool parseRecover(const void *self, RecoveryContext &ctx) {
      return parser::parse(static_cast<const Model *>(self)->value, ctx);
    }
    static bool parseExpect(const void *self, ExpectContext &ctx) {
      return parser::parse(static_cast<const Model *>(self)->value, ctx);
    }
    static bool probeRecoverable(const void *self, RecoveryContext &ctx)
      requires RecoveryProbeCapableExpression<T>
    {
      return static_cast<const Model *>(self)->value.probeRecoverable(ctx);
    }
    static bool probeRecoverableAtEntry(const void *self, RecoveryContext &ctx)
      requires EntryRecoveryProbeCapableExpression<T>
    {
      return static_cast<const Model *>(self)->value.probeRecoverableAtEntry(ctx);
    }
    static bool probeRecoverableAtEntryConsumesVisible(const void *self,
                                                       RecoveryContext &ctx)
      requires EntryRecoveryConsumesVisibleProbeCapableExpression<T>
    {
      return static_cast<const Model *>(self)
          ->value.probeRecoverableAtEntryConsumesVisible(ctx);
    }
    static const char *terminal(const void *self, const char *b) noexcept
      requires TerminalCapableExpression<T>
    {
      return static_cast<const Model *>(self)->value.terminal(b);
    }
    static const grammar::AbstractElement *elem(const void *self) noexcept {
      return std::addressof(static_cast<const Model *>(self)->value);
    }
    static void init(const void *self, AstReflectionInitContext &ctx) {
      parser::init(static_cast<const Model *>(self)->value, ctx);
    }
    static void destroy(void *self) noexcept {
      delete static_cast<Model *>(self);
    }
    static void *clone(const void *self) {
      return new Model(*static_cast<const Model *>(self));
    }

    static constexpr Ops make_ops() noexcept {
      Ops result{
          .parse = &Model::parse,
          .parseTracked = &Model::parseTracked,
          .fastProbeTracked = &Model::fastProbeTracked,
          .probeMatchHereTracked = &Model::probeMatchHereTracked,
          .parseExpect = &Model::parseExpect,
          .elem = &Model::elem,
          .init = &Model::init,
          .destroy = &Model::destroy,
          .clone = &Model::clone,
      };
      // Taking the address of `parseRecover`/`probeMatchHereRecover` (and, below,
      // the three entry-probe members) forces the compiler to instantiate their
      // bodies, which parse/probe `value` through a `RecoveryContext` — the
      // choke point that drags in the recovery-mode `parse_impl` chain of the
      // whole nested combinator tree. Skipping the assignment when recovery is
      // compiled out leaves the slots null, which every caller already
      // null-checks (`Wrapper::has_recovery_probe()` and friends) or never
      // reaches (`ParserRule::recover` short-circuits before touching them).
      if constexpr (detail::kRecoveryCompiledIn) {
        result.probeMatchHereRecover = &Model::probeMatchHereRecover;
        result.parseRecover = &Model::parseRecover;
        if constexpr (RecoveryProbeCapableExpression<T>) {
          result.probeRecoverable = &Model::probeRecoverable;
        }
        if constexpr (EntryRecoveryProbeCapableExpression<T>) {
          result.probeRecoverableAtEntry = &Model::probeRecoverableAtEntry;
        }
        if constexpr (EntryRecoveryConsumesVisibleProbeCapableExpression<T>) {
          result.probeRecoverableAtEntryConsumesVisible =
              &Model::probeRecoverableAtEntryConsumesVisible;
        }
      }
      if constexpr (TerminalCapableExpression<T>) {
        result.terminal = &Model::terminal;
      }
      return result;
    }

    inline static constexpr Ops ops = make_ops();

  private:
    ExpressionHolder<T> value;
  };

  void *_obj = nullptr;
  // Points to a `Model<T>::ops` static instance (one per Element type).
  // Sharing the table by pointer keeps each Wrapper to two pointers
  // instead of copying the full ~104-byte function-pointer table.
  const Ops *_ops = nullptr;
};
} // namespace pegium::parser
