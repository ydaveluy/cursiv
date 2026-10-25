#include "cursiv/core/services/JsonValue.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace cursiv {
namespace {

void append_indent(std::string& out, std::size_t count) {
  out.append(count, ' ');
}

void append_escaped_json_string(std::string& out, std::string_view text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  out.push_back('"');
  for (const char ch : text) {
    const auto c = static_cast<unsigned char>(ch);
    switch (c) {
      case '\"':
        out += R"(\")";
        break;
      case '\\':
        out += R"(\\)";
        break;
      case '\b':
        out += R"(\b)";
        break;
      case '\f':
        out += R"(\f)";
        break;
      case '\n':
        out += R"(\n)";
        break;
      case '\r':
        out += R"(\r)";
        break;
      case '\t':
        out += R"(\t)";
        break;
      default:
        if (c < 0x20) {
          out += "\\u00";
          out.push_back(kHex[c >> 4]);
          out.push_back(kHex[c & 0x0F]);
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out.push_back('"');
}

std::string serialize_number(double value) {
  if (!std::isfinite(value)) return "null";
  std::ostringstream stream;
  // JSON requires a '.' separator and no digit grouping, so the global locale
  // must not reach the wire format.
  stream.imbue(std::locale::classic());
  stream.precision(std::numeric_limits<double>::max_digits10);
  stream << value;
  return stream.str();
}

void append_json_value(std::string& out, const JsonValue& value,
                       const JsonValue::SerializationOptions& options,
                       std::size_t indent);

void append_json_array(std::string& out, const JsonValue::Array& array,
                       const JsonValue::SerializationOptions& options,
                       std::size_t indent) {
  if (array.empty()) {
    out += "[]";
    return;
  }
  const std::size_t childIndent =
      options.pretty ? indent + options.indentSize : indent;
  out.push_back('[');
  for (std::size_t i = 0; i < array.size(); ++i) {
    if (i > 0) out.push_back(',');
    if (options.pretty) {
      out.push_back('\n');
      append_indent(out, childIndent);
    }
    append_json_value(out, array[i], options, childIndent);
  }
  if (options.pretty) {
    out.push_back('\n');
    append_indent(out, indent);
  }
  out.push_back(']');
}

// Keys sorted, so the same object always prints the same bytes: a pinned
// payload must not depend on hash order.
void append_json_object(std::string& out, const JsonValue::Object& object,
                        const JsonValue::SerializationOptions& options,
                        std::size_t indent) {
  if (object.empty()) {
    out += "{}";
    return;
  }
  std::vector<const JsonValue::Object::value_type*> rows;
  rows.reserve(object.size());
  for (const auto& row : object) rows.push_back(&row);
  std::ranges::sort(rows, {}, [](const auto* row) -> const std::string& {
    return row->first;
  });

  const std::size_t childIndent =
      options.pretty ? indent + options.indentSize : indent;
  out.push_back('{');
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i > 0) out.push_back(',');
    if (options.pretty) {
      out.push_back('\n');
      append_indent(out, childIndent);
    }
    append_escaped_json_string(out, rows[i]->first);
    out.push_back(':');
    if (options.pretty) out.push_back(' ');
    append_json_value(out, rows[i]->second, options, childIndent);
  }
  if (options.pretty) {
    out.push_back('\n');
    append_indent(out, indent);
  }
  out.push_back('}');
}

void append_json_value(std::string& out, const JsonValue& value,
                       const JsonValue::SerializationOptions& options,
                       std::size_t indent) {
  if (value.isNull()) {
    out += "null";
  } else if (value.isBoolean()) {
    out += value.boolean() ? "true" : "false";
  } else if (value.isInteger()) {
    out += std::to_string(value.integer());
  } else if (value.isString()) {
    append_escaped_json_string(out, value.string());
  } else if (value.isArray()) {
    append_json_array(out, value.array(), options, indent);
  } else if (value.isObject()) {
    append_json_object(out, value.object(), options, indent);
  } else {
    out += serialize_number(value.number());
  }
}

}  // namespace

JsonValue::JsonValue() noexcept : _value(nullptr) {}
JsonValue::JsonValue(std::nullptr_t) noexcept : _value(nullptr) {}
JsonValue::JsonValue(bool value) noexcept : _value(value) {}
JsonValue::JsonValue(int value) noexcept
    : _value(static_cast<std::int64_t>(value)) {}
JsonValue::JsonValue(std::int64_t value) noexcept : _value(value) {}
// A non-finite double has no JSON spelling; it lands as null rather than as
// bytes no parser accepts.
JsonValue::JsonValue(double value) noexcept
    : _value(std::isfinite(value) ? Storage(value) : Storage(nullptr)) {}
JsonValue::JsonValue(std::string value) noexcept : _value(std::move(value)) {}
JsonValue::JsonValue(std::string_view value) : _value(std::string(value)) {}
JsonValue::JsonValue(const char* value)
    : _value(value == nullptr ? Storage(nullptr)
                              : Storage(std::string(value))) {}
JsonValue::JsonValue(Array value) noexcept : _value(std::move(value)) {}
JsonValue::JsonValue(Object value) noexcept : _value(std::move(value)) {}

// The right-hand side is owned before its containing value can be replaced.
JsonValue& JsonValue::operator=(JsonValue value) noexcept {
  _value = std::move(value._value);
  return *this;
}

bool JsonValue::isNull() const noexcept {
  return std::holds_alternative<std::nullptr_t>(_value);
}
bool JsonValue::isBoolean() const noexcept {
  return std::holds_alternative<bool>(_value);
}
bool JsonValue::isInteger() const noexcept {
  return std::holds_alternative<std::int64_t>(_value);
}
bool JsonValue::isNumber() const noexcept {
  return isInteger() || std::holds_alternative<double>(_value);
}
bool JsonValue::isString() const noexcept {
  return std::holds_alternative<std::string>(_value);
}
bool JsonValue::isArray() const noexcept {
  return std::holds_alternative<Array>(_value);
}
bool JsonValue::isObject() const noexcept {
  return std::holds_alternative<Object>(_value);
}

bool JsonValue::boolean() const { return std::get<bool>(_value); }
std::int64_t JsonValue::integer() const {
  return std::get<std::int64_t>(_value);
}
double JsonValue::number() const {
  return isInteger() ? static_cast<double>(integer())
                     : std::get<double>(_value);
}
const std::string& JsonValue::string() const {
  return std::get<std::string>(_value);
}
const JsonValue::Array& JsonValue::array() const {
  return std::get<Array>(_value);
}
JsonValue::Array& JsonValue::array() { return std::get<Array>(_value); }
const JsonValue::Object& JsonValue::object() const {
  return std::get<Object>(_value);
}
JsonValue::Object& JsonValue::object() { return std::get<Object>(_value); }

std::string JsonValue::toJsonString() const {
  return toJsonString(SerializationOptions{});
}

std::string JsonValue::toJsonString(const SerializationOptions& options) const {
  std::string out;
  append_json_value(out, *this, options, 0);
  return out;
}

}  // namespace cursiv
