#include "cursiv/shared/gen/AstEmitter.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <span>
#include <stdexcept>
#include <utility>

#include "cursiv/shared/gen/Concat.hpp"
#include "cursiv/shared/gen/EmitAstMetaData.hpp"
#include "cursiv/shared/gen/EmitTable.hpp"
#include "cursiv/shared/table/WriteKind.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

AstEmitter::AstEmitter(const AstRegistry& registry, const AstEntryTable& table,
                       std::string n, std::string h)
    : reg(registry), a(table), ns(std::move(n)), header(std::move(h)) {
  std::vector<char> pending(reg.fields.size(), 0);
  for (const AstRowGen& g : reg.rows) {
    if (g.fieldSlot != kNoField &&
        (g.writes() || g.kind == WriteKind::flag || g.kind == WriteKind::nest))
      pending[g.fieldSlot] = 1;
  }
  while (built < reg.types.size() && reg.types[built].built) ++built;
  accepted.resize(built);
  applierOf.resize(built);
  std::map<std::vector<std::uint16_t>, std::uint16_t> firstOf;
  for (std::size_t t = 0; t < built; ++t) {
    for (std::size_t f = 0; f < reg.fields.size(); ++f)
      if (pending[f] &&
          derives(reg.fields[f].owner, static_cast<std::uint16_t>(t)))
        accepted[t].push_back(static_cast<std::uint16_t>(f));
    applierOf[t] =
        firstOf.try_emplace(accepted[t], static_cast<std::uint16_t>(t))
            .first->second;
  }
  for (const auto& field : reg.fields) {
    const auto& owner = reg.types[field.owner];
    tags.push_back("::cursiv::detail::field_tag<&" + owner.source +
                   "::" + std::string(field.name) + ">");
  }
}

bool AstEmitter::fills(std::uint16_t cr, std::uint16_t r) const {
  if (!reg.rows[r].writes()) return false;
  return derives(reg.rows[r].declType, reg.rows[cr].declType) ||
         (reg.rows[cr].kind == WriteKind::close &&
          derives(reg.rows[cr].declType, reg.rows[r].declType));
}
int AstEmitter::made_row(const Run& l) const {
  int first = -1;
  for (const std::uint16_t r : l)
    if (reg.rows[r].makes()) {
      first = r;
      break;
    }
  if (first < 0 || reg.rows[first].kind != WriteKind::close) return first;
  for (const std::uint16_t r : l)
    if (reg.rows[r].opens() &&
        derives(reg.rows[first].declType, reg.rows[r].declType))
      return r;
  return first;
}
AstEmitter::Run AstEmitter::spare(std::uint16_t cr, const Run& l) const {
  Run keep;
  bool past = false;
  for (const std::uint16_t r : l) {
    if (past &&
        ((reg.rows[r].writes() && !fills(cr, r)) || reg.rows[r].opens()))
      keep.push_back(r);
    past = past || r == cr;
  }
  return keep;
}

const char* AstEmitter::act_name(ActKind k) {
  switch (k) {
    case ActKind::assign:
      return "::cursiv::detail::ActKind::assign";
    case ActKind::append:
      return "::cursiv::detail::ActKind::append";
    case ActKind::flag:
      return "::cursiv::detail::ActKind::flag";
    case ActKind::nest:
      return "::cursiv::detail::ActKind::nest";
    case ActKind::create:
      return "::cursiv::detail::ActKind::create";
    case ActKind::none:
      break;
  }
  throw std::logic_error("a deferred row names neither a write nor an effect");
}

void AstEmitter::plan_held() {
  // The registry keeps `convCount` below `kNoIndex`; a held slot takes the
  // next number, and none may reach it.
  auto next = static_cast<std::uint16_t>(reg.convCount);
  for (std::uint32_t e = 0; e < a.count; ++e) {
    Run held, over;
    a.rows(e, held, over);
    const auto* const value =
        plans.emplace_back(plan_conversions(held, over)).value;
    if (value == nullptr) continue;
    for (const Run* run : {&held, &over})
      for (const std::uint16_t r : *run) {
        const AstRowGen& g = reg.rows[r];
        if (g.writes() && g.convPasses && g.convSlot != kNoIndex &&
            heldSlots.try_emplace({g.convSlot, value->valueRule}, next)
                .second) {
          if (next == kNoIndex)
            throw std::runtime_error(
                "the grammar uses more than 65 535 conversion slots -- split "
                "the grammar");
          ++next;
        }
      }
  }
}

