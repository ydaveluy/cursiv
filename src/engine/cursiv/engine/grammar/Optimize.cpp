#include "cursiv/engine/grammar/Optimize.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/engine/parser/Tables.hpp"

namespace cursiv::engine::gen {

namespace {

// A list read after its first item, `X N` with `N -> | N S X`, takes that
// item as its base, `N -> X | N S X`: no production of it reads nothing.
void seed_lists(Cfg& cfg) {
  const std::size_t nnts = cfg.nonterminals;
  // Per nonterminal, its productions where it has two: an empty base, then
  // a turn.
  std::vector<std::uint32_t> baseOf(nnts, kNone), turnOf(nnts, kNone), had(nnts, 0);
  for (std::uint32_t p = 0; p < cfg.productions.size(); ++p) {
    const std::uint32_t nt = cfg.productions[p].lhs;
    (had[nt] == 0 ? baseOf : turnOf)[nt] = p;
    ++had[nt];
  }
  const auto plain = [](const Production& q) {
    return q.entry == kNone && !q.tested() && !q.start && !q.implied && !q.prefixTurn &&
           q.rank == 0 && q.scopes.empty();
  };
  std::vector<bool> lists(nnts, false);
  for (std::uint32_t nt = 0; nt < nnts; ++nt) {
    if (had[nt] != 2) continue;
    const Production& base = cfg.productions[baseOf[nt]];
    const Production& turn = cfg.productions[turnOf[nt]];
    lists[nt] = base.rhs.empty() && turn.rhs.size() >= 2 && turn.rhs[0] == nonterminal(nt) &&
                plain(base) && plain(turn) && base.region == turn.region &&
                std::ranges::count(turn.rhs, nonterminal(nt)) == 1;
  }
  const auto list = [&](Symbol s) { return !is_terminal(s) && lists[index_of(s)]; };
  // Per list, the symbols every use reads before it as its turn ends, none
  // of them a list: as many as its turn has where no use is met yet.
  std::vector<std::size_t> count(nnts);
  for (std::uint32_t nt = 0; nt < nnts; ++nt)
    if (lists[nt]) count[nt] = cfg.productions[turnOf[nt]].rhs.size();
  for (const Production& user : cfg.productions)
    for (std::size_t at = 0; at < user.rhs.size(); ++at) {
      if (!list(user.rhs[at])) continue;
      const std::uint32_t nt = index_of(user.rhs[at]);
      if (at == 0 && user.lhs == nt) continue;
      const Production& turn = cfg.productions[turnOf[nt]];
      std::size_t same = 0;
      if (user.region == turn.region && user.scopes.empty() && !user.prefixTurn) {
        const std::size_t most = std::min({count[nt], turn.rhs.size() - 1, at});
        while (same < most && user.rhs[at - 1 - same] == turn.rhs[turn.rhs.size() - 1 - same] &&
               !list(user.rhs[at - 1 - same]))
          ++same;
      }
      count[nt] = same;
    }
  const auto moves = [&](Symbol s) {
    return list(s) && count[index_of(s)] != cfg.productions[turnOf[index_of(s)]].rhs.size()
               ? count[index_of(s)] : 0;
  };
  std::vector<Symbol> kept;
  for (std::uint32_t p = 0; p < cfg.productions.size(); ++p) {
    Production& user = cfg.productions[p];
    if (lists[user.lhs] && p == baseOf[user.lhs]) {
      const std::vector<Symbol>& turn = cfg.productions[turnOf[user.lhs]].rhs;
      user.rhs.assign(turn.end() - static_cast<std::ptrdiff_t>(moves(nonterminal(user.lhs))),
                      turn.end());
      continue;
    }
    kept.clear();
    for (std::size_t at = 0; at < user.rhs.size(); ++at) {
      if (at != 0 || !list(user.rhs[0]) || index_of(user.rhs[0]) != user.lhs)
        kept.resize(kept.size() - moves(user.rhs[at]));
      kept.push_back(user.rhs[at]);
    }
    user.rhs.assign(kept.begin(), kept.end());
  }
}

// A nonterminal that only gives an entry to the one record of a left
// recursion, `W -> Y`, is read as `Y` where it is used, and the production
// that reads it gives the record that entry: no state reduces `W` for free.
void fold_wrappers(Cfg& cfg) {
  const std::size_t nnts = cfg.nonterminals;
  // Per nonterminal: its production where it has one; whether it calls
  // itself first; its productions not yet known to write one record, and
  // the nonterminals that read it alone.
  constexpr std::uint32_t kSeveral = kNone - 1;
  std::vector<std::uint32_t> only(nnts, kNone), waits(nnts, 0);
  std::vector<bool> recursive(nnts, false);
  std::vector<std::vector<std::uint32_t>> readers(nnts);
  for (std::uint32_t p = 0; p < cfg.productions.size(); ++p) {
    const Production& q = cfg.productions[p];
    only[q.lhs] = only[q.lhs] == kNone ? p : kSeveral;
    if (!q.rhs.empty() && q.rhs[0] == nonterminal(q.lhs)) recursive[q.lhs] = true;
    if (q.entry != kNone || (q.rhs.size() == 1 && is_terminal(q.rhs[0]))) continue;
    ++waits[q.lhs];
    if (q.rhs.size() == 1) readers[index_of(q.rhs[0])].push_back(q.lhs);
  }
  // Whether a nonterminal always writes one record.
  std::vector<bool> single(nnts, false);
  std::vector<std::uint32_t> work;
  for (std::uint32_t nt = 0; nt < nnts; ++nt)
    if (waits[nt] == 0) work.push_back(nt);
  while (!work.empty()) {
    const std::uint32_t nt = work.back();
    work.pop_back();
    single[nt] = true;
    for (const std::uint32_t reader : readers[nt])
      if (--waits[reader] == 0) work.push_back(reader);
  }
  const auto folds = [&](std::uint32_t nt) {
    if (only[nt] >= kSeveral) return false;
    const Production& q = cfg.productions[only[nt]];
    if (q.rhs.size() != 1 || is_terminal(q.rhs[0]) || q.entry == kNone || q.tested() || q.start ||
        q.implied || q.prefixTurn || q.rank != 0 || !q.scopes.empty() || !q.takes.empty())
      return false;
    const std::uint32_t read = index_of(q.rhs[0]);
    return read != nt && recursive[read] && single[read];
  };
  for (Production& user : cfg.productions) {
    if (!user.scopes.empty()) continue;
    std::size_t taken = 0;
    for (std::size_t at = 0; at < user.rhs.size() && taken != kMostTakes; ++at) {
      if (is_terminal(user.rhs[at]) || !folds(index_of(user.rhs[at]))) continue;
      const Production& wrapper = cfg.productions[only[index_of(user.rhs[at])]];
      if (wrapper.region != user.region) continue;
      user.rhs[at] = wrapper.rhs[0];
      if (user.takes.empty()) user.takes.assign(user.rhs.size(), kNone);
      user.takes[at] = wrapper.entry;
      ++taken;
    }
  }
}

}  // namespace

void optimize(Cfg& cfg) {
  seed_lists(cfg);
  fold_wrappers(cfg);
}

}  // namespace cursiv::engine::gen
