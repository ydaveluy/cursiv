#include "BenchmarkServices.hpp"

int main(int argc, char** argv) {
  return cursiv::bench::runLanguage<cursiv::SharedLspServices,
                                     tiny::register_benchmark_services>(argc,
                                                                        argv);
}
