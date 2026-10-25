// A random grammar as the productions an Earley chart reads, written from the
// grammar alone: a region is a nonterminal whose inner gaps must be empty, a
// condition an empty item that holds at its position or not.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "oracle/RgramDocument.hpp"
#include "oracle/RgramExpr.hpp"
#include "oracle/RgramGrammar.hpp"
#include "oracle/RgramRandom.hpp"
#include "oracle/RgramVerdict.hpp"

namespace cursiv {

struct RgramCfg {
  struct Sym {
    enum class Kind : std::uint8_t { term, nt, cond };
    Kind kind = Kind::term;
    char tok = 0;
    unsigned nt = 0;
    RgramCond cond = RgramCond::eof;
    // A condition inside a region reads the gap as empty.
    bool tight = false;
  };
  struct Prod {
    unsigned lhs = 0;
    std::vector<Sym> rhs{};
  };

  std::vector<Prod> prods{};
  // Per nonterminal: its productions, the node it builds (empty for none),
  // and whether it is a region. Nonterminal 0 is the entry.
  std::vector<std::vector<unsigned>> of{};
  std::vector<std::string> node{};
  std::vector<char> region{};

  explicit RgramCfg(const RgramGrammar& g);

  std::vector<RgramDocument> documents(std::uint64_t seed,
                                       unsigned count) const;
  RgramVerdict read(const RgramDocument& d, bool tree) const;

 private:
  const std::vector<RgramExpr>* _rules = nullptr;
  std::vector<unsigned> _tightOf{};
  std::vector<unsigned> _tokOf{};

  unsigned fresh(std::string name = {}, bool isRegion = false);
  void push(unsigned lhs, std::vector<Sym> rhs);
  void add(unsigned lhs, const RgramExpr& e, bool tight);
  std::vector<Sym> syms(const RgramExpr& e, bool tight);
  bool derive(RgramRandom& r, unsigned nt, unsigned depth, bool inRegion,
              std::vector<char>& toks, std::vector<char>& glued) const;
};

}  // namespace cursiv
