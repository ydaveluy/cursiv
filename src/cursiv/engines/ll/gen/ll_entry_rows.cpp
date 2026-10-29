#include "cursiv/engines/ll/gen/ll_entry_rows.hpp"

#include <algorithm>
#include <utility>

#include "cursiv/engines/ll/gen/LlFinalProgram.hpp"
#include "cursiv/engines/shared/gen/chain_run.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

namespace {

// The runs a record's chain is made of, the base first and then what each
// join added over it.
void ll_entry_runs(const LlBuiltProgram& a, std::uint16_t i,
                   std::vector<std::vector<std::uint16_t>>& runs) {
  const auto from = a.origin[i];
  if (from.from == kNoRow) {
    runs.assign(1, chain_run(a, i));
    return;
  }
  ll_entry_runs(a, from.from, runs);
  runs.push_back(chain_run(a, from.to));
}

// What fills the node a record built, and what writes that node into the
// region above: the join the construction kept the provenance of tells them
// apart.
void ll_entry_halves(const std::vector<std::vector<std::uint16_t>>& runs,
                     std::vector<std::uint16_t>& held,
                     std::vector<std::uint16_t>& over) {
  const std::size_t split = runs.size() > 1 ? runs.size() - 1 : runs.size();
  held.clear();
  for (std::size_t k = 0; k < split; ++k)
    held.insert(held.end(), runs[k].begin(), runs[k].end());
  over.clear();
  if (split < runs.size()) over = runs.back();
}

}  // namespace

AstEntryTable ll_entry_table(const LlFinalProgram& done,
                             std::span<const AstRowGen> rows,
                             std::vector<RecordGen>& records) {
  const LlBuiltProgram& a = done.program();
  const auto makes = [&](const std::vector<std::uint16_t>& run) {
    return std::ranges::any_of(
        run, [&](std::uint16_t r) { return rows[r].makes(); });
  };
  const auto opens = [&](const std::vector<std::uint16_t>& run) {
    return std::ranges::any_of(
        run, [&](std::uint16_t r) { return rows[r].opens(); });
  };
  AstEntryTable t;
  t.turnsByState = true;
  t.count = a.writes.nchains;
  t.chain.resize(t.count);
  t.from.resize(t.count, AstEntryTable::kNoEntry);
  records.resize(t.count);
  std::vector<std::vector<std::uint16_t>> raw, scopes;
  std::vector<std::uint16_t> held, over;
  std::vector<char> runMakes, runOpens, opensAfter, makesBefore;
  for (std::uint16_t e = 0; e < a.writes.nchains; ++e) {
    ll_entry_runs(a, e, raw);
    ll_entry_halves(raw, held, over);
    records[e] = record_gen(rows, held, over);
    runMakes.assign(raw.size(), 0);
    runOpens.assign(raw.size(), 0);
    bool stacked = false, made = false;
    for (std::size_t k = 0; k < raw.size(); ++k) {
      runMakes[k] = makes(raw[k]);
      runOpens[k] = opens(raw[k]);
      stacked = stacked || (made && runOpens[k]);
      made = made || runMakes[k];
    }
    scopes.clear();
    if (stacked) {
      // Past a run: whether a later one opens, and whether one makes up to
      // the first that does, that one included.
      opensAfter.assign(raw.size(), 0);
      makesBefore.assign(raw.size(), 0);
      for (std::size_t k = raw.size() - 1; k-- > 0;) {
        opensAfter[k] = runOpens[k + 1] || opensAfter[k + 1];
        makesBefore[k] =
            runMakes[k + 1] || (!runOpens[k + 1] && makesBefore[k + 1]);
      }
      bool lays = false, owed = false;
      for (std::size_t k = 0; k < raw.size(); ++k) {
        auto& run = raw[k];
        const bool opening = opensAfter[k], closing = makesBefore[k];
        if (scopes.empty()) {
          lays = runMakes[k];
          scopes.push_back(std::move(run));
        } else if (runOpens[k] && lays) {
          owed = false;
          scopes.push_back(std::move(run));
        } else if (lays && !runMakes[k] && (owed || opening)) {
          if (owed)
            scopes.back().insert(scopes.back().end(), run.begin(), run.end());
          else
            scopes.push_back(std::move(run));
          owed = true;
        } else if (owed) {
          owed = false;
          lays = runMakes[k];
          scopes.push_back(std::move(run));
        } else if (lays && !runMakes[k] && !closing) {
          // What follows the last creation is the caller's, owed outward.
          scopes.push_back(std::move(run));
        } else {
          lays = lays || runMakes[k];
          scopes.back().insert(scopes.back().end(), run.begin(), run.end());
        }
      }
    } else {
      if (makes(over)) {
        held.insert(held.end(), over.begin(), over.end());
        over.clear();
      }
      scopes.push_back(held);
      if (!over.empty()) scopes.push_back(over);
    }
    const auto own = static_cast<std::uint32_t>(t.runs.size());
    for (auto& run : scopes) t.runs.push_back(std::move(run));
    std::uint32_t before = AstEntryTable::kNoEntry;
    for (std::size_t k = 0; k + 1 < scopes.size(); ++k)
      before = t.add(own + static_cast<std::uint32_t>(k), before);
    t.chain[e] = own + static_cast<std::uint32_t>(scopes.size()) - 1;
    t.from[e] = before;
  }
  return t;
}

}  // namespace cursiv
