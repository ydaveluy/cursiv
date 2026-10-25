#include "cursiv/lsp/services/AbstractExecuteCommandHandler.hpp"

#include <algorithm>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/lsp/services/ExecuteCommandHandler.hpp"
#include "lsp/types.h"

namespace cursiv {

namespace {

[[nodiscard]] auto named(std::string_view name) {
  return [name](const auto& entry) { return entry.first == name; };
}

}  // namespace

const AbstractExecuteCommandHandler::Table&
AbstractExecuteCommandHandler::table() const {
  std::call_once(_filled, [this]() {
    _commands.emplace();
    registerCommands([this](std::string name, ExecuteCommandFunction execute) {
      // Last registration wins, in the place of the first: a later module
      // overriding an earlier one does not move the name.
      const auto found = std::ranges::find_if(*_commands, named(name));
      if (found == _commands->end())
        _commands->emplace_back(std::move(name), std::move(execute));
      else
        found->second = std::move(execute);
    });
  });
  return *_commands;
}

std::vector<std::string> AbstractExecuteCommandHandler::commands() const {
  std::vector<std::string> names;
  const Table& registered = table();
  names.reserve(registered.size());
  for (const auto& [name, execute] : registered) names.push_back(name);
  return names;
}

std::optional<lsp::LSPAny> AbstractExecuteCommandHandler::executeCommand(
    std::string_view name, const lsp::LSPArray& arguments,
    const CancellationToken& cancelToken) const {
  const Table& registered = table();
  const auto found = std::ranges::find_if(registered, named(name));
  if (found == registered.end()) return std::nullopt;
  return found->second(arguments, cancelToken);
}

}  // namespace cursiv
