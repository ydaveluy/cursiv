#include "cursiv/test/Disposable.hpp"

#include <functional>
#include <utility>

namespace cursiv::test {

Disposable::Disposable(std::function<void()> action)
    : _action(std::move(action)) {}

Disposable Disposable::create(std::function<void()> action) {
  return Disposable(std::move(action));
}

void Disposable::dispose() {
  if (!_action) return;
  const std::function<void()> action = std::move(_action);
  _action = nullptr;
  action();
}

}  // namespace cursiv::test
