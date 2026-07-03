#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace pegium::utils {

template <typename> class function_ref;

/// Non-owning view of a callable, aligned with `std::function_ref` (C++26).
///
/// Like the standard type, it binds to any callable — lvalue **or** rvalue —
/// that the call site keeps alive for the duration of the call (the usual
/// `foo(function_ref<...> cb, ...)` parameter pattern), the converting
/// constructor is implicit, and assignment *from a callable* is deleted so a
/// `function_ref` cannot be silently rebound to a soon-dangling temporary
/// (copying from another `function_ref` stays allowed).
///
/// Two deliberate additions over C++26 keep existing callers working: a null
/// state via default / `nullptr` construction, and `explicit operator bool` to
/// test it — used by the optional-sink callers (e.g. `ValidationAcceptor`).
/// The C++26 `nontype_t` constructors and the `const` / `noexcept`-qualified
/// signature forms are not modelled (unused in this codebase).
template <typename R, typename... Args> class function_ref<R(Args...)> {
  /// Const-correct erased storage: a non-const callable is held in `object`, a
  /// const callable in `constObject`, so the thunk recovers the pointer with the
  /// matching cv-qualification rather than casting the const away.
  union Storage {
    void *object;
    const void *constObject;
  };

public:
  function_ref() noexcept = default;
  // NOLINTNEXTLINE(google-explicit-constructor) — implicit by design (C++26).
  function_ref(std::nullptr_t) noexcept {}

  /// Binds to any callable invocable as `R(Args...)`, by reference. The
  /// callable must outlive the call. Implicit, like `std::function_ref`, so a
  /// callable passes straight into a `function_ref` parameter.
  template <typename F>
    requires(!std::same_as<std::remove_cvref_t<F>, function_ref> &&
             std::is_invocable_r_v<R, F &, Args...>)
  // NOLINTNEXTLINE(google-explicit-constructor) — implicit by design (C++26).
  function_ref(F &&callable) noexcept
      : _thunk([](Storage storage, Args &&...args) -> R {
          using Object = std::remove_reference_t<F>;
          if constexpr (std::is_const_v<Object>) {
            return std::invoke(*static_cast<Object *>(storage.constObject),
                               std::forward<Args>(args)...);
          } else {
            return std::invoke(*static_cast<Object *>(storage.object),
                               std::forward<Args>(args)...);
          }
        }) {
    if constexpr (std::is_const_v<std::remove_reference_t<F>>) {
      _storage.constObject = std::addressof(callable);
    } else {
      _storage.object = std::addressof(callable);
    }
  }

  function_ref(const function_ref &) noexcept = default;
  function_ref &operator=(const function_ref &) noexcept = default;

  /// Deleted to mirror C++26: rebinding from a callable is the classic way to
  /// end up referring to a destroyed temporary. Copy-assignment from another
  /// `function_ref` (above) is unaffected.
  template <typename T> function_ref &operator=(T) = delete;

  [[nodiscard]] explicit operator bool() const noexcept {
    return _thunk != nullptr;
  }

  R operator()(Args... args) const {
    assert(_thunk != nullptr);
    return _thunk(_storage, std::forward<Args>(args)...);
  }

private:
  Storage _storage{nullptr};
  R (*_thunk)(Storage, Args &&...) = nullptr;
};

} // namespace pegium::utils
