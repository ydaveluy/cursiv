#include "cursiv/lsp/actions/JsonBridge.hpp"

#include <lsp/json/json.h>
#include <lsp/types.h>

#include <cstdint>
#include <utility>

#include "cursiv/core/services/JsonValue.hpp"

namespace cursiv {

// The framework's integer is 32-bit on the wire; a payload value outside it
// travels as a decimal, which JSON cannot tell apart anyway.
lsp::LSPAny to_lsp_any(const JsonValue& value) {
  if (value.isBoolean()) return lsp::LSPAny(value.boolean());
  if (value.isInteger()) {
    const std::int64_t i = value.integer();
    if (i >= INT32_MIN && i <= INT32_MAX)
      return lsp::LSPAny(static_cast<lsp::json::Integer>(i));
    return lsp::LSPAny(static_cast<lsp::json::Decimal>(i));
  }
  if (value.isNumber()) return lsp::LSPAny(value.number());
  if (value.isString()) return lsp::LSPAny(lsp::json::String(value.string()));
  if (value.isArray()) {
    lsp::json::Array array;
    array.reserve(value.array().size());
    for (const JsonValue& element : value.array())
      array.push_back(to_lsp_any(element));
    return lsp::LSPAny(std::move(array));
  }
  if (value.isObject()) {
    lsp::json::Object object;
    for (const auto& [key, element] : value.object())
      object[key] = to_lsp_any(element);
    return lsp::LSPAny(std::move(object));
  }
  return lsp::LSPAny(lsp::json::Null{});
}

}  // namespace cursiv
