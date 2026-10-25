// What a client may ask the server to run -- the other half of a code
// action or a code lens, whose `command` names something only the server
// can do. Names are advertised at initialisation and must stay stable: an
// unannounced command is one no client will ever send.
#pragma once

#include <lsp/types.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"

namespace cursiv {

// The payload is the protocol's own `LSPAny`: an argument list crosses the wire
// as JSON and this storey is where it is already parsed.
using ExecuteCommandFunction = std::function<std::optional<lsp::LSPAny>(
    const lsp::LSPArray& arguments, const CancellationToken& cancelToken)>;

// How a handler declares one command. A callback rather than a returned map:
// registration writes into whatever the caller is filling, so a handler that
// declares nothing costs no container.
using ExecuteCommandAcceptor =
    std::function<void(std::string name, ExecuteCommandFunction execute)>;

class ExecuteCommandHandler {
 public:
  virtual ~ExecuteCommandHandler() noexcept = default;

  [[nodiscard]] virtual std::vector<std::string> commands() const = 0;

  /**
   * Runs `name` with `arguments`. `nullopt` means the command produced no
   * result, sent as null rather than an error; an unknown name answers the
   * same way instead of throwing across the wire.
   */
  [[nodiscard]] virtual std::optional<lsp::LSPAny> executeCommand(
      std::string_view name, const lsp::LSPArray& arguments,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
