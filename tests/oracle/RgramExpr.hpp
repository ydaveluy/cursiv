// One element of a random grammar's rule, as its author would spell it.
#pragma once

#include <cstdint>
#include <vector>

namespace cursiv {

enum class RgramKind : std::uint8_t {
  tok,      // `lit<"a">`
  tokNode,  // a token building a node of its own
  ref,
  opt,
  many,
  some,
  sep,  // `many(x, lit<",">)`
  rep,
  cond,
  skip,
  seq,
  alt,
};

enum class RgramCond : std::uint8_t {
  notTok,
  andTok,
  line,
  noLine,
  lineOrTok,
  none,
  notNone,
  lineOrEof,
  eof,
};

struct RgramExpr {
  RgramKind kind = RgramKind::seq;
  char tok = 0;
  unsigned ref = 0;
  RgramCond cond = RgramCond::eof;
  unsigned lo = 0, hi = 0;
  bool committed = false;
  std::vector<RgramExpr> kids{};
};

}  // namespace cursiv
