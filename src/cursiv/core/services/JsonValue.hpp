#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

// An owned JSON value for the one core field that is JSON by contract,
// `Diagnostic::data` -- a core service attaches a machine payload with no
// protocol framework in the room. `toJsonString` is deterministic (sorted
// keys, classic-locale numbers), so a payload can be pinned and diffed.

#include "cursiv/core/utils/TransparentStringHash.hpp"

namespace cursiv {

class JsonValue {
 public:
  struct SerializationOptions {
    bool pretty = true;
    std::size_t indentSize = 2;
  };

  using Array = std::vector<JsonValue>;
  using Object = std::unordered_map<std::string, JsonValue,
                                    TransparentStringHash, std::equal_to<>>;
  using Storage = std::variant<std::nullptr_t, bool, std::int64_t, double,
                               std::string, Array, Object>;

  JsonValue() noexcept;
  explicit(false) JsonValue(std::nullptr_t) noexcept;
  explicit(false) JsonValue(bool value) noexcept;
  explicit(false) JsonValue(int value) noexcept;
  explicit(false) JsonValue(std::int64_t value) noexcept;
  explicit(false) JsonValue(double value) noexcept;
  explicit(false) JsonValue(std::string value) noexcept;
  explicit(false) JsonValue(std::string_view value);
  explicit(false) JsonValue(const char* value);
  explicit(false) JsonValue(Array value) noexcept;
  explicit(false) JsonValue(Object value) noexcept;
  JsonValue(const JsonValue&) = default;
  JsonValue(JsonValue&&) noexcept = default;
  JsonValue& operator=(JsonValue value) noexcept;

  [[nodiscard]] bool isNull() const noexcept;
  [[nodiscard]] bool isBoolean() const noexcept;
  [[nodiscard]] bool isInteger() const noexcept;
  [[nodiscard]] bool isNumber() const noexcept;
  [[nodiscard]] bool isString() const noexcept;
  [[nodiscard]] bool isArray() const noexcept;
  [[nodiscard]] bool isObject() const noexcept;

  // Precondition on each: the value holds that alternative.
  [[nodiscard]] bool boolean() const;
  [[nodiscard]] std::int64_t integer() const;
  [[nodiscard]] double number() const;
  [[nodiscard]] const std::string& string() const;
  [[nodiscard]] const Array& array() const;
  [[nodiscard]] Array& array();
  [[nodiscard]] const Object& object() const;
  [[nodiscard]] Object& object();

  [[nodiscard]] std::string toJsonString() const;
  [[nodiscard]] std::string toJsonString(const SerializationOptions&) const;

 private:
  Storage _value;
};

}  // namespace cursiv
