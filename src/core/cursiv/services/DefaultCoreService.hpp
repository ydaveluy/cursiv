#pragma once

#include "cursiv/services/CoreServices.hpp"

// Base for a default service bound to one language. Reach sibling services
// through `services` at each call, never cached: a language module may
// replace them. `shared` is fixed for the container's life.

namespace cursiv {

class DefaultCoreService {
 public:
  explicit DefaultCoreService(const CoreServices& coreServices) noexcept
      : services(coreServices), shared(coreServices.shared()) {}
  virtual ~DefaultCoreService() noexcept = default;

 protected:
  const CoreServices& services;
  SharedCoreServices& shared;
};

}  // namespace cursiv
