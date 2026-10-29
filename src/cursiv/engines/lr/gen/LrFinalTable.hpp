// The automaton once its tables are derived and its programs are laid.
#pragma once

#include <memory>
#include <span>
#include <utility>

#include "cursiv/engines/lr/gen/LrBuildTable.hpp"
#include "cursiv/engines/lr/gen/LrProgramLayout.hpp"
#include "cursiv/engines/lr/gen/lr_build_derived.hpp"

namespace cursiv {

// The build table, derived and laid out, held read-only: no pass or write
// reaches a row that a pool or a program was built from. It is the table that
// was handed in, not a copy of it.
class LrFinalTable {
 public:
  // Derives the tables of `graph` from the completion `sites`, then lays its
  // programs.
  static LrFinalTable make(std::unique_ptr<LrBuildTable> graph,
                           std::span<const ElementId> sites) {
    lr_derive_tables(*graph, sites);
    return LrFinalTable(std::move(graph));
  }

  const LrBuildTable& table() const { return *_table; }
  const LrProgramLayout& programs() const { return _programs; }

 private:
  explicit LrFinalTable(std::unique_ptr<const LrBuildTable> graph)
      : _table(std::move(graph)), _programs(*_table) {}

  std::unique_ptr<const LrBuildTable> _table;
  LrProgramLayout _programs;
};

}  // namespace cursiv
