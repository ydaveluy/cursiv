#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/core/documentation/CommentProvider.hpp"
#include "cursiv/core/services/DefaultCoreService.hpp"

// The node's lexical predecessor, when the rule named below wrote it: walk
// backward past the node's own subtree, and the first disjoint record decides.
// The image is returned as written -- the reader downstream decides how to
// render it.

namespace cursiv {

// Not `final`: a language derives it to change which comment belongs to a
// node.
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
