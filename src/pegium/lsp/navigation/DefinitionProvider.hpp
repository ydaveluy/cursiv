#pragma once

#include <optional>
#include <vector>

#include <lsp/types.h>

#include <pegium/core/utils/Cancellation.hpp>
#include <pegium/core/workspace/Document.hpp>

namespace pegium {

/// Provides definition targets for one document position.
class DefinitionProvider {
public:
  virtual ~DefinitionProvider() noexcept = default;
  /// Returns definition links for `params`.
  virtual std::optional<std::vector<::lsp::LocationLink>>
  getDefinition(const workspace::Document &document,
                const ::lsp::DefinitionParams &params,
                const utils::CancellationToken &cancelToken =
                    utils::default_cancel_token) const = 0;
};

} // namespace pegium
