#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/ll_rescan.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/lr_rescan.hpp"
#include "cursiv/engines/shared/runtime/TypoCandidates.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/ReservedRefusedGlrGrammar.hpp"
#include "engines/ll/ReservedRefusedGrammar.hpp"
#include "engines/ll/ReservedRefusedPeekGrammar.hpp"
#include "refusedglrll/generated/Ll/Module.hpp"
#include "refusedll/generated/Ll/Module.hpp"
#include "refusedll/generated/Ll/Table.hpp"
#include "refusedlr/generated/Lr/Module.hpp"
#include "refusedlr/generated/Lr/Table.hpp"
#include "refusedpeekll/generated/Ll/Module.hpp"

namespace {

using Cases = std::span<const std::pair<std::string_view, std::string_view>>;
using Spell = std::string (*)(const cursiv::ParseResult&);

std::string plain_items(const cursiv::ParseResult& r) {
  const auto* d = r.value_as<tllrefused::Doc>();
  if (d == nullptr) return "<none>";
  std::string out;
  for (const tllrefused::Item* i : d->items)
    out += i->keyword ? "IN " : i->dash ? "- " : std::string(i->name) + ' ';
  return out;
}

std::string several_items(const cursiv::ParseResult& r) {
  const auto* d = r.value_as<tllrefusedglr::Doc>();
  if (d == nullptr) return "<none>";
  std::string out;
  for (const tllrefusedglr::Item* i : d->items)
    out += i->keyword ? "IN "
           : i->tag.empty() ? "n:" + std::string(i->name) + ' '
                            : "t:" + std::string(i->tag) + ' ';
  return out;
}

std::string peek_items(const cursiv::ParseResult& r) {
  const auto* d = r.value_as<tllrefusedpeek::Doc>();
  if (d == nullptr) return "<none>";
  std::string out;
  for (const tllrefusedpeek::Item* i : d->items)
    out += i->keyword ? "IN "
           : i->tag.empty() ? "n:" + std::string(i->name) + ' '
                            : "t:" + std::string(i->tag) + ' ';
  return out;
}

void check(const cursiv::Parser& ll, const cursiv::Parser* lr, Spell spell,
           Cases cases) {
  for (const auto& [text, expected] : cases) {
    const auto a = ll.parse(text);
    bool same = a.diagnostics.empty() && spell(a) == expected;
    if (lr) {
      const auto b = lr->parse(text);
      same = same && b.diagnostics.empty() && spell(b) == expected &&
          cursiv::test::records(a.cst()) == cursiv::test::records(b.cst());
    }
    if (!same)
      std::printf("`%.*s`: got `%s`, expected `%.*s`\n",
                  static_cast<int>(text.size()), text.data(), spell(a).c_str(),
                  static_cast<int>(expected.size()), expected.data());
    CHECK(same);
  }
}

// The re-readings of the text as the name its start owes: the state, or the
// point, that takes a name and the keyword beside it.
std::vector<cursiv::Typo> lr_names(std::string_view text) {
  namespace lr = tllrefused::generated::Lr;
  cursiv::Cst cst(text);
  cursiv::CstBuilder builder(cst);
  cursiv::LrContext c(text, builder);
  std::uint64_t read = 0;
  bool cut = false;
  return cursiv::lr_rescan_candidates<lr::kTable>(
      c, 0, 0, static_cast<cursiv::TextOffset>(text.size()), read, cut);
}

std::vector<cursiv::Typo> ll_names(std::string_view text) {
  namespace ll = tllrefused::generated::Ll;
  constexpr const auto& table = ll::kTable;
  cursiv::Cst cst(text);
  cursiv::CstBuilder builder(cst);
  cursiv::LlContext c(text, builder);
  const auto name =
      cursiv::ll_match_text<table>("inx", 3, table.errMode, 0).term;
  std::array<std::uint64_t, table.termWords> want{};
  want[name >> 6] |= std::uint64_t{1} << (name & 63u);
  std::uint64_t read = 0;
  cursiv::TypoCandidates found;
  cursiv::ll_rescan_candidates<table>(c, table.errMode, want.data(), 0, read,
                                      found);
  return found.ranked();
}

// A keyword stands, and its spelling is no name for a repair to join to what
// follows; refused where the word it refuses is spelled, it reserves nothing.
void a_refused_keyword_reserves_nothing_from_a_misspelling() {
  namespace lr = tllrefused::generated::Lr;
  const auto name =
      cursiv::lr_match_text<lr::kTable>("inx", 3, lr::kTable.mode[0], 0).term;
  const auto len = [](std::string_view text) {
    return static_cast<cursiv::TextOffset>(text.size());
  };
  const auto whole = [&](std::string_view text) {
    for (const cursiv::Typo& t : lr_names(text))
      if (t.term == name && t.end == len(text) && t.faults == 1) return true;
    return false;
  };
  CHECK(whole("in-place"));
  CHECK(whole("inx-place"));
  CHECK(!whole("in place"));
  namespace ll = tllrefused::generated::Ll;
  const auto llName =
      cursiv::ll_match_text<ll::kTable>("inx", 3, ll::kTable.errMode, 0).term;
  const auto ll_whole = [&](std::string_view text) {
    for (const cursiv::Typo& t : ll_names(text))
      if (t.term == llName && t.end == len(text) && t.faults == 1) return true;
    return false;
  };
  CHECK(ll_whole("in-place"));
  CHECK(ll_whole("inx-place"));
  CHECK(!ll_whole("in place"));
}

}  // namespace

// A keyword refused at a position is off it and reserves nothing there: the
// bytes read as a name, in either engine, and in a mode that reads several
// candidates.
int main() {
  a_refused_keyword_reserves_nothing_from_a_misspelling();
  constexpr std::pair<std::string_view, std::string_view> plain[] = {
      {"in x", "IN x "},
      {"in", "IN "},
      {"inx", "inx "},
      {"in place", "IN place "},
      {"in -place", "IN - place "},
      {"in-place", "in - place "},
      {"in-placex", "IN - placex "},
      {"x in-place in", "x in - place IN "},
  };
  check(*tllrefused::generated::Ll::create_parser(),
        tllrefused::generated::Lr::create_parser().get(), plain_items, plain);
  constexpr std::pair<std::string_view, std::string_view> peek[] = {
      {"in place.", "IN n:place "},
      {"in-place.", "t:in n:place "},
      {"in-place.in-place.", "t:in n:place t:in n:place "},
      {"inx-", "t:inx "},
      {"x- in-place.", "t:x t:in n:place "},
  };
  check(*tllrefusedpeek::generated::Ll::create_parser(),
        nullptr, peek_items, peek);
  constexpr std::pair<std::string_view, std::string_view> several[] = {
      {"in place.", "IN n:place "},
      {"in-place.", "t:in n:place "},
      {"in-place.in-place.", "t:in n:place t:in n:place "},
      {"inx.", "n:inx "},
      {"inx-", "t:inx "},
      {"x- in-place.", "t:x t:in n:place "},
  };
  check(*tllrefusedglr::generated::Ll::create_parser(),
        nullptr, several_items, several);
  return cursiv::test::report("LlReservedRefused_test");
}
