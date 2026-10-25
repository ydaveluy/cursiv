// The bookkeeping every handler would otherwise repeat: a language says what
// it registers, this says how a name is found and answered. The table is
// built once, lazily, since registration may run arbitrary code and re-doing
// it per request would pay it per keystroke on a code lens.
#pragma once

#include <lsp/types.h>

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/lsp/services/ExecuteCommandHandler.hpp"

namespace cursiv {

class AbstractExecuteCommandHandler : public ExecuteCommandHandler {
 public:
  [[nodiscard]] std::vector<std::string> commands() const override;

  [[nodiscard]] std::optional<lsp::LSPAny> executeCommand(
      std::string_view name, const lsp::LSPArray& arguments,
      const CancellationToken& = {}) const override;

 protected:
  // Called once. Every `acceptor(name, fn)` declares one command.
  virtual void registerCommands(
      const ExecuteCommandAcceptor& acceptor) const = 0;

 private:
  // In registration order, which is the order `commands()` announces.
  using Table = std::vector<std::pair<std::string, ExecuteCommandFunction>>;
  [[nodiscard]] const Table& table() const;

  // A memo of a const question, filled once whichever reader asks first.
  mutable std::once_flag _filled;
  mutable std::optional<Table> _commands;
};

}  // namespace cursiv
