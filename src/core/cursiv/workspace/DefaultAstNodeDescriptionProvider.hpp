#pragma once

#include <optional>
#include <string_view>

#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/workspace/AstNodeDescriptionProvider.hpp"

namespace cursiv {

class DefaultAstNodeDescriptionProvider : public AstNodeDescriptionProvider,
                                          protected DefaultCoreService {
 public:
  using DefaultCoreService::DefaultCoreService;

  using AstNodeDescriptionProvider::createDescription;
  [[nodiscard]] std::optional<AstNodeDescription> createDescription(
      AstNode& node, std::string_view name = {},
      const Document* document = nullptr) const override;
};

}  // namespace cursiv
