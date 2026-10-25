#include "cursiv/lsp/workspace/DefaultConfigurationProvider.hpp"

#include <lsp/json/json.h>
#include <lsp/messages.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/validation/ValidationCategory.hpp"
#include "cursiv/core/validation/ValidationRegistry.hpp"
#include "cursiv/lsp/runtime/LanguageClient.hpp"
#include "cursiv/lsp/workspace/ConfigurationProvider.hpp"
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
std::vector<const cursiv::ValidationCategory*> validation_categories(
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
    // The shipped palette answers by name whether or not a check declared it
    // yet -- "fast" always means cursiv's `fast` -- and the language's own
    // inventions answer through the registry that filed them.
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

void DefaultConfigurationProvider::updateSectionConfiguration(
    const std::string& section, lsp::LSPAny value) {
  const std::scoped_lock lock(_mutex);
  _bySection.insert_or_assign(section, Section{std::move(value), ++_revision});
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
}

void DefaultConfigurationProvider::initialized(LanguageClient* client) {
  // Nothing to pull is ready, immediately: a client that never answers
  // `workspace/configuration` leaves every section unknown for the whole
  // session, and saying so at once keeps a reader from waiting forever.
  std::vector<std::string> names = sections();
  _client = client;
  if (!_supported || client == nullptr || names.empty()) {
    const std::scoped_lock lock(_mutex);
    ++_pullGeneration;
    _ready = true;
    return;
  }

  // Subscribe before pulling. A change arriving before the reply takes
  // precedence over the initial snapshot.
  lsp::DidChangeConfigurationRegistrationOptions options;
  if (names.size() == 1)
    options.section = names.front();
  else
    options.section = lsp::Array<lsp::String>(names.begin(), names.end());
  lsp::Registration registration;
  registration.id = std::string(kDidChangeConfigurationId);
  registration.method =
      std::string(lsp::notifications::WorkspaceDidChangeConfiguration::Method);
  lsp::json::Writer writer;
  lsp::writeJson(options, writer);
  registration.registerOptions = lsp::json::parse(writer.text());
  lsp::RegistrationParams registrationParams;
  registrationParams.registrations.push_back(std::move(registration));
  client->registerCapability(std::move(registrationParams));

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

  // No `scopeUri`: this is the workspace-level value of each section. A
  // per-document scope would be one request per document per section, for a
  // question nothing in this workspace asks per document.
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
          for (std::size_t i = 0; i < count; ++i) {
            const auto previous = _bySection.find(names[i]);
            if (previous != _bySection.end() &&
                previous->second.revision > revision)
              continue;
            if (previous == _bySection.end() ||
                previous->second.value != values[i])
              changed.push_back(i);
            _bySection.insert_or_assign(names[i], Section{values[i], revision});
          }
          _ready = true;
        }
        for (const std::size_t i : changed)
          for (ConfigurationSectionListener* listener : _listeners)
            listener->onConfigurationSectionUpdate(names[i], values[i]);
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
  for (const auto& [section, value] : params.settings.object()) {
    updateSectionConfiguration(section, value);
    // Notify outside the cache lock: listeners may read the new settings.
    for (ConfigurationSectionListener* listener : _listeners)
      listener->onConfigurationSectionUpdate(section, value);
  }
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
