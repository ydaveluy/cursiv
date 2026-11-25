#include "cursiv/services/DefaultServiceRegistry.hpp"

#include <algorithm>
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
    throw ServiceRegistrationError("cursiv: the shared services are missing '" +
                                   std::string(missing) + "'");

  const LanguageMetaData& meta = services->languageMetaData;
  if (meta.languageId.empty())
    throw ServiceRegistrationError(
        "cursiv: a language is registered by its id, which is empty");
  if (const std::string_view missing = services->firstMissingService();
      !missing.empty())
    throw ServiceRegistrationError("cursiv: language '" + meta.languageId +
                                   "' is missing service '" +
                                   std::string(missing) + "'");
  // Its services were built over another workspace, and would index and
  // resolve there.
  if (&services->shared() != &shared)
    throw ServiceRegistrationError("cursiv: language '" + meta.languageId +
                                   "' was built for another workspace");

  const CoreServices* const language = services.get();
  const CoreServices* const old = lookup(_byLanguageId, meta.languageId);
  struct Claim {
    ByName& table;
    ByName::value_type& row;
    const CoreServices* previous;
  };
  std::vector<Claim> claims;
  claims.reserve(1 + meta.fileExtensions.size() + meta.fileNames.size());
  std::vector<Observation> collisions;
  const auto claim = [&](ByName& table, std::string key, const char* kind) {
    if (key.empty()) return;
    auto& row = *table.try_emplace(std::move(key), nullptr).first;
    if (row.second == language) return;
    claims.push_back({table, row, row.second});
    if (row.second && row.second != old)
      collisions.push_back({.severity = ObservationSeverity::Warning,
                            .code = ObservationCode::LanguageMappingCollision,
                            .message = std::string("two languages claim ") +
                                       kind + " '" + row.first + "'",
                            .uri = {},
                            .languageId = meta.languageId});
    row.second = language;
  };

  // Map rows keep their addresses across rehashes. Restore every changed
  // route if the registration cannot take ownership of the container.
  try {
    claim(_byLanguageId, meta.languageId, "language id");
    for (const auto& extension : meta.fileExtensions)
      claim(_byExtension, normalizeExtension(extension), "extension");
    for (const auto& name : meta.fileNames)
      claim(_byFileName, name, "file name");
    if (!old) _all.push_back(language);
    _owned.push_back(std::move(services));
  } catch (...) {
    if (!_all.empty() && _all.back() == language) _all.pop_back();
    for (const auto& [table, row, previous] : claims) {
      if (previous)
        row.second = previous;
      else
        table.erase(row.first);
    }
    throw;
  }
  if (old) {
    const auto obsolete = [old](const auto& row) { return row.second == old; };
    std::erase_if(_byExtension, obsolete);
    std::erase_if(_byFileName, obsolete);
    std::ranges::replace(_all, old, language);
  }
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
  if (const CoreServices* const named = lookup(_byFileName, uriFileName(uri)))
    return named;
  return lookup(_byExtension, uriExtension(uri));
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
  throw ServiceRegistryError("cursiv: no language claims '" + std::string(uri) +
                             "'");
}

}  // namespace cursiv
