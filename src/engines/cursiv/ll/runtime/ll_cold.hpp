// The cold paths of the generated descent: reads that do not take the token in
// place, repairs, and the episode prediction. None is inlined: the parser's
// unit instantiates them once per table, and other units declare them extern.
#pragma once

#include <cstdint>

#include "cursiv/ll/runtime/LlClimbExits.hpp"
#include "cursiv/ll/runtime/LlContext.hpp"
#include "cursiv/ll/runtime/ll_conditions.hpp"
#include "cursiv/ll/runtime/ll_predictor.hpp"
#include "cursiv/ll/runtime/ll_read.hpp"
#include "cursiv/ll/runtime/ll_repair.hpp"

// The explicit instantiations of the cold paths of table `T`, at global scope:
// `How` is `extern` where declared, empty where defined. No leading `::`, which
// would run into the return type. Those every table has...
#define CURSIV_LL_COLD_PATHS(How, T)                                          \
  How template void cursiv::engine::ll_peek_read<T>(                          \
      ::cursiv::engine::LlContext&, ::std::uint16_t, ::cursiv::engine::Scan); \
  How template void cursiv::engine::ll_peek_held<T>(                          \
      ::cursiv::engine::LlContext&, ::std::uint16_t);                         \
  How template void cursiv::engine::ll_expect<T>(                             \
      ::cursiv::engine::LlContext&, ::std::uint16_t, ::std::uint16_t,         \
      ::std::uint16_t, ::std::uint16_t, ::std::uint16_t);                     \
  How template bool cursiv::engine::ll_drop<T>(::cursiv::engine::LlContext&); \
  How template ::std::uint16_t cursiv::engine::ll_choice_fix<T>(              \
      ::cursiv::engine::LlContext&, ::std::uint16_t, ::std::uint16_t);        \
  How template ::std::uint8_t cursiv::engine::ll_loop_fix<T>(                 \
      ::cursiv::engine::LlContext&, ::std::uint16_t, ::std::uint16_t)

// ...those of a table whose descent asks the prediction...
#define CURSIV_LL_PREDICT_COLD_PATHS(How, T)                      \
  How template ::std::uint16_t cursiv::engine::ll_episode<T>(     \
      ::cursiv::engine::LlContext&, ::std::uint16_t);             \
  How template ::std::uint16_t cursiv::engine::ll_reading_fix<T>( \
      ::cursiv::engine::LlContext&, ::std::uint16_t);             \
  How template bool cursiv::engine::ll_loop_leaves<T>(            \
      ::cursiv::engine::LlContext&, ::std::uint16_t)

// ...those of a table with conditions...
#define CURSIV_LL_CONDITION_COLD_PATHS(How, T)                        \
  How template void cursiv::engine::ll_condition_node<T>(             \
      ::cursiv::engine::LlContext&, ::std::uint16_t, ::std::uint16_t, \
      ::std::uint16_t)

// ...those of a table with conditions whose descent asks the prediction...
#define CURSIV_LL_PREDICT_CONDITION_COLD_PATHS(How, T)             \
  How template ::std::uint16_t cursiv::engine::ll_read_stopped<T>( \
      ::cursiv::engine::LlContext&, ::std::uint16_t)

// ...and those of a table with a precedence climb.
#define CURSIV_LL_CLIMB_COLD_PATHS(How, T)                             \
  How template ::std::uint16_t cursiv::engine::ll_climb_fix<T>(        \
      ::cursiv::engine::LlContext&, ::std::uint16_t, ::std::uint16_t); \
  How template bool cursiv::engine::ll_climb_reads_on<T>(              \
      const ::cursiv::engine::LlContext&)
