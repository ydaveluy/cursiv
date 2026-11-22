#pragma once

#include <functional>
#include <type_traits>
#include <utility>

// A callable passed by reference, without ownership or allocation.

namespace cursiv {

template <class Sig>
class FunctionRef;

template <class R, class... A>
class FunctionRef<R(A...)> {
  union Bound {
    const void* constObject;
    void* object;
    void (*function)();

    constexpr explicit Bound(const void* p) : constObject(p) {}
    constexpr explicit Bound(void* p) : object(p) {}
    constexpr explicit Bound(void (*p)()) : function(p) {}

    template <class F>
    F* object_as() const {
      if constexpr (std::is_const_v<F>)
        return static_cast<F*>(constObject);
      else
        return static_cast<F*>(object);
    }
  };

  // Whether calling a `G` with `A...` gives an `R`. A reference `R` binds only
  // to a reference the call returns, never to a temporary: a proxy is refused.
  template <class G>
  static consteval bool accepts() {
    if constexpr (!std::is_invocable_r_v<R, G, A...>) {
      return false;
    } else if constexpr (!std::is_reference_v<R>) {
      return true;
    } else {
      using Result = std::invoke_result_t<G, A...>;
      return std::is_reference_v<Result> &&
             std::is_convertible_v<std::remove_reference_t<Result>*,
                                   std::remove_reference_t<R>*>;
    }
  }

 public:
  // `A` is the stored signature, not a deduced type: `static_cast<A&&>` passes
  // each argument on as declared -- moved when taken by value, unchanged when
  // a reference.
  template <class F>
    requires(std::is_function_v<F> && accepts<F*>())
  FunctionRef(F* callable) noexcept
      : _bound(reinterpret_cast<void (*)()>(callable)),
        _call(+[](Bound bound, A... args) -> R {
          if constexpr (std::is_void_v<R>)
            std::invoke(reinterpret_cast<F*>(bound.function),
                        static_cast<A&&>(args)...);
          else
            return std::invoke(reinterpret_cast<F*>(bound.function),
                               static_cast<A&&>(args)...);
        }) {}

  template <class F>
    requires(!std::is_same_v<std::remove_cvref_t<F>, FunctionRef> &&
             !std::is_function_v<std::remove_reference_t<F>> &&
             !(std::is_pointer_v<std::remove_cvref_t<F>> &&
               std::is_function_v<std::remove_pointer_t<std::remove_cvref_t<F>>>) &&
             accepts<F&>())
  constexpr FunctionRef(F&& callable) noexcept
      : _bound(std::addressof(callable)),
        _call(+[](Bound bound, A... args) -> R {
          auto& callable =
              *bound.template object_as<std::remove_reference_t<F>>();
          if constexpr (std::is_void_v<R>)
            std::invoke(callable, static_cast<A&&>(args)...);
          else
            return std::invoke(callable, static_cast<A&&>(args)...);
        }) {}

  constexpr R operator()(A... args) const {
    return _call(_bound, static_cast<A&&>(args)...);
  }

 private:
  // Not owned: the callable must outlive the call. A function pointer is kept
  // by value; other callables by reference.
  Bound _bound;
  R (*_call)(Bound, A...);
};

}  // namespace cursiv
