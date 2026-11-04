#pragma once

#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>

// Which language a document belongs to: one routing table and one answer. A
// registry that routed a URI one way for the parse and another for the services
// would be two languages for one document.

namespace cursiv {

struct CoreServices;

// A refused registration is an error, not an assertion: the guard keeps an
// incomplete language out of the build, and an `assert` would let it back in
// under `-DNDEBUG` -- publishing documents unvalidated and unscoped.
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

  // A later registration claiming a file name or extension an earlier language
  // holds replaces that mapping.
  // Existing documents may still use the previous container, whose lifetime
  // extends to the registry's destruction.
  //
  // Precondition on the whole interface: every registration happens-before
  // any concurrent read. The readers take no lock -- a registry that mutated
  // under a running build would be an orchestration bug however it locked.
  virtual void registerServices(std::unique_ptr<CoreServices> services) = 0;

  // Resolution is URI-first: an explicit `languageId` outranks everything, then
  // the exact file name, then the extension. Core has no notion of an open
  // buffer, so the id is an argument -- the storey that has one passes it.
  //
  // Throws `ServiceRegistryError` when no language can be resolved.
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
