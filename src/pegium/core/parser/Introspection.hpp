#pragma once

/// Compile-time and runtime type-name helpers used by parser metadata.

#include <cstdlib>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>

#if defined(__clang__) || defined(__GNUC__)
#include <cxxabi.h>
#endif

namespace pegium::parser::detail {

template <auto... Values>
[[nodiscard]] constexpr auto function_name() noexcept -> std::string_view {
  return std::source_location::current().function_name();
}

template <auto MemberPointer>
struct MemberNameExtractor {
  struct REF_STRUCT {
    int MEMBER;

    static constexpr auto name = function_name<&REF_STRUCT::MEMBER>();
    static constexpr auto end_marker =
        name.substr(name.find("REF_STRUCT::MEMBER") +
                    std::string_view{"REF_STRUCT::MEMBER"}.size());
  };

  static consteval std::string_view value() noexcept {
    std::string_view func_name = function_name<MemberPointer>();
    if (const auto marker_pos = func_name.rfind(REF_STRUCT::end_marker);
        marker_pos != std::string_view::npos) {
      func_name = func_name.substr(0, marker_pos);
    }

    const auto sep = func_name.rfind("::");
    if (sep == std::string_view::npos) {
      return func_name;
    }

    return func_name.substr(sep + 2);
  }
};

template <auto MemberPointer>
consteval std::string_view member_name() noexcept {
  return MemberNameExtractor<MemberPointer>::value();
}

template <auto MemberPointer>
inline constexpr std::string_view member_name_v = member_name<MemberPointer>();

constexpr std::string_view simplifyTypeName(std::string_view typeName) noexcept {
  if (constexpr std::string_view classPrefix = "class ";
      typeName.rfind(classPrefix, 0) == 0) {
    typeName.remove_prefix(classPrefix.size());
  }

  if (constexpr std::string_view structPrefix = "struct ";
      typeName.rfind(structPrefix, 0) == 0) {
    typeName.remove_prefix(structPrefix.size());
  }

  if (const auto nsSeparator = typeName.rfind("::");
      nsSeparator != std::string_view::npos) {
    typeName.remove_prefix(nsSeparator + 2);
  }

  return typeName;
}

template <typename Type>
consteval std::string_view computeTypeName() noexcept {
#if defined(__clang__) || defined(__GNUC__)
  constexpr std::string_view functionName = __PRETTY_FUNCTION__;
  constexpr std::string_view marker = "Type = ";
  auto typeBegin = functionName.find(marker);
  if (typeBegin == std::string_view::npos) {
    return simplifyTypeName(functionName);
  }

  typeBegin += marker.size();
  auto typeEnd = functionName.find(';', typeBegin);
  if (typeEnd == std::string_view::npos) {
    typeEnd = functionName.find(']', typeBegin);
  }
  if (typeEnd == std::string_view::npos) {
    typeEnd = functionName.size();
  }

  return simplifyTypeName(functionName.substr(typeBegin, typeEnd - typeBegin));
#elif defined(_MSC_VER)
  constexpr std::string_view functionName = __FUNCSIG__;
  constexpr std::string_view marker = "computeTypeName<";
  auto typeBegin = functionName.find(marker);
  if (typeBegin == std::string_view::npos) {
    return simplifyTypeName(functionName);
  }

  typeBegin += marker.size();
  auto typeEnd = functionName.find(">(void)", typeBegin);
  if (typeEnd == std::string_view::npos) {
    typeEnd = functionName.find('>', typeBegin);
  }
  if (typeEnd == std::string_view::npos) {
    typeEnd = functionName.size();
  }

  return simplifyTypeName(functionName.substr(typeBegin, typeEnd - typeBegin));
#else
  return "Unknown";
#endif
}

template <typename Type>
inline constexpr std::string_view type_name_v = computeTypeName<Type>();

[[nodiscard]] inline std::string demangleTypeName(
    std::string_view typeName) noexcept {
#if defined(__clang__) || defined(__GNUC__)
  int status = 0;
  const auto input = std::string(typeName);
  constexpr auto freeDeleter = [](char *ptr) { std::free(ptr); };
  if (std::unique_ptr<char, decltype(freeDeleter)> demangled(
          abi::__cxa_demangle(input.c_str(), nullptr, nullptr, &status),
          freeDeleter);
      status == 0 && demangled) {
    return std::string(simplifyTypeName(demangled.get()));
  }
#endif
  return std::string(simplifyTypeName(typeName));
}

[[nodiscard]] inline std::string runtime_type_name(
    const std::type_info &type) noexcept {
  return demangleTypeName(type.name());
}

[[nodiscard]] inline std::string runtime_type_name(
    std::type_index type) noexcept {
  return demangleTypeName(type.name());
}

} // namespace pegium::parser::detail
