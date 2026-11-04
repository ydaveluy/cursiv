#pragma once

#include "cursiv/utils/FunctionRef.hpp"

namespace support {

template <class Error>
bool throws(cursiv::function_ref<void()> action) {
  try {
    action();
  } catch (const Error&) {
    return true;
  }
  return false;
}

}  // namespace support
