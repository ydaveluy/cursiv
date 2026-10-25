#pragma once

#include <string>
#include <string_view>

#include "cursiv/core/services/DefaultCoreService.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/workspace/AstNodeLocator.hpp"

// The separators are stable: a path once written keeps reading.

namespace cursiv {

// Not `final`: a language whose paths must read differently overrides
// `getPathSegment` and keeps the walk.
class DefaultAstNodeLocator : public AstNodeLocator,
                              protected DefaultCoreService {
 public:
  using DefaultCoreService::DefaultCoreService;

  [[nodiscard]] std::string getAstNodePath(const AstNode& node) const override;
  [[nodiscard]] AstNode* getAstNode(AstNode& node,
                                    std::string_view path) const override;
  ~DefaultAstNodeLocator() noexcept override = default;

 protected:
  static constexpr char kSegmentSeparator = '/';
  static constexpr char kIndexSeparator = '@';

  // One step of a path: the member that holds `node` in its container, and the
  // place in it when the member is a list. Empty when the container holds it
  // nowhere, which is a detached node.
  [[nodiscard]] virtual std::string getPathSegment(const AstNode& node) const;
};

}  // namespace cursiv
