#pragma once

#include <exception>
#include <format>
#include <new>
#include <stdexcept>
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
    throw LanguageServiceError(
        std::format("{} failed: {}", operation, error.what()));
  } catch (...) {
    throw LanguageServiceError(
        std::format("{} failed: unknown exception", operation));
  }
}

}  // namespace cursiv::detail
