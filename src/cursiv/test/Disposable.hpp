// A cleanup the caller runs explicitly, never on destruction: a check
// returns one so the document it built may outlive the check, and an
// implicit dispose on destruction would take that away.
#pragma once

#include <functional>

namespace cursiv::test {

class Disposable {
 public:
  Disposable() = default;
  explicit Disposable(std::function<void()> action);

  [[nodiscard]] static Disposable create(std::function<void()> action);

  // Idempotent: the second call has nothing left to run.
  void dispose();

 private:
  std::function<void()> _action;
};

}  // namespace cursiv::test
