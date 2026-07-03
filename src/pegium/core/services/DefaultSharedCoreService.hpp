#pragma once

#include <pegium/core/services/SharedCoreServices.hpp>

namespace pegium {

/// Convenience base class for default services bound to shared core services.
class DefaultSharedCoreService {
public:
  explicit DefaultSharedCoreService(const SharedCoreServices &shared) noexcept
      : shared(shared) {}
  virtual ~DefaultSharedCoreService() noexcept = default;

protected:
  const SharedCoreServices &shared;
};

} // namespace pegium
