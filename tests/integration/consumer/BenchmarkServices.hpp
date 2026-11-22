#pragma once

#include "cursiv/LanguageBench.hpp"
#include "tiny/generated/Tiny/Module.hpp"

namespace tiny {

inline void register_benchmark_services(cursiv::SharedCoreServices& shared) {
  auto services = std::make_unique<cursiv::LspServices>(
      dynamic_cast<cursiv::SharedLspServices&>(shared));
  cursiv::installDefaultCoreServices(*services);
  generated::Tiny::installGeneratedModule(*services);
  cursiv::installDefaultLspServices(*services);
  shared.serviceRegistry->registerServices(std::move(services));
}

}  // namespace tiny
