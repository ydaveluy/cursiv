#pragma once

#include <pegium/lsp/services/SharedServices.hpp>

namespace pegium {

/// Convenience base class for default services bound to shared LSP services.
class DefaultSharedLspService {
public:
  explicit DefaultSharedLspService(const SharedServices &shared) noexcept
      : shared(shared) {}
  virtual ~DefaultSharedLspService() noexcept = default;

protected:
  const SharedServices &shared;
};

} // namespace pegium
