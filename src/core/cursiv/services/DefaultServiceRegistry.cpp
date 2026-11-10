#include "cursiv/services/DefaultServiceRegistry.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/observability/ObservabilitySink.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/LanguageMetaData.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/utils/UriUtils.hpp"

namespace cursiv {

void DefaultServiceRegistry::registerServices(
    std::unique_ptr<CoreServices> services) {
  if (services == nullptr)
    throw ServiceRegistrationError("cursiv: no services to register");

  // A container missing a required slot is refused here, the shared container
  // included.
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
  // Its services were built over another workspace, and would index and
  // resolve there.
  if (&services->shared() != &shared)
    throw ServiceRegistrationError(
        std::format("cursiv: language '{}' was built for another workspace",
                    meta.languageId));

  // Every table is rewritten on the side and swapped in at the end, when the
  // container is taken over: a throw before that leaves the registry as it was.
  const CoreServices* const language = services.get();
  ByName byLanguageId = _byLanguageId;
  ByName byExtension = _byExtension;
  ByName byFileName = _byFileName;

  // A re-registration drops the previous container's rows first, so they are
  // not a collision with itself; it keeps its rank in `_all`.
  const auto known = byLanguageId.find(meta.languageId);
  const CoreServices* const old =
      known != byLanguageId.end() ? known->second : nullptr;
  if (old != nullptr) {
    std::erase_if(byExtension,
                  [old](const auto& row) { return row.second == old; });
    std::erase_if(byFileName,
                  [old](const auto& row) { return row.second == old; });
  }
  byLanguageId[meta.languageId] = language;
  // A colliding key goes to the later language, with a warning naming the
  // normalized key, published once the registration stands.
  std::vector<Observation> collisions;
  const auto claim = [&](ByName& table, std::string key, const char* kind) {
    const CoreServices*& slot = table[key];
    if (slot != nullptr && slot != language)
      collisions.push_back(
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
      claim(byExtension, std::move(key), "extension");
  for (const std::string& fileName : meta.fileNames)
    if (!fileName.empty()) claim(byFileName, fileName, "file name");

  // Room for the two pushes below, so nothing past this point throws. A
  // released container stays owned until the registry dies: documents may still
  // use it.
  _owned.reserve(_owned.size() + 1);
  if (old == nullptr) _all.reserve(_all.size() + 1);
  _owned.push_back(std::move(services));
  if (old == nullptr)
    _all.push_back(language);
  else
    std::ranges::replace(_all, old, language);
  _byLanguageId.swap(byLanguageId);
  _byExtension.swap(byExtension);
  _byFileName.swap(byFileName);
  for (const Observation& collision : collisions)
    shared.observabilitySink->publish(collision);
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
