// The cold paths of the generated descent: the reads that do not take the
// token in place, the repairs, and the prediction an episode runs. None is
// inlined, and together they are most of the runtime a table instantiates:
// where the rules span several units, the parser's own unit instantiates them
// once per table, and every other unit declares them extern and compiles none
// of them.
#pragma once

#include <cstdint>

#include "cursiv/engines/ll/runtime/LlClimbExits.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/ll_conditions.hpp"
#include "cursiv/engines/ll/runtime/ll_predictor.hpp"
#include "cursiv/engines/ll/runtime/ll_read.hpp"
#include "cursiv/engines/ll/runtime/ll_repair.hpp"

// The explicit instantiations of the cold paths of table `T`, at global scope:
// `How` is `extern` where they are only declared, empty where they are
// defined. The names are spelt without a leading `::`, which would run into
// the return type. Those every table has...
#define CURSIV_LL_COLD_PATHS(How, T)                                           \
  How template void cursiv::ll_peek_read<T>(::cursiv::LlContext&,              \
                                            ::std::uint16_t, ::cursiv::Scan);  \
  How template void cursiv::ll_expect_repair<T>(                               \
      ::cursiv::LlContext&, ::std::uint16_t, ::std::uint16_t, ::std::uint16_t, \
      ::std::uint16_t);                                                        \
  How template void cursiv::ll_expect_arm<T>(::cursiv::LlContext&,             \
                                             const ::cursiv::LlTermArm&);      \
  How template bool cursiv::ll_drop<T>(::cursiv::LlContext&);                  \
  How template ::std::uint16_t cursiv::ll_choice_fix<T>(                       \
      ::cursiv::LlContext&, ::std::uint16_t, ::std::uint16_t);                 \
  How template ::std::uint8_t cursiv::ll_loop_fix<T>(                          \
      ::cursiv::LlContext&, ::std::uint16_t, ::std::uint16_t)

// ...those of a table whose descent asks the prediction...
#define CURSIV_LL_PREDICT_COLD_PATHS(How, T)                                   \
  How template ::std::uint16_t cursiv::ll_episode<T>(::cursiv::LlContext&,     \
                                                     ::std::uint16_t);         \
  How template ::std::uint16_t cursiv::ll_reading_fix<T>(::cursiv::LlContext&, \
                                                         ::std::uint16_t);     \
  How template bool cursiv::ll_loop_leaves<T>(::cursiv::LlContext&,            \
                                              ::std::uint16_t)

// ...those of a table with conditions...
#define CURSIV_LL_CONDITION_COLD_PATHS(How, T)    \
  How template void cursiv::ll_condition_node<T>( \
      ::cursiv::LlContext&, ::std::uint16_t, ::std::uint16_t, ::std::uint16_t)

// ...and those of a table with a precedence climb.
#define CURSIV_LL_CLIMB_COLD_PATHS(How, T)                     \
  How template ::std::uint16_t cursiv::ll_climb_fix<T>(        \
      ::cursiv::LlContext&, ::std::uint16_t, ::std::uint16_t); \
  How template bool cursiv::ll_climb_reads_on<T>(const ::cursiv::LlContext&)
