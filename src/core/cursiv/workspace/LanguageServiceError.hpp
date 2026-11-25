#pragma once

#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cursiv/utils/Cancellation.hpp"

namespace cursiv::detail {

// Only failures of a document's language service can be isolated by the
// builder.
struct LanguageServiceError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

template <class Call>
decltype(auto) language_call(std::string_view operation, Call&& call) {
  try {
    return call();
  } catch (const OperationCancelled&) {
    throw;
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::exception& error) {
    throw LanguageServiceError(std::string(operation) +
                               " failed: " + error.what());
  } catch (...) {
    throw LanguageServiceError(std::string(operation) +
                               " failed: unknown exception");
  }
}

}  // namespace cursiv::detail
