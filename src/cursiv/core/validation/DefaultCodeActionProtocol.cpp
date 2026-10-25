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
  row.emplace(std::string(kKindField), JsonValue(kQuickFixKind));
  row.emplace(std::string(kEditKindField), JsonValue(editKind));
  row.emplace(std::string(kTitleField), JsonValue(std::move(title)));
  row.emplace(std::string(kBeginField),
              JsonValue(static_cast<std::int64_t>(range.begin)));
  row.emplace(std::string(kEndField),
              JsonValue(static_cast<std::int64_t>(range.end)));
  row.emplace(std::string(kNewTextField), JsonValue(std::move(newText)));
  row.emplace(std::string(kRevisionField),
              JsonValue(static_cast<std::int64_t>(revision)));
  return JsonValue(std::move(row));
}

}  // namespace cursiv
