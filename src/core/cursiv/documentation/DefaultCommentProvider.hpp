#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/documentation/CommentProvider.hpp"
#include "cursiv/services/DefaultCoreService.hpp"

// The record right before the node's subtree, when one of the comment rules
// below wrote it. Returned as written.

namespace cursiv {

class DefaultCommentProvider : public CommentProvider,
                               protected DefaultCoreService {
 public:
  // The terminal rules that hold documentation comments; a language that
  // names them differently, or has several, passes its own.
  explicit DefaultCommentProvider(
      const CoreServices& services,
      std::vector<std::string> mlCommentRuleNames = {"ML_COMMENT"})
      : DefaultCoreService(services),
        _mlCommentRuleNames(std::move(mlCommentRuleNames)) {}

  [[nodiscard]] std::optional<std::string> getComment(
      const AstNode& node, const Document* document = nullptr) const override;

 private:
  std::vector<std::string> _mlCommentRuleNames;
};

}  // namespace cursiv
