#pragma once

#include "cursiv/core/references/DefaultScopeProvider.hpp"

// Symbol names match regardless of case: `StrinG` resolves the `String` the
// document declares, whose spelling stays as written.

namespace dm {

class DomainModelScopeProvider final : public cursiv::DefaultScopeProvider {
 public:
  using DefaultScopeProvider::DefaultScopeProvider;

 protected:
  [[nodiscard]] bool isCaseSensitive(
      const cursiv::ReferenceInfo&) const noexcept override {
    return false;
  }
};

}  // namespace dm
