#pragma once

#include "cursiv/grammar/GrammarSpec.hpp"
#include "statemachine/ast.hpp"

namespace sm {

struct SM : cursiv::GrammarSpec {
  static constexpr auto statemachineKw = ikw<"statemachine">.doc<
      "Starts the document and names the state machine.">();
  static constexpr auto eventsKw =
      ikw<"events">.doc<"Opens the events the machine reacts to.">();
  static constexpr auto initialStateKw =
      ikw<"initialState">.doc<"The state the machine starts in.">();
  static constexpr auto stateKw =
      ikw<"state">.doc<"Declares a state, closed by `end`.">();
  static constexpr auto endKw =
      ikw<"end">.doc<"Closes the state opened by `state`.">();
  static constexpr auto commandsKw =
      ikw<"commands">.doc<"Opens the commands that states can run.">();
  static constexpr auto actionsKw = ikw<"actions">.doc<
      "Lists the commands the state runs, between `{` and `}`.">();

  struct WS : terminal<some(s)> {};
  struct SL_COMMENT : terminal<lit<"//"> + many(cr<R"(^\r\n)">)> {};
  struct ML_COMMENT : terminal<through(lit<"/*">, lit<"*/">)> {};

  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};

  struct Event : rule<assign<&ast::Event::name>(ref<ID>)> {};
  struct Command : rule<assign<&ast::Command::name>(ref<ID>)> {};
  struct Transition : rule<assign<&ast::Transition::event>(ref<ID>) + kw<"=>">
                           + assign<&ast::Transition::state>(ref<ID>)> {};
  struct State
      : rule<stateKw + assign<&ast::State::name>(ref<ID>)
             + option(actionsKw + kw<"{">
                      + some(append<&ast::State::actions>(ref<ID>)) + kw<"}">)
             + many(append<&ast::State::transitions>(ref<Transition>))
             + endKw> {};
  struct Statemachine
      : rule<statemachineKw + assign<&ast::Statemachine::name>(ref<ID>)
             + option(eventsKw
                      + some(append<&ast::Statemachine::events>(ref<Event>)))
             + option(
                 commandsKw
                 + some(append<&ast::Statemachine::commands>(ref<Command>)))
             + initialStateKw + assign<&ast::Statemachine::init>(ref<ID>)
             + many(append<&ast::Statemachine::states>(ref<State>))> {};

  using entry = Statemachine;
  using ignored = WS;
  using hidden = list<ML_COMMENT, SL_COMMENT>;
};

}  // namespace sm