std::uint16_t AstEmitter::conv_of(std::uint16_t r) const {
  const AstRowGen& g = reg.rows[r];
  if (holder == nullptr || !g.convPasses) return g.convSlot;
  const auto at = heldSlots.find({g.convSlot, holder->valueRule});
  return at == heldSlots.end() ? g.convSlot : at->second;
}

void AstEmitter::emit_deferred(Body& b, std::uint16_t r, int m) const {
  const AstRowGen& g = reg.rows[r];
  if (g.writes() && m < 0)
    b.say({"b.defer_write(id, n, ", u(g.fieldSlot), ", ", u(conv_of(r)), ", ",
           u(g.narrowSlot), ", ", act_name(g.act()), ");"});
  else if (g.writes())
    b.say({"b.defer_write(id, ", u(g.fieldSlot), ", ", act_name(g.act()),
           ", m" + std::to_string(m) + ");"});
  else if (g.effects())
    // Only a nest's effect carries the class it builds.
    b.say({"b.defer_effect(id, ",
           u(g.kind == WriteKind::nest ? g.declType : kNoIndex), ", ",
           u(g.fieldSlot), ", ", act_name(g.act()), ");"});
}

bool AstEmitter::can_close_held(const Run& held, int cr, const Run& over,
                                const Run& added) const {
  if (held.empty() || added.empty() || !over.empty() || cr < 0) return false;
  if (reg.rows[cr].kind == WriteKind::close) {
    for (const std::uint16_t r : held)
      if (reg.rows[r].kind != WriteKind::close) return false;
  } else if (reg.rows[cr].kind == WriteKind::create) {
    unsigned creates = 0;
    for (const std::uint16_t r : held) {
      if (reg.rows[r].kind == WriteKind::create) {
        if (++creates > 1) return false;
      } else if (!reg.rows[r].converts() &&
                 !fills(static_cast<std::uint16_t>(cr), r)) {
        return false;
      }
    }
  } else {
    return false;
  }
  return std::ranges::all_of(added, [&](std::uint16_t r) {
    const AstRowGen& g = reg.rows[r];
    return g.kind == WriteKind::close &&
           derives(g.declType, reg.rows[cr].declType);
  });
}

void AstEmitter::check_base_chains() const {
  for (std::size_t e = 0; e < a.count; ++e) {
    const std::uint32_t i = a.chains(static_cast<std::uint32_t>(e)).front();
    const Run& held = a.runs[i];
    const int cr = made_row(held);
    if (cr < 0) continue;
    for (const std::uint16_t r : held) {
      if (r == cr) break;
      if (reg.rows[r].writes() && !fills(static_cast<std::uint16_t>(cr), r))
        throw std::logic_error("AST chain " + std::to_string(i) +
                               ": a write before the maker belongs to "
                               "another family");
    }
  }
}

std::string AstEmitter::write_pending(Body& b, std::uint16_t r) const {
  const AstRowGen& g = reg.rows[r];
  const std::string h = "h" + std::to_string(b.held++);
  const std::string lead = "const ::cursiv::engine::Pending " + h;
  if (g.convSlot != kNoIndex)
    b.say({lead + " = b.text_write(id, n, ", u(g.fieldSlot), ", ",
           u(conv_of(r)), ", ", u(g.narrowSlot), ", ", act_name(g.act()),
           ");"});
  else
    b.say({lead + " = b.node_write(id, n, ", u(g.fieldSlot), ", ",
           u(g.narrowSlot), ", ", act_name(g.act()), ");"});
  return h;
}

