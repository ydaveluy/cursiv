#include "cursiv/engines/lr/gen/lr_completion_sites.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>
#include <tuple>

namespace cursiv {

std::vector<RecordGen> lr_record_gens(const AstEntryTable& table,
                                      std::span<const AstRowGen> rows) {
  std::vector<RecordGen> records(table.count);
  std::vector<std::uint16_t> held, over;
  for (std::size_t e = 0; e < records.size(); ++e) {
    table.rows(static_cast<std::uint32_t>(e), held, over);
    records[e] = record_gen(rows, held, over);
  }
  return records;
}

std::vector<ElementId> lr_completion_sites(
    const std::vector<RecordGen>& records) {
  using Key = std::tuple<std::string_view, std::uint16_t, std::uint8_t>;
  std::map<Key, ElementId> canonical;
  std::vector result(records.size(), kNoElement);
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
