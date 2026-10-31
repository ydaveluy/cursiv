#include "cursiv/core/validation/DefaultCodeActionProtocol.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/text/TextRange.hpp"

namespace cursiv {

JsonValue make_action_row(std::string_view editKind, std::string title,
                          TextRange range, std::string newText,
                          std::uint64_t revision) {
  JsonValue::Object row;
  row.try_emplace(std::string(kKindField), kQuickFixKind);
  row.try_emplace(std::string(kEditKindField), editKind);
  row.try_emplace(std::string(kTitleField), std::move(title));
  row.try_emplace(std::string(kBeginField),
                  static_cast<std::int64_t>(range.begin));
  row.try_emplace(std::string(kEndField), static_cast<std::int64_t>(range.end));
  row.try_emplace(std::string(kNewTextField), std::move(newText));
  row.try_emplace(std::string(kRevisionField),
                  static_cast<std::int64_t>(revision));
  return JsonValue(std::move(row));
}

}  // namespace cursiv
