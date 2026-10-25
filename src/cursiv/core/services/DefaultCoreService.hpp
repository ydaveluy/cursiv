#pragma once

#include "cursiv/core/services/CoreServices.hpp"

// Base of convenience for a default service bound to one language.
//
// Reach a sibling service through `services` on each call, never cached at
// construction: a language module replaces slots a default already filled.
// Caching `services.shared` is fine -- it is fixed for the container's life.

namespace cursiv {

class DefaultCoreService {
 public:
  explicit DefaultCoreService(const CoreServices& coreServices) noexcept
      : services(coreServices) {}
  virtual ~DefaultCoreService() noexcept = default;

 protected:
  const CoreServices& services;
};

}  // namespace cursiv
