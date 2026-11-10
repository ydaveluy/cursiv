// The client's settings by section, a section being a language id.
// `initialized` sends the pull and returns; `isReady()` polls for the answer.
#pragma once

#include <lsp/json/json.h>
#include <lsp/messages.h>

#include <optional>
#include <string_view>
#include <vector>

#include "cursiv/validation/ValidationCategory.hpp"
#include "cursiv/validation/ValidationRegistry.hpp"
#include "cursiv/runtime/LanguageClient.hpp"

namespace cursiv {

// Told when a notification or a pull changes a section's value.
class ConfigurationSectionListener {
 public:
  virtual ~ConfigurationSectionListener() noexcept = default;
  virtual void onConfigurationSectionUpdate(std::string_view section,
                                            const lsp::LSPAny& value) = 0;
};

namespace detail {

// A language's `validation` setting as categories matched against `registry`
// and the built-ins, from `[...]` or `{"categories": [...]}`. Empty means all.
[[nodiscard]] std::vector<const cursiv::ValidationCategory*>
validation_categories(const lsp::LSPAny& validation,
                      const ValidationRegistry& registry);

}  // namespace detail

class ConfigurationProvider {
 public:
  virtual ~ConfigurationProvider() noexcept = default;

  // Listeners are read during dispatch without a lock: add and remove them
  // outside any dispatch.
  virtual void addConfigurationListener(ConfigurationSectionListener&) = 0;
  virtual void removeConfigurationListener(ConfigurationSectionListener&) = 0;

  // Records what the client can do: without `workspace.configuration` it is
  // never asked and every section stays unknown; `didChangeConfiguration` says
  // whether it accepts the change registration.
  virtual void initialize(const lsp::InitializeParams&) = 0;

  // Registers for the change notification if the client accepts it, and pulls
  // the current values. A null client detaches the session as it ends, and
  // must not throw.
  virtual void initialized(LanguageClient* client) = 0;

  // Whether the pull has finished; true at once when there was nothing to pull.
  [[nodiscard]] virtual bool isReady() const noexcept = 0;

  // `workspace/didChangeConfiguration`: each key of `settings` is a section,
  // whose value replaces the section whole. Null or empty settings pull afresh.
  virtual void updateConfiguration(
      const lsp::DidChangeConfigurationParams&) = 0;

  // The value of `configuration` in `language`'s section, as the client last
  // gave it. `nullopt` when it never arrived, which differs from a null value.
  [[nodiscard]] virtual std::optional<lsp::LSPAny> getConfiguration(
      std::string_view language, std::string_view configuration) const = 0;
};

}  // namespace cursiv
