#pragma once

#include <functional>
#include <type_traits>
#include <utility>

// A callable passed by reference, without ownership or allocation.

namespace cursiv {

template <class Sig>
class function_ref;

template <class R, class... A>
class function_ref<R(A...)> {
  union Bound {
    const void* constObject;
    void* object;
    void (*function)();

    constexpr Bound(const void* p) : constObject(p) {}
    constexpr Bound(void* p) : object(p) {}
    constexpr Bound(void (*p)()) : function(p) {}

    template <class F>
    F* object_as() const {
      if constexpr (std::is_const_v<F>)
        return static_cast<F*>(constObject);
      else
        return static_cast<F*>(object);
    }
  };

 public:
  // `A` is the stored signature, not a deduced type: `static_cast<A&&>` passes
  // each argument on as declared -- moved when taken by value, unchanged when
  // a reference.
  template <class F>
    requires(std::is_function_v<F> && std::is_invocable_r_v<R, F*, A...>)
  function_ref(F* callable) noexcept
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
    requires(!std::is_same_v<std::remove_cvref_t<F>, function_ref> &&
             !std::is_function_v<std::remove_reference_t<F>> &&
             !(std::is_pointer_v<std::remove_cvref_t<F>> &&
               std::is_function_v<std::remove_pointer_t<std::remove_cvref_t<F>>>) &&
             std::is_invocable_r_v<R, F&, A...>)
  constexpr function_ref(F&& callable) noexcept
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
  // Not owned: the callable outlives the call, which is the whole contract --
  // this type exists so a visitor crosses an ABI boundary without an
  // allocation.
  // Function pointers are values: binding &function must not retain the
  // address of a temporary pointer object. Other callables remain borrowed.
  Bound _bound;
  R (*_call)(Bound, A...);
};

}  // namespace cursiv
