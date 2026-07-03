#pragma once

#include <pegium/lsp/services/Services.hpp>

namespace pegium {

/// Convenience base class for default LSP feature services bound to one language.
class DefaultLanguageService {
public:
  explicit DefaultLanguageService(const Services &languageServices) noexcept
      : services(languageServices) {}
  virtual ~DefaultLanguageService() noexcept = default;

protected:
  const Services &services;
};

} // namespace pegium
