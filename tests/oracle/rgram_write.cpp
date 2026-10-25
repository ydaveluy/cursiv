// `rgram_write <seed> <header>`: the header of one random grammar. A header
// whose text did not move is not touched, or its parser would rebuild.
#include <charconv>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "oracle/RgramGrammar.hpp"

int main(int argc, char** argv) {
  unsigned seed = 0;
  const std::string_view arg = argc == 3 ? argv[1] : "";
  const auto [end, ec] =
      std::from_chars(arg.data(), arg.data() + arg.size(), seed);
  if (argc != 3 || ec != std::errc{} || end != arg.data() + arg.size()) {
    std::fprintf(stderr, "usage: %s <seed> <header>\n", argv[0]);
    return 2;
  }
  const std::string text = cursiv::RgramGrammar::draw(seed).header();
  {
    std::ifstream in(argv[2], std::ios::binary);
    if (in && std::string(std::istreambuf_iterator<char>(in), {}) == text)
      return 0;
  }
  std::ofstream out(argv[2], std::ios::binary);
  out << text;
  out.close();
  return out ? 0 : 1;
}
