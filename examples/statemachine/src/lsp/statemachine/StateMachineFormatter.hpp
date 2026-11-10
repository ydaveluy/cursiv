// The statemachine example's formatter, an LSP provider: how a state machine
// should look is the language's choice, not the framework's.
#pragma once

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/formatting/AbstractFormatter.hpp"
#include "statemachine/ast.hpp"

namespace sm {

using namespace cursiv;

class StatemachineFormatter final : public cursiv::AbstractFormatter {
 public:
  explicit StatemachineFormatter(const cursiv::CoreServices& core)
      : cursiv::AbstractFormatter(core) {
    on<ast::Statemachine>(&StatemachineFormatter::formatStatemachine);
    on<ast::State>(&StatemachineFormatter::formatState);
    on<ast::Transition>(&StatemachineFormatter::formatTransition);
  }

 private:
  using Builder = cursiv::FormattingBuilder;

  void formatStatemachine(Builder& builder,
                          const ast::Statemachine* model) const {
    auto formatter = builder.getNodeFormatter(model);
    formatter.keyword("statemachine").append(oneSpace);

    // The keyword moves only when the section exists: `events` with no event
    // is not written in this grammar.
    if (!model->events.empty()) {
      formatter.keyword("events").prepend(newLine);
      formatter.properties<&ast::Statemachine::events>().prepend(indent);
    }
    if (!model->commands.empty()) {
      formatter.keyword("commands").prepend(newLine);
      formatter.properties<&ast::Statemachine::commands>().prepend(indent);
    }

    formatter.keyword("initialState").prepend(newLine).append(oneSpace);
    formatter.properties<&ast::Statemachine::states>().prepend(newLines(2));
  }

  void formatState(Builder& builder, const ast::State* state) const {
    auto formatter = builder.getNodeFormatter(state);
    formatter.keyword("state").append(oneSpace);

    if (!state->actions.empty()) {
      formatter.keyword("actions").prepend(oneSpace);
      auto openBrace = formatter.keyword("{");
      auto closeBrace = formatter.keyword("}");
      openBrace.prepend(oneSpace).append(oneSpace);
      closeBrace.prepend(oneSpace);
      // The first action is spaced by the brace, every later one by this: the
      // slice keeps two rules off the same gap.
      formatter.properties<&ast::State::actions>().slice(1).prepend(oneSpace);
    }

    // A state with no transition closes on its own line, which is what
    // `state Idle end` is: nothing was indented, so there is nothing to
    // close under.
    if (state->transitions.empty()) {
      formatter.keyword("end").prepend(oneSpace);
      return;
    }

    formatter.properties<&ast::State::transitions>().prepend(indent);
    formatter.keyword("end").prepend(newLine);
  }

  void formatTransition(Builder& builder,
                        const ast::Transition* transition) const {
    auto formatter = builder.getNodeFormatter(transition);
    formatter.keyword("=>").prepend(oneSpace).append(oneSpace);
  }
};

}  // namespace sm
