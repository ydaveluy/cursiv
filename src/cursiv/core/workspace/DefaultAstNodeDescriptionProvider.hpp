#pragma once

#include <optional>
#include <string_view>

#include "cursiv/core/services/DefaultCoreService.hpp"
#include "cursiv/core/workspace/AstNodeDescriptionProvider.hpp"

namespace cursiv {

class DefaultAstNodeDescriptionProvider : public AstNodeDescriptionProvider,
                                          protected DefaultCoreService {
 public:
  using DefaultCoreService::DefaultCoreService;

  [[nodiscard]] std::optional<AstNodeDescription> createDescription(
      AstNode& node, std::string_view name = {},
      const Document* document = nullptr) const override;
};

}  // namespace cursiv
