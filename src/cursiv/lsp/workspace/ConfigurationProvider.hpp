// The client's settings, as the workspace sees them, keyed by section -- a
// section is the language id a language announced. Non-blocking:
// `initialized` sends the pull and returns; `isReady()` polls for the
// answer, since waiting on it would deadlock the thread that reads it.
#pragma once

#include <lsp/json/json.h>
#include <lsp/messages.h>

#include <optional>
#include <string_view>
#include <vector>

#include "cursiv/core/validation/ValidationCategory.hpp"
#include "cursiv/core/validation/ValidationRegistry.hpp"
#include "cursiv/lsp/runtime/LanguageClient.hpp"

namespace cursiv {

/**
 * Told when a notification or a configuration pull replaces a section.
 * What a section means stays the reader's business.
 */
class ConfigurationSectionListener {
 public:
  virtual ~ConfigurationSectionListener() noexcept = default;
  virtual void onConfigurationSectionUpdate(std::string_view section,
                                            const lsp::LSPAny& value) = 0;
};

/**
 * A language's `validation` setting, as category identities matched against
 * `registry` and the runtime's built-ins. Reads either `[...]` or
 * `{"categories": [...]}`; an empty result means "everything", not "nothing
 * matched".
 */
[[nodiscard]] std::vector<const cursiv::ValidationCategory*>
validation_categories(const lsp::LSPAny& validation,
                      const ValidationRegistry& registry);

class ConfigurationProvider {
 public:
  virtual ~ConfigurationProvider() noexcept = default;

  /**
   * Registers/unregisters a listener. Listeners are read during dispatch
   * without a lock, so add and remove must happen outside any dispatch, by
   * whoever owns the subscriber's lifetime.
   */
  virtual void addConfigurationListener(ConfigurationSectionListener&) = 0;
  virtual void removeConfigurationListener(ConfigurationSectionListener&) = 0;

  /**
   * Records what the client said it can do. `workspace.configuration`
   * governs the pull: a client that does not announce it is never asked,
   * and every section stays unknown. `workspace.didChangeConfiguration`
   * says whether the client accepts the change registration.
   */
  virtual void initialize(const lsp::InitializeParams&) = 0;

  /**
   * Registers for the change notification, when the client accepts that,
   * and pulls the current values. A null client detaches the session and
   * its outstanding pulls.
   */
  virtual void initialized(LanguageClient* client) = 0;

  /**
   * Whether the pull has settled. True immediately when there was nothing
   * to pull.
   */
  [[nodiscard]] virtual bool isReady() const noexcept = 0;

  /**
   * Handles `workspace/didChangeConfiguration`. Each key of `settings` is
   * a section, and its value replaces that section whole -- never a delta
   * into it. Null or empty settings pull fresh values from the client.
   */
  virtual void updateConfiguration(
      const lsp::DidChangeConfigurationParams&) = 0;

  /**
   * The value stored for `language` under the name `configuration`, as the
   * client last gave it. `nullopt` when the section never arrived or holds no
   * such name, which is not the same as "delivered, and null".
   */
  [[nodiscard]] virtual std::optional<lsp::LSPAny> getConfiguration(
      std::string_view language, std::string_view configuration) const = 0;
};

}  // namespace cursiv
