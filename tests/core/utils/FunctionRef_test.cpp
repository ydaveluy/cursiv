#include <type_traits>

#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/test/Check.hpp"

namespace {
int next(int n) { return n + 1; }
int previous(int n) noexcept { return n - 1; }
auto from_temporary_pointer() { return cursiv::function_ref<int(int)>(&next); }
struct Value {
  int offset = 4;
  int operator()(int n) const { return n + offset; }
};
}  // namespace

int main() {
  cursiv::function_ref<int(int)> direct(next);
  CHECK(direct(7) == 8);
  auto held = from_temporary_pointer();
  CHECK(held(2) == 3);
  auto pointer = &next;
  cursiv::function_ref<int(int)> copied(pointer);
  pointer = &previous;
  CHECK(copied(2) == 3);
  CHECK(cursiv::function_ref<int(int)>(&previous)(2) == 1);
  Value value;
  cursiv::function_ref<int(int)> borrowed(value);
  CHECK(borrowed(1) == 5);
  value.offset = 8;
  CHECK(borrowed(1) == 9);
  const Value constant{3};
  cursiv::function_ref<int(int)> constBorrowed(constant);
  CHECK(constBorrowed(2) == 5);
  auto mutableCallback = [count = 0]() mutable { return ++count; };
  cursiv::function_ref<int()> mutableBorrowed(mutableCallback);
  CHECK(mutableBorrowed() == 1);
  CHECK(mutableBorrowed() == 2);
  CHECK(mutableCallback() == 3);
  static_assert(!std::is_constructible_v<cursiv::function_ref<int()>,
                                         const decltype(mutableCallback)&>);
  int calls = 0;
  const auto callback = [&] { return ++calls; };
  cursiv::function_ref<void()> discard(callback);
  discard();
  CHECK(calls == 1);
  cursiv::function_ref<void(int)> discarded_function(next);
  discarded_function(3);
  static_assert(std::is_trivially_copyable_v<decltype(borrowed)>);
  return cursiv::test::report("FunctionRef_test");
}
