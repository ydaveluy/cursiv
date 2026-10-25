#pragma once

#include "cursiv/core/services/SharedCoreServices.hpp"

// Base of convenience for a default service bound to the whole workspace. The
// same rule as `DefaultCoreService`: reach a sibling on each call.

namespace cursiv {

class DefaultSharedCoreService {
 public:
  explicit DefaultSharedCoreService(
      const SharedCoreServices& sharedServices) noexcept
      : shared(sharedServices) {}
  virtual ~DefaultSharedCoreService() noexcept = default;

 protected:
  const SharedCoreServices& shared;
};

}  // namespace cursiv
