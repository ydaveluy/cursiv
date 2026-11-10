// What a client may ask the server to run, named by a code action's or a code
// lens's `command`. Names are announced at initialization and stay stable.
#pragma once

#include <lsp/types.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"

namespace cursiv {

using ExecuteCommandFunction = std::function<std::optional<lsp::LSPAny>(
    const lsp::LSPArray& arguments, const CancellationToken& cancelToken)>;

// How a handler declares one command.
using ExecuteCommandAcceptor =
    std::function<void(std::string name, ExecuteCommandFunction execute)>;

class ExecuteCommandHandler {
 public:
  virtual ~ExecuteCommandHandler() noexcept = default;

  [[nodiscard]] virtual std::vector<std::string> commands() const = 0;

  // Runs `name` with `arguments`. `nullopt`, also for an unknown name, is sent
  // as a null result, not an error.
  [[nodiscard]] virtual std::optional<lsp::LSPAny> executeCommand(
      std::string_view name, const lsp::LSPArray& arguments,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
