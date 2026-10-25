#pragma once

#include <optional>
#include <string_view>

#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/services/DefaultCoreService.hpp"

// Not templated: whether a class declares a `name` is a fact about the
// class, which its type tag carries, so `getName` asks the grammar nothing.
// Which record wrote that name is the other question, and only the grammar
// answers it, which is why `getNameNode` holds the services.

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
