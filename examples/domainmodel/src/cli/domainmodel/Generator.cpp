#include "domainmodel/Generator.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/text/TextBuilder.hpp"
#include "domainmodel/CliUtils.hpp"

namespace dm {
namespace {

using namespace dm::ast;
using cursiv::TextBuilder;

[[nodiscard]] std::string upper_first(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  std::string value(text);
  value.front() = static_cast<char>(
      std::toupper(static_cast<unsigned char>(value.front())));
  return value;
}

[[nodiscard]] std::string package_path_from_file_path(
    std::string_view filePath) {
  std::string packagePath(filePath);
  std::ranges::replace(packagePath, '/', '.');
  while (!packagePath.empty() && packagePath.front() == '.') {
    packagePath.erase(packagePath.begin());
  }
  return packagePath;
}

[[nodiscard]] std::string resolved_type_name(const Type& type) {
  if (const auto* entity = cursiv::ast_cast<const Entity>(&type)) {
    return std::string(entity->name);
  }
  if (const auto* dataType = cursiv::ast_cast<const DataType>(&type)) {
    return std::string(dataType->name);
  }
  throw std::runtime_error("Unsupported domainmodel type.");
}

// The text the author wrote first, the resolved target second: a qualified
// name is what the Java source should say, and resolution normalises it away.
[[nodiscard]] std::string referenced_type_name(const Feature& feature) {
  if (!feature.type.refText.empty()) {
    return std::string(feature.type.refText);
  }
  if (const Type* const target = feature.type.get()) {
    return resolved_type_name(*target);
  }
  throw std::runtime_error("Cannot generate Java for unresolved feature type.");
}

// A `reference` written nowhere carries no text: that is how an entity with no
// `extends` is spelled, and it is not an error.
[[nodiscard]] std::string super_type_name(const Entity& entity) {
  if (!entity.superType.refText.empty()) {
    return std::string(entity.superType.refText);
  }
  if (const Entity* const target = entity.superType.get()) {
    return std::string(target->name);
  }
  return {};
}

[[nodiscard]] std::string java_type(const Feature& feature) {
  auto type = referenced_type_name(feature);
  if (feature.many) {
    type += "[]";
  }
  return type;
}

void write_accessors(TextBuilder& out, const Feature& feature) {
  const auto type = java_type(feature);
  const auto upperName = upper_first(feature.name);
  out.open("public void set", upperName, "(", type, " ", feature.name, ") {");
  out.line("this.", feature.name, " = ", feature.name, ";");
  out.close("}");
  out.line();
  out.open("public ", type, " get", upperName, "() {");
  out.line("return ", feature.name, ";");
  out.close("}");
}

[[nodiscard]] std::string generate_entity_file_content(
    const Entity& entity, std::string_view packagePath) {
  std::vector<const Feature*> features;
  features.reserve(entity.features.size());
  for (const auto& feature : entity.features) {
    if (feature) {
      features.push_back(feature);
    }
  }

  TextBuilder out;
  out.line("package ", packagePath, ";");
  out.line();
  out.write("class ", entity.name);
  if (const auto superType = super_type_name(entity); !superType.empty()) {
    out.write(" extends ", superType);
  }
  out.open(" {");
  for (const auto* feature : features) {
    out.line("private ", java_type(*feature), " ", feature->name, ";");
  }
  if (!features.empty()) {
    out.line();
  }
  for (std::size_t index = 0; index < features.size(); ++index) {
    if (index != 0) {
      out.line();
    }
    write_accessors(out, *features[index]);
  }
  out.close("}");
  return out.take();
}

void generate_abstract_elements(const std::filesystem::path& destination,
                                std::span<AbstractElement* const> elements,
                                const std::string& filePath) {
  const auto fullPath = destination / filePath;
  std::filesystem::create_directories(fullPath);

  const auto packagePath = package_path_from_file_path(filePath);
  for (const auto& element : elements) {
    if (const auto* package =
            cursiv::ast_cast<const PackageDeclaration>(element)) {
      // A dotted package becomes a directory path, as Java lays one out.
      std::string packagePath(package->name);
      std::ranges::replace(packagePath, '.', '/');
      generate_abstract_elements(
          destination, package->elements,
          (std::filesystem::path(filePath) / std::filesystem::path(packagePath))
              .string());
    } else if (const auto* entity = cursiv::ast_cast<const Entity>(element)) {
      const auto filePathValue =
          fullPath / (std::string(entity->name) + ".java");
      std::ofstream out(filePathValue, std::ios::binary);
      if (!out.is_open()) {
        throw std::runtime_error("Unable to open output file: "
                                 + filePathValue.string());
      }
      out << generate_entity_file_content(*entity, packagePath);
    }
  }
}

}  // namespace

std::string generate_java(const ast::DomainModel& model,
                          std::string_view filePath,
                          std::optional<std::string_view> destination) {
  const auto data = extract_destination_and_name(filePath, destination);
  std::filesystem::create_directories(data.destination);

  generate_abstract_elements(data.destination, model.elements, data.name);
  return (std::filesystem::path(data.destination) / data.name).string();
}

}  // namespace dm
