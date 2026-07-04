#pragma once

#include <pegium/core/services/CoreServices.hpp>

namespace pegium {

/// Convenience base class for default services bound to one `CoreServices`.
///
/// Reach sibling services through `services` on *each call* — never cache one at
/// construction: `installDefaultCoreServices(...)` fills empty slots that a
/// language module then replaces. Caching `services.shared` is fine (it is fixed
/// for the container's lifetime).
class DefaultCoreService {
public:
  explicit DefaultCoreService(const CoreServices &services) noexcept
      : services(services) {}
  virtual ~DefaultCoreService() noexcept = default;

protected:
  const CoreServices &services;
};

} // namespace pegium