unsigned AstEmitter::fill_run(Body& b, std::uint16_t cr, const Run& held,
                              int result, bool read) const {
  std::vector<std::string> slots;
  for (const std::uint16_t r : held)
    if (fills(cr, r)) slots.push_back(write_pending(b, r));
  // A nest whose member holds the class made here wraps that node, so it is
  // queued before the creation.
  std::vector<char> early(held.size(), 0);
  for (std::size_t k = 0; k < held.size(); ++k) {
    const AstRowGen& g = reg.rows[held[k]];
    if (g.act() != ActKind::nest || g.fieldSlot == kNoField) continue;
    if (const std::uint16_t family = reg.head_of(reg.fields[g.fieldSlot]);
        family == kNoIndex || !derives(family, reg.rows[cr].declType))
      continue;
    early[k] = 1;
    emit_deferred(b, held[k], -1);
  }
  const unsigned m = b.made++;
  const std::string made = "m" + std::to_string(m);
  const AstRowGen& c = reg.rows[cr];
  const auto own_flag = [&](std::uint16_t r) {
    return reg.rows[r].kind == WriteKind::flag &&
           derives(reg.rows[r].declType, c.declType);
  };
  // The node is named only where a statement after its creation reads it.
  const std::string lead =
      read || !slots.empty() || std::ranges::any_of(held, own_flag)
          ? "const ::cursiv::engine::Made " + made + " = b."
          : "b.";
  const std::string type = u(c.declType);
  const std::string nest = reg.anyNest ? "true" : "false";
  // The node goes to the region above as the write `result` states, or as a
  // plain creation.
  const std::string field =
      u(result < 0 ? kNoField : reg.rows[result].fieldSlot);
  const char* const kind =
      act_name(result < 0 ? ActKind::create : reg.rows[result].act());
  if (c.kind == WriteKind::close)
    b.say({lead + "adopt<" + nest + ">(id, n, ", type, ", ", field, ", ", kind,
           ");"});
  else
    b.say({lead + "create<kMakes[", type, "], ", "&" + applier_name(c.declType),
           ", " + nest + ">(id, n, ", type, ", ", field, ", ", kind, ");"});
  std::size_t k = 0;
  for (const std::uint16_t r : held)
    if (fills(cr, r)) {
      // An adoption learns its node's class at runtime.
      const std::uint16_t f = reg.rows[r].fieldSlot;
      const bool checked = c.kind == WriteKind::close;
      b.say({"b.fill<", tags[f], ", ", conv_addr(conv_of(r)), ", ",
             checked ? "true" : "false", ">(", u(f),
             ", " + slots[k++] + ", " + made + ");"});
    }
  for (std::size_t j = 0; j < held.size(); ++j) {
    const std::uint16_t r = held[j];
    if (early[j]) continue;
    if (own_flag(r))
      b.say({"b.flag(id, ", u(reg.rows[r].fieldSlot), ", " + made + ");"});
    else if (reg.rows[r].effects())
      emit_deferred(b, r, -1);
  }
  return m;
}

void AstEmitter::run_held(Body& b, const Run& maker, int makerRow,
                          const Run& held, const Run& over, int cr) const {
  if (cr < 0) {
    for (const std::uint16_t r : held) emit_deferred(b, r, -1);
    for (const std::uint16_t r : over) emit_deferred(b, r, -1);
    return;
  }
  Run all = spare(static_cast<std::uint16_t>(cr), held);
  all.insert(all.end(), over.begin(), over.end());
  // The node's first write goes with its creation; the rest follow it.
  const auto writes = [&](std::uint16_t r) { return reg.rows[r].writes(); };
  const auto first = std::ranges::find_if(all, writes);
  const int result = first == all.end() ? -1 : *first;
  const bool closes = makerRow >= 0;
  const bool read = closes || std::ranges::count_if(all, writes) > 1;
  const unsigned m =
      closes ? fill_run(b, static_cast<std::uint16_t>(makerRow), maker, result,
                        read)
             : fill_run(b, static_cast<std::uint16_t>(cr), held, result, read);
  if (closes)
    b.say({"b.close_made(", u(reg.rows[cr].declType),
           ", id, m" + std::to_string(m) + ");"});
  for (auto at = all.begin(); at != all.end(); ++at)
    if (at != first) emit_deferred(b, *at, static_cast<int>(m));
}

