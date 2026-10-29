#pragma once

#include "cursiv/core/grammar/GrammarSpec.hpp"
#include "statemachine/core/ast.hpp"

namespace sm {

namespace x = cursiv;

inline constexpr auto eventsKw =
    x::ikw<"events">.doc<"Opens the events the machine reacts to.">();
inline constexpr auto initialStateKw =
    x::ikw<"initialState">.doc<"The state the machine starts in.">();
inline constexpr auto stateKw =
    x::ikw<"state">.doc<"Declares a state, closed by `end`.">();
inline constexpr auto endKw =
    x::ikw<"end">.doc<"Closes the state opened by `state`.">();

struct SM : cursiv::GrammarSpec {
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
             + option(ikw<"actions"> + kw<"{">
                      + some(append<&ast::State::actions>(ref<ID>)) + kw<"}">)
             + many(append<&ast::State::transitions>(ref<Transition>))
             + endKw> {};
  struct Statemachine
      : rule<ikw<"statemachine"> + assign<&ast::Statemachine::name>(ref<ID>)
             + option(eventsKw
                      + some(append<&ast::Statemachine::events>(ref<Event>)))
             + option(
                 ikw<"commands">
                 + some(append<&ast::Statemachine::commands>(ref<Command>)))
             + initialStateKw + assign<&ast::Statemachine::init>(ref<ID>)
             + many(append<&ast::Statemachine::states>(ref<State>))> {};

  using entry = Statemachine;
  using ignored = WS;
  using hidden = list<ML_COMMENT, SL_COMMENT>;
};

}  // namespace sm
