// A recursion as coroutines: `co_await f(x)` keeps the call's frame on the
// heap, and `descend(f(x))` runs the outermost call. GCC may run a `co_await`
// in a branch of `?:`, `&&` or `||` that is not taken: write an `if` instead.
#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>
#include <vector>

namespace cursiv::detail {

template <class T>
struct DescentValue {
  std::optional<T> value;
  void return_value(T v) { value.emplace(std::move(v)); }
  T take() { return std::move(*value); }
};
template <>
struct DescentValue<void> {
  void return_void() {}
  void take() {}
};

template <class T>
class [[nodiscard]] Descent {
 public:
  struct promise_type : DescentValue<T> {
    std::exception_ptr error;
    // The calls under way, innermost last.
    std::vector<std::coroutine_handle<>>* calls = nullptr;

    Descent get_return_object() {
      return Descent(std::coroutine_handle<promise_type>::from_promise(*this));
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void unhandled_exception() { error = std::current_exception(); }
  };

  Descent(Descent&& o) noexcept : _h(std::exchange(o._h, {})) {}
  ~Descent() {
    if (_h) _h.destroy();
  }

  bool await_ready() const noexcept { return false; }
  template <class P>
  void await_suspend(std::coroutine_handle<P> caller) {
    _h.promise().calls = caller.promise().calls;
    _h.promise().calls->push_back(_h);
  }
  T await_resume() {
    if (_h.promise().error) std::rethrow_exception(_h.promise().error);
    return _h.promise().take();
  }

 private:
  explicit Descent(std::coroutine_handle<promise_type> h) : _h(h) {}
  template <class U>
  friend U descend(Descent<U> call);

  std::coroutine_handle<promise_type> _h;
};

template <class T>
T descend(Descent<T> call) {
  std::vector<std::coroutine_handle<>> calls{call._h};
  call._h.promise().calls = &calls;
  while (!calls.empty()) {
    const std::coroutine_handle<> h = calls.back();
    h.resume();
    if (h.done()) calls.pop_back();
  }
  return call.await_resume();
}

}  // namespace cursiv::detail
