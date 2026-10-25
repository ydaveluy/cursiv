#include "BenchmarkServices.hpp"

int main(int argc, char** argv) {
  return cursiv::bench::run_language<cursiv::SharedLspServices,
                                     tiny::register_benchmark_services>(argc,
                                                                        argv);
}
