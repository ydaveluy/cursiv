#include "cursiv/core/services/DefaultServiceRegistry.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/observability/ObservabilitySink.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/LanguageMetaData.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/utils/UriUtils.hpp"

namespace cursiv {

void DefaultServiceRegistry::registerServices(
    std::unique_ptr<CoreServices> services) {
  if (services == nullptr)
    throw ServiceRegistrationError("cursiv: no services to register");

  // The gate of the slot law: a container that owes a required slot is
  // refused here, the shared container's included.
  if (const std::string_view missing = shared.firstMissingService();
      !missing.empty())
    throw ServiceRegistrationError(
        std::format("cursiv: the shared services are missing '{}'", missing));

  const LanguageMetaData& meta = services->languageMetaData;
  if (meta.languageId.empty())
    throw ServiceRegistrationError(
        "cursiv: a language is registered by its id, which is empty");
  if (const std::string_view missing = services->firstMissingService();
      !missing.empty())
    throw ServiceRegistrationError(
        std::format("cursiv: language '{}' is missing service '{}'",
                    meta.languageId, missing));

  // Owned before any table names it: a throw below -- a table growing, a
  // collision's message -- leaves rows on a live container, never a freed one.
  const CoreServices* const language = services.get();
  _owned.push_back(std::move(services));

  // A re-registration retires the previous container before claiming: its
  // rows would otherwise read as a collision of one language with itself. The
  // language keeps its rank in `_all` -- registration order is the order a
  // consumer walks, and re-registering must not reshuffle it.
  const auto known = _byLanguageId.find(meta.languageId);
  const bool reRegistered = known != _byLanguageId.end();
  if (reRegistered) {
    const CoreServices* const old = known->second;
    std::erase_if(_byExtension,
                  [old](const auto& row) { return row.second == old; });
    std::erase_if(_byFileName,
                  [old](const auto& row) { return row.second == old; });
    std::ranges::replace(_all, old, language);
    // Existing documents retain this container through their language pointer.
    // Keep its ownership until registry destruction; only routing is replaced.
  }
  _byLanguageId[meta.languageId] = language;
  // The later registration owns a colliding key -- said, never silent: the
  // first language's documents would quietly resolve to the second's. The
  // message names the normalised key, which is what routes.
  const auto claim = [&](auto& table, std::string key, const char* kind) {
    const CoreServices*& slot = table[key];
    if (slot != nullptr && slot != language)
      shared.observabilitySink->publish(
          {.severity = ObservationSeverity::Warning,
           .code = ObservationCode::LanguageMappingCollision,
           .message = std::format("two languages claim {} '{}'", kind, key),
           .uri = {},
           .languageId = meta.languageId});
    slot = language;
  };
  // An empty key would route every file without an extension, or every URI
  // ending in a separator: not a claim.
  for (const std::string& extension : meta.fileExtensions)
    if (std::string key = normalize_extension(extension); !key.empty())
      claim(_byExtension, std::move(key), "extension");
  for (const std::string& fileName : meta.fileNames)
    if (!fileName.empty()) claim(_byFileName, fileName, "file name");

  if (!reRegistered) _all.push_back(language);
}

const CoreServices* DefaultServiceRegistry::lookup(
    const ByName& table, std::string_view key) noexcept {
  const auto it = table.find(key);
  return it == table.end() ? nullptr : it->second;
}

const CoreServices* DefaultServiceRegistry::findServices(
    std::string_view uri, std::string_view languageId) const {
  if (!languageId.empty())
    if (const CoreServices* const claimed = lookup(_byLanguageId, languageId))
      return claimed;
  if (const CoreServices* const named = lookup(_byFileName, uri_file_name(uri)))
    return named;
  return lookup(_byExtension, uri_extension(uri));
}

const CoreServices& DefaultServiceRegistry::getServices(
    std::string_view uri, std::string_view languageId) const {
  // An empty registry is its own failure: nothing was ever installed, which
  // no URI-shaped message would say.
  if (_byLanguageId.empty())
    throw ServiceRegistryError(
        "cursiv: the service registry is empty -- no language was "
        "registered");
  if (const CoreServices* const services = findServices(uri, languageId))
    return *services;
  throw ServiceRegistryError(
      std::format("cursiv: no language claims '{}'", uri));
}

}  // namespace cursiv
