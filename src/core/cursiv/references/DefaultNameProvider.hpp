#pragma once

#include <optional>
#include <string_view>

#include "cursiv/references/NameProvider.hpp"
#include "cursiv/services/DefaultCoreService.hpp"

namespace cursiv {

class DefaultNameProvider : public NameProvider, protected DefaultCoreService {
 public:
  using DefaultCoreService::DefaultCoreService;

  [[nodiscard]] std::optional<std::string_view> getName(
      const AstNode& node) const override;

  [[nodiscard]] std::optional<CstNodeView> getNameNode(
      const AstNode& node, const Document* document = nullptr) const override;

 private:
  // The member a grammar spells a declared name with.
  static constexpr std::string_view kNameFeature = "name";
};

}  // namespace cursiv
