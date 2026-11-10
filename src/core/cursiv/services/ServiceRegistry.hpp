#pragma once

#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>

// Which language a document belongs to: one routing table, so the parse and the
// services always agree.

namespace cursiv {

struct CoreServices;

// A refused registration throws rather than asserts, so the check holds under
// `-DNDEBUG` too.
class ServiceRegistrationError : public std::invalid_argument {
 public:
  using std::invalid_argument::invalid_argument;
};

// A resolution that found no language: the workspace's fault, not a
// registration's, so it has its own name.
class ServiceRegistryError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class ServiceRegistry {
 public:
  virtual ~ServiceRegistry() noexcept = default;

  // A later language claiming a file name or extension takes the mapping; old
  // documents keep the previous container, owned until the registry dies. Every
  // registration must happen before any concurrent read: readers take no lock.
  virtual void registerServices(std::unique_ptr<CoreServices> services) = 0;

  // Resolved by explicit `languageId` first, then exact file name, then
  // extension. Throws `ServiceRegistryError` when no language matches.
  [[nodiscard]] virtual const CoreServices& getServices(
      std::string_view uri, std::string_view languageId = {}) const = 0;

  // The same resolution, null when no registered language matches.
  [[nodiscard]] virtual const CoreServices* findServices(
      std::string_view uri, std::string_view languageId = {}) const = 0;

  [[nodiscard]] virtual bool hasServices(std::string_view uri) const = 0;

  // Every registered language, in registration order. All elements non-null.
  [[nodiscard]] virtual std::span<const CoreServices* const> all()
      const noexcept = 0;
};

}  // namespace cursiv
