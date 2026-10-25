#include "cursiv/engines/lr/gen/lr_completion_sites.hpp"

#include <cstdint>
#include <map>
#include <string_view>
#include <tuple>

namespace cursiv {

std::vector<RecordGen> lr_record_gens(const AstEntryTable& table,
                                      std::span<const AstRowGen> rows) {
  return record_gens(rows, table.count,
                     [&](std::uint16_t e, std::vector<std::uint16_t>& held,
                         std::vector<std::uint16_t>& over) {
                       table.rows(e, held, over);
                     });
}

std::vector<ElementId> lr_completion_sites(
    const std::vector<RecordGen>& records) {
  using Key = std::tuple<std::string_view, std::uint16_t, std::uint8_t>;
  std::map<Key, ElementId> canonical;
  std::vector<ElementId> result(records.size(), kNoElement);
  for (unsigned i = 0; i < records.size(); ++i) {
    const RecordGen& r = records[i];
    if (r.feature.empty()) continue;
    const Key key{r.feature, r.typeSlot, r.narrow};
    result[i] =
        canonical.try_emplace(key, static_cast<ElementId>(i)).first->second;
  }
  return result;
}

}  // namespace cursiv