AstEmitter::Body AstEmitter::entry_body(std::uint32_t e) const {
  Body b;
  const auto chains = a.chains(e);
  Run held = a.runs[chains.front()], over;
  // The maker a fused close waits on, with the run it fills.
  Run maker;
  int makerRow = -1;
  int heldRow = made_row(held);
  for (std::size_t k = 1; k < chains.size(); ++k) {
    const Run& added = a.runs[chains[k]];
    if (can_close_held(held, heldRow, over, added)) {
      if (makerRow >= 0 || !reg.anyNest) continue;
      // With a nest the close still checks the wrapper the maker may have
      // returned: the maker is written with the scope that closes it.
      maker = std::exchange(held, added);
      makerRow = std::exchange(heldRow, made_row(added));
      continue;
    }
    if (made_row(added) >= 0) {
      // A caller making a node closes the inner scope first, so a child of
      // the same type cannot take its writes.
      run_held(b, maker, makerRow, held, over, heldRow);
      held = added;
      heldRow = made_row(held);
      over.clear();
      makerRow = -1;
    } else {
      // Directives over one payload share its Made value.
      over.insert(over.end(), added.begin(), added.end());
    }
  }
  run_held(b, maker, makerRow, held, over, heldRow);
  return b;
}

void AstEmitter::emit_entries() {
  check_base_chains();
  std::map<std::string, std::string> seen;
  for (std::uint32_t e = 0; e < a.count; ++e) {
    holder = plans[e].value;
    const Body b = entry_body(e);
    const auto [found, fresh] =
        seen.try_emplace(b.text, "r" + std::to_string(seen.size()));
    names.push_back(found->second);
    if (!fresh) continue;
    if (b.text.empty())
      line("void ", found->second,
           "(::cursiv::engine::AstReducer&, ::cursiv::NodeId, const "
           "::cursiv::CstNode&) {}");
    else
      line("void ", found->second,
           "(::cursiv::engine::AstReducer& b, ::cursiv::NodeId id, "
           "[[maybe_unused]] const ::cursiv::CstNode& n) {\n",
           b.text, "}");
  }
}

void AstEmitter::emit_types() {
  write(emit_table(
      "constexpr ::cursiv::engine::MakeFn kMakes[]",
      std::span(reg.types).first(built),
      [](const auto& t) {
        return "&::cursiv::engine::make_node<" + t.source + ">";
      },
      "nullptr"));
}

std::string_view AstEmitter::emit_fields() {
  if (std::ranges::none_of(reg.rows, [](const AstRowGen& g) {
        return g.writes() && g.convSlot == kNoIndex;
      }))
    return {};
  line("constexpr ::std::uint64_t kTakeBits[] = {");
  for (const auto& field : reg.fields) {
    std::string row = "    ";
    for (std::size_t w = 0; w < reg.typeWords; ++w) {
      std::uint64_t bits = 0;
      for (const std::uint16_t t : reg.targets_of(field))
        bits |= reg.derives[t * reg.typeWords + w];
      row += concat(bits, "ull, ");
    }
    line(row);
  }
  line("};");
  return "kTakeBits";
}

std::string AstEmitter::conv_addr(std::uint16_t slot) {
  return slot == kNoIndex ? std::string("nullptr") : "&kConvs[" + u(slot) + "]";
}

std::string AstEmitter::applier_name(std::uint16_t t) const {
  return t < built && !accepted[t].empty() ? "a" + std::to_string(applierOf[t])
                                           : std::string("a_none");
}

