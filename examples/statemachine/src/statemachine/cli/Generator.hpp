#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "statemachine/core/ast.hpp"

namespace sm {

[[nodiscard]] std::string generate_cpp_content(const ast::Statemachine& model);

[[nodiscard]] std::string generate_cpp(
    const ast::Statemachine& model, std::string_view inputPath,
    std::optional<std::string_view> destination = std::nullopt);

}  // namespace sm
