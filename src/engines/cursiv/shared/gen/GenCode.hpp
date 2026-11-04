// One code for a refusal, and one per unit that failed to land.
#pragma once

#define CURSIV_GEN_STRINGIFY_IMPL(...) #__VA_ARGS__
#define CURSIV_GEN_STRINGIFY(...) CURSIV_GEN_STRINGIFY_IMPL(__VA_ARGS__)

namespace cursiv {

enum class GenCode : int {
  ok = 0,
  refused = 1,
  usage = 2,
  table = 5,
  parser = 6,
  parserFiles = 7,
  grammarText = 8,
  ast = 9,
  records = 10,
  module = 11,
};

constexpr int exit_status(GenCode c) { return static_cast<int>(c); }

}  // namespace cursiv
