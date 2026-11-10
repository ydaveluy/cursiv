#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>

namespace cursiv::engine {

// Begins an array's lifetime without initializing its elements, in aligned,
// large enough storage. `T` must be an implicit-lifetime type.
template <class T>
[[nodiscard]] inline T* start_implicit_array(void* storage,
                                             std::size_t count) noexcept {
  static_assert(std::is_aggregate_v<T> && std::is_trivially_copyable_v<T>);
#if defined(__cpp_lib_start_lifetime_as)
  return std::start_lifetime_as_array<T>(storage, count);
#else
  // C++20 memmove implicitly creates objects in the destination and returns
  // a pointer to a suitable created object. Self-copy preserves the bytes and
  // is eliminated by the compiler; unlike memcpy it permits overlap.
  if (count == 0) return static_cast<T*>(storage);
  return static_cast<T*>(std::memmove(storage, storage, sizeof(T) * count));
#endif
}

}  // namespace cursiv::engine