void AstEmitter::emit_writers() {
  std::vector<char> used(reg.fields.size(), 0);
  for (std::size_t t = 0; t < built; ++t)
    for (const std::uint16_t f : accepted[t]) used[f] = 1;
  // Each member's conversions in the order rows first name them, and whether
  // a row of it converts nothing, gathered in one pass over the rows.
  std::vector<std::vector<std::uint16_t>> convsOf(reg.fields.size());
  std::vector<char> plainOf(reg.fields.size(), 0);
  for (const AstRowGen& g : reg.rows) {
    if (g.fieldSlot == kNoField) continue;
    auto& convs = convsOf[g.fieldSlot];
    if (!g.writes() || g.convSlot == kNoIndex)
      plainOf[g.fieldSlot] = 1;
    else if (std::ranges::find(convs, g.convSlot) == convs.end())
      convs.push_back(g.convSlot);
  }
  // A slot a passing write takes instead stands where the passing one does.
  for (auto& convs : convsOf)
    for (const auto& [held, slot] : heldSlots)
      if (std::ranges::find(convs, held.pass) != convs.end())
        convs.push_back(slot);
  for (std::size_t i = 0; i < reg.fields.size(); ++i) {
    const auto f = static_cast<std::uint16_t>(i);
    if (!used[f]) continue;
    const std::vector<std::uint16_t>& convs = convsOf[f];
    const bool plain = plainOf[f] != 0;
    const std::string& tag = tags[f];
    const std::string args = u(f) + ", p, m);";
    const auto write = [&](std::uint16_t slot) {
      return "b.write<" + tag + ", " + conv_addr(slot) + ">(" + args;
    };
    line(
        "CURSIV_ALWAYS_INLINE inline void w", f,
        "(::cursiv::engine::AstReducer& b, const ::cursiv::engine::Pending& p, "
        "const ::cursiv::engine::Made& m) {");
    if (convs.size() + (plain ? 1 : 0) == 1) {
      line("  ", write(plain ? kNoIndex : convs[0]));
    } else {
      line("  switch (p.conv) {");
      for (const std::uint16_t c : convs)
        line("    case ", u(c), ": ", write(c), " return;");
      line("    default: ", write(kNoIndex), " return;");
      line("  }");
    }
    line("}");
  }
  bool none = false;
  std::vector<std::string> appliers(built);
  for (std::size_t i = 0; i < built; ++i) {
    const auto t = static_cast<std::uint16_t>(i);
    appliers[t] = applier_name(t);
    if (appliers[t] == "a_none") {
      none = true;
      continue;
    }
    if (applierOf[t] != t) continue;
    line(
        "CURSIV_ALWAYS_INLINE inline bool ", appliers[t],
        "(::cursiv::engine::AstReducer& b, const ::cursiv::engine::Pending& p, "
        "const ::cursiv::engine::Made& m) {");
    line("  switch (p.field) {");
    for (const std::uint16_t f : accepted[t])
      line("    case ", u(f), ": w", f, "(b, p, m); return true;");
    line("    default: return false;");
    line("  }");
    line("}");
  }
  if (none)
    line(
        "bool a_none(::cursiv::engine::AstReducer&, const "
        "::cursiv::engine::Pending&, "
        "const ::cursiv::engine::Made&) { return false; }");
  write(emit_table(
      "constexpr ::cursiv::engine::AstApplyFn kApplies[]", appliers,
      [](const std::string& n) { return "&" + n; }, "nullptr"));
}

void AstEmitter::emit_convs() {
  if (reg.convCount == 0) return;
  std::vector<const AstRowGen*> rep(reg.convCount, nullptr);
  for (const AstRowGen& g : reg.rows)
    if (g.convSlot != kNoIndex && rep[g.convSlot] == nullptr)
      rep[g.convSlot] = &g;
  const auto site = [&](const AstRowGen& g) {
    return concat("::cursiv::engine::site_conv_t<", g.convName, ", ",
                  g.convOver.empty() ? std::string_view("void") : g.convOver,
                  ">, "
                  "::cursiv::engine::conv_target_t<",
                  tags[g.fieldSlot], ">, ", g.convRank, ", ", g.convJoins, ", ",
                  g.convTextParts);
  };
  std::vector<std::string> slots;
  for (const AstRowGen* g : rep)
    slots.push_back("::cursiv::engine::conv_slot<" + site(*g) + ">()");
  slots.resize(reg.convCount + heldSlots.size());
  for (const auto& [held, slot] : heldSlots)
    slots[slot] = concat("::cursiv::engine::held_slot<", held.holder, ", ",
                         site(*rep[held.pass]), ">()");
  write(emit_table("constexpr ::cursiv::engine::ConvSlot kConvs[]", slots));
}

