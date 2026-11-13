#include "cursiv/workspace/DefaultConfigurationProvider.hpp"

#include <lsp/json/json.h>
#include <lsp/messages.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/runtime/LanguageClient.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/validation/ValidationCategory.hpp"
#include "cursiv/validation/ValidationRegistry.hpp"
#include "cursiv/workspace/ConfigurationProvider.hpp"
#include "lsp/types.h"

namespace cursiv {

namespace {

// One registration, one stable id. The protocol identifies a dynamic
// registration by the id the server chose, and a server that generated a fresh
// one per session could never unregister the previous.
constexpr std::string_view kDidChangeConfigurationId =
    "cursiv.workspace.didChangeConfiguration";

}  // namespace

// Matched by description against what `registry` declares: configuration
// is the one place a category must arrive as text.
std::vector<const cursiv::ValidationCategory*> detail::validation_categories(
    const lsp::LSPAny& validation, const ValidationRegistry& registry) {
  std::vector<const cursiv::ValidationCategory*> out;
  const lsp::LSPAny* names = &validation;
  if (names->isObject()) {
    names = names->object().find("categories");
    if (names == nullptr) return out;
  }
  if (!names->isArray()) return out;

  for (const lsp::LSPAny& name : names->array()) {
    if (!name.isString()) continue;
    const std::string& spelled = name.string();
    const cursiv::ValidationCategory* found = nullptr;
    // The built-in categories match by name whether or not a check declared
    // them yet ("fast" always means cursiv's `fast`); the language's own
    // categories match through its registry.
    if (spelled == cursiv::built_in.description) {
      found = &cursiv::built_in;
    } else if (spelled == cursiv::fast.description) {
      found = &cursiv::fast;
    } else if (spelled == cursiv::slow.description) {
      found = &cursiv::slow;
    } else {
      for (const cursiv::ValidationCategory* category : registry.categories())
        if (category->description != nullptr &&
            spelled == category->description) {
          found = category;
          break;
        }
    }
    if (found != nullptr && std::ranges::find(out, found) == out.end())
      out.push_back(found);
  }
  return out;
}

DefaultConfigurationProvider::~DefaultConfigurationProvider() noexcept =
    default;

std::optional<std::uint64_t>
DefaultConfigurationProvider::updateSectionConfiguration(
    const std::string& section, lsp::LSPAny value) {
  const std::scoped_lock lock(_mutex);
  const auto previous = _bySection.find(section);
  const bool changed = previous == _bySection.end() ||
                       !previous->second.delivered ||
                       previous->second.value != value;
  _bySection.insert_or_assign(section,
                              Section{std::move(value), ++_revision, !changed});
  if (!changed) return std::nullopt;
  return _revision;
}

void DefaultConfigurationProvider::deliver(std::string_view section,
                                           const lsp::LSPAny& value,
                                           std::uint64_t revision,
                                           std::exception_ptr& failure) {
  bool delivered = true;
  for (ConfigurationSectionListener* listener : _listeners) {
    try {
      listener->onConfigurationSectionUpdate(section, value);
    } catch (...) {
      delivered = false;
      if (!failure) failure = std::current_exception();
    }
  }
  if (!delivered) return;
  const std::scoped_lock lock(_mutex);
  if (const auto at = _bySection.find(section);
      at != _bySection.end() && at->second.revision == revision)
    at->second.delivered = true;
}

std::vector<std::string> DefaultConfigurationProvider::sections() const {
  std::vector<std::string> names;
  for (const CoreServices* language : _shared.serviceRegistry->all()) {
    const std::string& id = language->languageMetaData.languageId;
    if (!id.empty() && std::ranges::find(names, id) == names.end())
      names.push_back(id);
  }
  return names;
}

void DefaultConfigurationProvider::initialize(
    const lsp::InitializeParams& params) {
  _supported = params.capabilities.workspace.has_value() &&
               params.capabilities.workspace->configuration.value_or(false);
  _dynamicRegistration =
      params.capabilities.workspace.has_value() &&
      params.capabilities.workspace->didChangeConfiguration.has_value() &&
      params.capabilities.workspace->didChangeConfiguration->dynamicRegistration
          .value_or(false);
}

void DefaultConfigurationProvider::initialized(LanguageClient* client) {
  _client = client;
  // Nothing to pull is ready at once, so no reader waits for a client that
  // never answers. A detach, from a dying session, allocates nothing.
  std::vector<std::string> names;
  if (_supported && client != nullptr) names = sections();
  if (names.empty()) {
    const std::scoped_lock lock(_mutex);
    ++_pullGeneration;
    _ready = true;
    return;
  }

  // Subscribe before pulling. A change arriving before the reply takes
  // precedence over the initial snapshot. Only a client that accepts the
  // registration is asked; another may still send the notification itself.
  if (_dynamicRegistration) {
    lsp::DidChangeConfigurationRegistrationOptions options;
    if (names.size() == 1)
      options.section = names.front();
    else
      options.section = lsp::Array<lsp::String>(names.begin(), names.end());
    lsp::Registration registration;
    registration.id = std::string(kDidChangeConfigurationId);
    registration.method = std::string(
        lsp::notifications::WorkspaceDidChangeConfiguration::Method);
    lsp::json::Writer writer;
    lsp::writeJson(options, writer);
    registration.registerOptions = lsp::json::parse(writer.text());
    lsp::RegistrationParams registrationParams;
    registrationParams.registrations.push_back(std::move(registration));
    client->registerCapability(std::move(registrationParams));
  }

  pullConfiguration(std::move(names));
}

void DefaultConfigurationProvider::pullConfiguration(
    std::vector<std::string> names) {
  if (!_supported || _client == nullptr || names.empty()) return;
  std::uint64_t revision;
  std::uint64_t generation;
  {
    const std::scoped_lock lock(_mutex);
    revision = _revision;
    generation = ++_pullGeneration;
    _ready = false;
  }

  // No `scopeUri`: the workspace-level value of each section.
  lsp::ConfigurationParams request;
  request.items.reserve(names.size());
  for (const std::string& name : names) {
    lsp::ConfigurationItem item;
    item.section = name;
    request.items.push_back(std::move(item));
  }

  // `this` outlives the answer: the provider is a slot of the shared container
  // and the container outlives the session that installed the client.
  _client->configuration(
      std::move(request),
      [this, names = std::move(names), revision,
       generation](std::vector<lsp::LSPAny> values) {
        // Matched by position, clamped by length. The protocol answers one
        // value per item in the order asked; a client that answers short is
        // wrong, and reading past its array would be worse than believing it.
        const std::size_t count = std::min(names.size(), values.size());
        std::vector<std::size_t> changed;
        {
          const std::scoped_lock lock(_mutex);
          if (generation != _pullGeneration) return;
          changed.reserve(count);
          for (std::size_t i = 0; i < count; ++i) {
            const auto previous = _bySection.find(names[i]);
            if (previous != _bySection.end() &&
                previous->second.revision > revision)
              continue;
            const bool toDeliver = previous == _bySection.end() ||
                                   !previous->second.delivered ||
                                   previous->second.value != values[i];
            if (toDeliver) changed.push_back(i);
            _bySection.insert_or_assign(
                names[i], Section{values[i], revision, !toDeliver});
          }
          _ready = true;
        }
        std::exception_ptr failure;
        for (const std::size_t i : changed)
          deliver(names[i], values[i], revision, failure);
        if (failure) std::rethrow_exception(failure);
      });
}

void DefaultConfigurationProvider::updateConfiguration(
    const lsp::DidChangeConfigurationParams& params) {
  // Pull clients announce a change without sending values. VS Code also
  // sends an empty object when a registered section has been removed.
  if (params.settings.isNull() ||
      (params.settings.isObject() && params.settings.object().isEmpty())) {
    pullConfiguration(sections());
    return;
  }
  if (!params.settings.isObject()) return;
  std::exception_ptr failure;
  for (const auto& [section, value] : params.settings.object()) {
    // A value sent again is no change, as for a pulled one: the language is
    // not revalidated for it.
    const std::optional<std::uint64_t> revision =
        updateSectionConfiguration(section, value);
    if (!revision.has_value()) continue;
    // Notify outside the cache lock: listeners may read the new settings.
    deliver(section, value, *revision, failure);
  }
  if (failure) std::rethrow_exception(failure);
}

std::optional<lsp::LSPAny> DefaultConfigurationProvider::getConfiguration(
    std::string_view language, std::string_view configuration) const {
  const std::scoped_lock lock(_mutex);
  const auto it = _bySection.find(language);
  if (it == _bySection.end() || !it->second.value.isObject()) return std::nullopt;
  const lsp::LSPAny* value = it->second.value.object().find(configuration);
  if (value == nullptr) return std::nullopt;
  return *value;
}

}  // namespace cursiv
