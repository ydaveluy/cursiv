#include "requirements/Generator.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "cursiv/text/TextBuilder.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/workspace/Document.hpp"
#include "requirements/CliUtils.hpp"

namespace req {
namespace {

using namespace req::ast;
using cursiv::TextBuilder;

[[nodiscard]] std::string document_source_path(const cursiv::AstNode& node) {
  const cursiv::Document* const document = node.getDocument();
  if (document == nullptr) return {};
  return cursiv::file_uri_to_path(document->uri).value_or(document->uri);
}

// Escapes text for safe inclusion in the generated HTML body (paths can contain
// '&', '<', '>').
[[nodiscard]] std::string html_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

}  // namespace

std::string generate_summary_file_html_content(
    const ast::RequirementModel& model,
    std::span<const ast::TestModel* const> testModels) {
  TextBuilder out;
  out.line("<html>");
  out.line("<body>");
  out.line("<h1>Requirement coverage (demo generator)</h1>");
  out.line("<div>Source: ", html_escape(document_source_path(model)), "</div>");
  out.line(R"(<table border="1">)");
  out.line("<TR><TH>Requirement ID</TH><TH>Testcase ID</TH></TR>");
  for (const auto& requirement : model.requirements) {
    if (!requirement) {
      continue;
    }

    out.line("<TR><TD>", requirement->name, "</TD><TD>");
    for (const auto* testModel : testModels) {
      if (testModel == nullptr) {
        continue;
      }
      for (const auto& test : testModel->tests) {
        if (!test) {
          continue;
        }
        const bool referencesRequirement =
            std::ranges::any_of(test->requirements, [&](const auto& reference) {
              return reference.get() == requirement;
            });
        if (!referencesRequirement) {
          continue;
        }

        out.line("<div>", test->name, " (from ",
                 html_escape(document_source_path(*testModel)), ")</div>");
      }
    }
    out.line("</TD></TR>");
  }
  out.line("</table>");
  out.line("</body>");
  out.line("</html>");
  return out.take();
}

std::string generate_summary(const ast::RequirementModel& model,
                             std::span<const ast::TestModel* const> testModels,
                             std::string_view filePath,
                             std::optional<std::string_view> destination) {
  const auto data = extract_destination_and_name(filePath, destination);
  std::filesystem::create_directories(data.destination);
  const auto outputPath =
      std::filesystem::path(data.destination) / (data.name + ".html");

  std::ofstream out(outputPath, std::ios::binary);
  if (!out.is_open()) {
    throw std::runtime_error("Unable to open output file: "
                             + outputPath.string());
  }
  out << generate_summary_file_html_content(model, testModels);
  return outputPath.string();
}

}  // namespace req
