#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/DefaultSharedCoreService.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/utils/TransparentStringHash.hpp"

// Three routing tables, one row per key. A later registration replaces the
// mapping an earlier language claimed.

namespace cursiv {

class DefaultServiceRegistry : public ServiceRegistry,
                               protected DefaultSharedCoreService {
 public:
  using DefaultSharedCoreService::DefaultSharedCoreService;

  void registerServices(std::unique_ptr<CoreServices> services) override;

  [[nodiscard]] const CoreServices& getServices(
      std::string_view uri, std::string_view languageId = {}) const override;
  [[nodiscard]] const CoreServices* findServices(
      std::string_view uri, std::string_view languageId = {}) const override;

  [[nodiscard]] bool hasServices(std::string_view uri) const override {
    return findServices(uri) != nullptr;
  }

  [[nodiscard]] std::span<const CoreServices* const> all()
      const noexcept override {
    return _all;
  }

 private:
  using ByName = std::unordered_map<std::string, const CoreServices*,
                                    TransparentStringHash, std::equal_to<>>;

  [[nodiscard]] static const CoreServices* lookup(
      const ByName& table, std::string_view key) noexcept;

  // Owns active and retired containers: documents may still name a retired
  // language. `_all` lists only the current routing targets, in registration order.
  std::vector<std::unique_ptr<CoreServices>> _owned;
  std::vector<const CoreServices*> _all;
  ByName _byLanguageId;
  ByName _byExtension;
  ByName _byFileName;
};

}  // namespace cursiv
