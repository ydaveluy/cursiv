#include "BenchmarkServices.hpp"

std::string corpus(std::size_t bytes) {
  std::string text = "1 + 2";
  while (text.size() < bytes) text += " + 3";
  return text;
}

std::string file(std::size_t index, std::size_t bytes) {
  return std::to_string(index) + " + " + corpus(bytes);
}

int main(int argc, char** argv) {
  return cursiv::bench::run_language<cursiv::SharedLspServices,
                                     tiny::register_benchmark_services>(
      argc, argv, corpus, file);
}
