#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "domainmodel/core/ast.hpp"

namespace dm {

std::string generate_java(const ast::DomainModel& model,
                          std::string_view filePath,
                          std::optional<std::string_view> destination);

}  // namespace dm