AstEmitter::ConvPlan AstEmitter::plan_conversions(const Run& held,
                                               const Run& over) const {
  // The conversions a record carries, in the order its rules apply them: the
  // rows it is held by, then the one it is written over.
  std::vector<const AstRowGen*> chain;
  for (const auto* run : {&held, &over})
    for (const std::uint16_t r : *run)
      if (reg.rows[r].converts()) chain.push_back(&reg.rows[r]);
  ConvPlan p;
  if (chain.empty()) return p;
  p.data = chain.front()->dataRule;
  // What a site converts is the rule of the last row, so a payload's own
  // text stops before it.
  const std::span<const AstRowGen* const> all(chain);
  p.text = plan_text(chain.back()->dataRule ? all.first(all.size() - 1) : all,
                     p.data);
  p.call = plan_text(all, p.data);
  const auto last =
      std::ranges::find_if(chain.rbegin(), chain.rend(),
                           [](const AstRowGen* g) { return !g->passes; });
  if (last != chain.rend() && (*last)->valued) p.value = *last;
  return p;
}

AstEmitter::TextPlan AstEmitter::plan_text(
    std::span<const AstRowGen* const> chain, bool data) {
  TextPlan t;
  for (std::size_t i = 0; i < chain.size(); ++i) {
    if (chain[i]->textConv.empty()) continue;
    if (t.steps.empty()) t.lead = i == 0;
    t.steps.push_back(chain[i]);
  }
  if (t.steps.empty()) return t;
  // Only the first step reads the record, the others the text before them;
  // a converter that is handed nothing reads no child, whatever the record.
  t.children = data && t.steps.front()->textInput != TextInput::none;
  return t;
}

std::string AstEmitter::text_conv(const AstRowGen& g) {
  return g.textOver.empty()
             ? concat("::cursiv::detail::converter_rule_t<", g.textConv, ">")
             : concat("::cursiv::engine::site_conv_t<", g.textConv, ", ",
                      g.textOver, ">");
}

std::string AstEmitter::text_fn(const ConvPlan& p, const TextPlan& t) {
  if (t.steps.empty()) return "nullptr";
  if (t.steps.size() == 1 && t.lead)
    return concat("&::cursiv::engine::denoted_text<",
                  text_conv(*t.steps.front()), ", ", p.data, ">");
  std::string list;
  for (std::size_t i = 0; i < t.steps.size(); ++i)
    list += concat(", ::cursiv::engine::text_step<", text_conv(*t.steps[i]),
                   ", ", t.steps[i]->dataRule, ", ", i == 0 && t.lead, ">");
  return concat("&::cursiv::engine::denoted_chain<", p.data, list, ">");
}

AstEmitter::ConvTables AstEmitter::emit_conversions() {
  ConvTables named;
  // A grammar that converts no text names no table.
  if (std::ranges::any_of(plans, [](const ConvPlan& p) {
        return !p.text.steps.empty() || !p.call.steps.empty();
      })) {
    std::vector<std::string> texts, calls;
    std::vector<std::uint8_t> inputs;
    for (const ConvPlan& p : plans) {
      texts.push_back(text_fn(p, p.text));
      calls.push_back(text_fn(p, p.call));
      inputs.push_back(static_cast<std::uint8_t>(
          (p.text.children ? 1u : 0u) | (p.call.children ? 2u : 0u)));
    }
    write(emit_table("constexpr ::cursiv::engine::TextFn kTexts[]", texts));
    // One table answers both where every entry gives its parent the text its
    // payload denotes.
    named.texts = named.calls = "kTexts";
    if (calls != texts) {
      write(emit_table("constexpr ::cursiv::engine::TextFn kCalls[]", calls));
      named.calls = "kCalls";
    }
    // A grammar whose records never read their children first names no table.
    if (std::ranges::any_of(inputs, [](std::uint8_t c) { return c != 0; })) {
      write(emit_table("constexpr ::std::uint8_t kTextInputs[]", inputs,
                       [](std::uint8_t c) { return u(c); }));
      named.textInputs = "kTextInputs";
    }
  }
  // A call that read nothing differs from a token with no width only by its
  // record, and only a converter of parts sees the difference.
  std::vector<std::uint8_t> records;
  for (const ConvPlan& p : plans) records.push_back(p.data);
  if (std::ranges::any_of(reg.rows,
                          [](const AstRowGen& g) { return g.convParts; }))
    named.callRecords = emit_flags("kCallRecords", records);
  // A grammar whose data rules pass no value on names no table either.
  if (std::ranges::any_of(reg.rows,
                          [](const AstRowGen& g) { return g.passes; })) {
    write(emit_table("constexpr ::cursiv::engine::ValueSlot kValues[]", plans,
                     [](const ConvPlan& p) {
                       return p.value != nullptr
                                  ? concat("::cursiv::engine::value_slot<",
                                           p.value->valueRule, ">()")
                                  : std::string("{}");
                     }));
    named.values = "kValues";
  }
  return named;
}

