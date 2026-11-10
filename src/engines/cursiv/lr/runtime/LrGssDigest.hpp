// What the graph's election reads of a forest node's elected derivation: the
// fork steps it took, in order, as a rope of their positions. Derivations with
// the same forks before a position share that part.
#pragma once

#include <vector>

#include "cursiv/text/TextOffset.hpp"
#include "cursiv/lr/runtime/LrGssForest.hpp"
#include "cursiv/lr/runtime/LrGssRope.hpp"

namespace cursiv::engine {

class LrGssDigest {
 public:
  using Id = LrGssForest::Id;
  static constexpr Id none = LrGssForest::none;
  using Rope = LrGssRope<LrGssForkAgg>;

  // A step as the digest reads it: where its vertex stands, and whether that
  // vertex took others.
  struct Facts {
    TextOffset at;
    bool fork;
  };

  void clear() {
    _forks.clear();
    _of.clear();
  }

  [[nodiscard]] const Rope& forks() const { return _forks; }

  Rope::Id join(Rope::Id first, Rope::Id second) {
    return _forks.concat(first, second);
  }

  // The rope of `root`'s fork steps, and of every node under it, built where
  // missing; `none` for a derivation that took none.
  template <class Of>
  Rope::Id of(LrGssForest& forest, Id root, Of&& facts) {
    if (_of.size() < forest.size()) _of.resize(forest.size(), kUnset);
    if (_of[root] != kUnset) return _of[root];
    _work.assign(1, root);
    while (!_work.empty()) {
      const Id f = _work.back();
      if (_of[f] != kUnset) {
        _work.pop_back();
        continue;
      }
      const Id alt = forest.elect(f);
      bool ready = true;
      for (const Id child : forest.children(alt))
        if (_of[child] == kUnset) {
          _work.push_back(child);
          ready = false;
        }
      if (!ready) continue;
      _work.pop_back();
      _parts.clear();
      for (const Id child : forest.children(alt)) _parts.push_back(_of[child]);
      if (const Id step = forest.step(alt); step != none) {
        const Facts s = facts(step);
        if (s.fork) _parts.push_back(_forks.leaf(step, LrGssForkAgg{s.at}));
      }
      _of[f] = _forks.concat_all(_parts);
    }
    return _of[root];
  }

 private:
  static constexpr Rope::Id kUnset = Rope::none - 1;

  Rope _forks;
  std::vector<Rope::Id> _of;
  std::vector<Id> _work;
  std::vector<Rope::Id> _parts;
};

}  // namespace cursiv::engine
