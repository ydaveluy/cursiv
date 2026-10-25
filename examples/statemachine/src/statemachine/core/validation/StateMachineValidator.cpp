#include "statemachine/core/validation/StateMachineValidator.hpp"

#include <algorithm>
#include <memory_resource>
#include <string>
#include <string_view>
#include <unordered_map>

#include "cursiv/core/utils/CaseFold.hpp"

namespace sm {

void StateMachineValidator::checkStateNameStartsWithCapital(
    const ast::State& state, cursiv::ValidationAcceptor& accept) const {
  if (!state.name.empty() && state.name.front() >= 'a' &&
      state.name.front() <= 'z')
    accept.warning(state, "State name should start with a capital letter.")
        .property<&ast::State::name>();
}

// One table over both families: a state and an event wearing the same name
// collide as surely as two states do, and each of the two is named. A name's
// first holder is reported when its first duplicate turns up.
void StateMachineValidator::checkUniqueStatesAndEvents(
    const ast::Statemachine& model, cursiv::ValidationAcceptor& accept) const {
  const auto report = [&](const cursiv::AstNode& symbol,
                          std::string_view name) {
    const std::string message =
        "Duplicate identifier name: " + std::string(name);
    if (symbol.type == &cursiv::ast_type_tag<ast::State>)
      accept.error(static_cast<const ast::State&>(symbol), message)
          .property<&ast::State::name>();
    else
      accept.error(static_cast<const ast::Event&>(symbol), message)
          .property<&ast::Event::name>();
  };
  struct First {
    const cursiv::AstNode* node;
    bool reported;
  };
  std::pmr::monotonic_buffer_resource arena;
  std::pmr::unordered_map<std::string_view, First> seen(&arena);
  seen.reserve(model.states.size() + model.events.size());
  const auto visit = [&](const cursiv::AstNode& symbol, std::string_view name) {
    const auto [at, fresh] = seen.try_emplace(name, First{&symbol, false});
    if (fresh) return;
    if (!at->second.reported) {
      report(*at->second.node, name);
      at->second.reported = true;
    }
    report(symbol, name);
  };
  for (const ast::State* const state : model.states)
    if (state != nullptr) visit(*state, state->name);
  for (const ast::Event* const event : model.events)
    if (event != nullptr) visit(*event, event->name);
}

// The words a name may not be. The grammar lexes a name as a plain token, so
// a keyword worn as one parses; it is refused here, once, instead of being
// reserved in every state the lexer has. Case-insensitive, as the keywords are.
void StateMachineValidator::checkNamesAreNotKeywords(
    const ast::Statemachine& model, cursiv::ValidationAcceptor& accept) const {
  static constexpr std::string_view kKeywords[] = {
      "statemachine", "events",  "commands", "initialstate",
      "state",        "actions", "end"};
  const auto isKeyword = [](std::string_view name) {
    return std::ranges::any_of(kKeywords, [name](std::string_view keyword) {
      return cursiv::folded_equal(name, keyword);
    });
  };
  const auto message = [](std::string_view name) {
    return "`" + std::string(name) + "` is a keyword and cannot be a name.";
  };

  if (isKeyword(model.name))
    accept.error(model, message(model.name))
        .property<&ast::Statemachine::name>();
  for (const ast::Event* const event : model.events)
    if (event != nullptr && isKeyword(event->name))
      accept.error(*event, message(event->name)).property<&ast::Event::name>();
  for (const ast::Command* const command : model.commands)
    if (command != nullptr && isKeyword(command->name))
      accept.error(*command, message(command->name))
          .property<&ast::Command::name>();
  for (const ast::State* const state : model.states)
    if (state != nullptr && isKeyword(state->name))
      accept.error(*state, message(state->name)).property<&ast::State::name>();
}

}  // namespace sm