std::string_view AstEmitter::emit_flags(
    std::string_view name, const std::vector<std::uint8_t>& cells) {
  if (std::ranges::none_of(cells, [](std::uint8_t c) { return c != 0; }))
    return {};
  std::string row;
  for (const std::uint8_t c : cells) row += c != 0 ? "1," : "0,";
  line("constexpr ::std::uint8_t ", std::string(name), "[] = {", row, "};");
  return name;
}

std::string AstEmitter::run() {
  // Angle brackets: quoted, it would find a generated sibling of its name.
  write(R"(// generated by cursiv gen -- do not edit
#include <)",
        header, R"(>
#include "AstReflection.hpp"

#include <cstdint>

#include "cursiv/shared/gen/GrammarBindings.hpp"
#include "cursiv/shared/runtime/AstConvThunks.hpp"
#include "cursiv/shared/runtime/AstReducer.hpp"
#include "cursiv/utils/macros.hpp"
#include "cursiv/syntax-tree/AstMemberAccess.hpp"
#include "cursiv/shared/runtime/AstMetaDataThunks.hpp"

namespace )",
        ns, R"( {
namespace ast_detail {
)");
  write(emit_ast_metadata(reg, tags));
  emit_types();
  const std::string_view takeBits = emit_fields();
  plan_held();
  emit_convs();
  line("");
  emit_writers();
  line("");
  emit_entries();
  line("");
  write(emit_table(
      "constexpr ::cursiv::engine::AstEntryFn kEntries[]", names,
      [](const std::string& n) { return "&" + n; }, "nullptr"));
  const ConvTables conv = emit_conversions();
  const std::string_view turns = emit_flags("kTurns", a.turns);
  line("\n}  // namespace ast_detail\n");
  write(emit_ast_metadata_record(reg));
  EmitFields f;
  const auto table = [&f](std::string_view field, std::string_view name) {
    f(field, name, std::string_view(), "ast_detail::");
  };
  f("meta", std::string_view("&kAstMetaData"), std::string_view());
  table("makes", "kMakes");
  table("entries", "kEntries");
  table("texts", conv.texts);
  table("calls", conv.calls);
  table("callRecords", conv.callRecords);
  f("entryCount", names.size(), std::size_t{0}, "", "u");
  table("applies", "kApplies");
  table("takeBits", takeBits);
  f("anyNest", reg.anyNest, false);
  f("turnsByState", a.turnsByState, false);
  table("turns", turns);
  table("textInputs", conv.textInputs);
  table("values", conv.values);
  line("constexpr ::cursiv::engine::AstActions kAstActions{", f.list(),
       "};\n\n}  // namespace ", ns);
  return take();
}

std::string emit_ast(const AstRegistry& registry, const AstEntryTable& table,
                     std::string_view ns, std::string_view header) {
  return AstEmitter(registry, table, std::string(ns), std::string(header))
      .run();
}

std::string emit_ast_decl(std::string_view ns) {
  return concat(R"(// generated by cursiv gen -- do not edit
#pragma once

#include "cursiv/syntax-tree/AstMetaData.hpp"
#include "cursiv/shared/runtime/AstActions.hpp"

namespace )",
                ns, R"( {
extern const ::cursiv::detail::AstMetaData kAstMetaData;
extern const ::cursiv::engine::AstActions kAstActions;
}
)");
}

}  // namespace cursiv::engine
