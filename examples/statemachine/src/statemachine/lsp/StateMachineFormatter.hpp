// The statemachine example's formatter: an LSP provider, and the only part
// of this example that knows the protocol. Formatting lives here and not in
// `src/`, for the same reason `validation.hpp` does: what a state machine
// should look like is the language's question, never the framework's.
#pragma once

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/lsp/formatting/AbstractFormatter.hpp"
#include "statemachine/core/ast.hpp"

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

    // The keyword only moves when the section exists: `events` with no event
    // is not written at all in this grammar, so a rule about an absent word
    // would select nothing -- guarding on the AST says so once, not twice.
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
      // slice is what keeps one rule from fighting another over the same gap.
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
