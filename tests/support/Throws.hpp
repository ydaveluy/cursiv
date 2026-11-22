#pragma once

#include "cursiv/utils/FunctionRef.hpp"

namespace support {

template <class Error>
bool throws(cursiv::FunctionRef<void()> action) {
  try {
    action();
  } catch (const Error&) {
    return true;
  }
  return false;
}

}  // namespace support
