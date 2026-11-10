// One map from a section, a registered language, to the client's whole value
// for it. The section list is built once: a pull's answer is read by position.
#pragma once

#include <lsp/messages.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/runtime/LanguageClient.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/workspace/ConfigurationProvider.hpp"

namespace cursiv {

class DefaultConfigurationProvider : public ConfigurationProvider {
 public:
  explicit DefaultConfigurationProvider(
      const SharedCoreServices& shared) noexcept
      : _shared(shared) {}
  ~DefaultConfigurationProvider() noexcept override;

  void initialize(const lsp::InitializeParams&) override;
  void initialized(LanguageClient* client) override;
  [[nodiscard]] bool isReady() const noexcept override { return _ready; }

  void updateConfiguration(const lsp::DidChangeConfigurationParams&) override;
  [[nodiscard]] std::optional<lsp::LSPAny> getConfiguration(
      std::string_view language, std::string_view configuration) const override;

  void addConfigurationListener(
      ConfigurationSectionListener& listener) override {
    _listeners.push_back(&listener);
  }
  void removeConfigurationListener(
      ConfigurationSectionListener& listener) override {
    std::erase(_listeners, &listener);
  }

 protected:
  // The cache alone; dispatch happens outside its lock. The value's revision
  // when the listeners must receive it: it differs from the one held, or that
  // one never reached them all. The revision moves either way.
  std::optional<std::uint64_t> updateSectionConfiguration(
      const std::string& section, lsp::LSPAny value);

 private:
  // The language ids of every registered language, in registration order,
  // deduplicated. The list the request is built from and the list its answer is
  // read back against.
  [[nodiscard]] std::vector<std::string> sections() const;
  void pullConfiguration(std::vector<std::string> names);
  // Tells every listener, even when one fails, and keeps the first failure in
  // `failure`. The value at `revision` counts as received once all of them
  // received it.
  void deliver(std::string_view section, const lsp::LSPAny& value,
               std::uint64_t revision, std::exception_ptr& failure);

  const SharedCoreServices& _shared;
  struct Section {
    lsp::LSPAny value;
    std::uint64_t revision = 0;
    bool delivered = false;
  };
  // Transparent, so a lookup by `string_view` materialises no string.
  std::map<std::string, Section, std::less<>> _bySection;
  mutable std::mutex _mutex;
  std::vector<ConfigurationSectionListener*> _listeners;
  LanguageClient* _client = nullptr;
  std::uint64_t _revision = 0;
  std::uint64_t _pullGeneration = 0;
  bool _supported = false;
  // Whether the client accepts a `didChangeConfiguration` registration.
  bool _dynamicRegistration = false;
  std::atomic<bool> _ready = false;
};

}  // namespace cursiv
