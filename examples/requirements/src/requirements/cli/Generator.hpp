#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "requirements/core/ast.hpp"

namespace req {

std::string generate_summary_file_html_content(
    const ast::RequirementModel& model,
    std::span<const ast::TestModel* const> testModels);

std::string generate_summary(const ast::RequirementModel& model,
                             std::span<const ast::TestModel* const> testModels,
                             std::string_view filePath,
                             std::optional<std::string_view> destination);

}  // namespace req
