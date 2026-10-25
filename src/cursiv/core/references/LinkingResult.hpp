#pragma once

#include <utility>
#include <variant>
#include <vector>

#include "cursiv/core/references/LinkingError.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"

namespace cursiv {

template <class T>
class LinkingResult {
 public:
  LinkingResult() = default;
  LinkingResult(T value) : _result(std::in_place_index<0>, std::move(value)) {}
  LinkingResult(LinkingError error)
      : _result(std::in_place_index<1>, std::move(error)) {}

  [[nodiscard]] bool has_value() const noexcept { return _result.index() == 0; }
  explicit operator bool() const noexcept { return has_value(); }

  T& operator*() & { return std::get<0>(_result); }
  const T& operator*() const& { return std::get<0>(_result); }
  T&& operator*() && { return std::get<0>(std::move(_result)); }
  T* operator->() { return &std::get<0>(_result); }
  const T* operator->() const { return &std::get<0>(_result); }

  LinkingError& error() & { return std::get<1>(_result); }
  const LinkingError& error() const& { return std::get<1>(_result); }
  LinkingError&& error() && { return std::get<1>(std::move(_result)); }

 private:
  std::variant<T, LinkingError> _result;
};

using AstNodeDescriptionOrError = LinkingResult<const AstNodeDescription*>;
// By value: the linker is one shared const service, so a buffer of its own
// would be a race between two documents linking at once.
using AstNodeDescriptionsOrError =
    LinkingResult<std::vector<const AstNodeDescription*>>;

}  // namespace cursiv
