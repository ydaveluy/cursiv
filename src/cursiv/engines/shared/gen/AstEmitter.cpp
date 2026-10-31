#include "cursiv/engines/shared/gen/AstEmitter.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

#include "cursiv/engines/shared/gen/EmitAstMetaData.hpp"
#include "cursiv/engines/shared/gen/EmitOutput.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

AstEmitter::AstEmitter(const AstRegistry& registry, const AstEntryTable& table,
                       std::string n, std::string h)
    : reg(registry), a(table), ns(std::move(n)), header(std::move(h)) {
  std::vector<char> pending(reg.fields.size(), 0);
  for (const AstRowGen& g : reg.rows) {
    if (g.fieldSlot != kNoField &&
        (g.writes() || g.kind == WriteKind::flag || g.kind == WriteKind::nest))
      pending[g.fieldSlot] = 1;
    takesNodes = takesNodes || (g.writes() && g.convSlot == detail::kNoIndex);
  }
  while (built < reg.types.size() && reg.types[built].built) ++built;
  accepted.resize(built);
  for (std::size_t t = 0; t < built; ++t)
    for (std::size_t f = 0; f < reg.fields.size(); ++f)
      if (pending[f] &&
          derives(reg.fields[f].owner, static_cast<std::uint16_t>(t)))
        accepted[t].push_back(static_cast<std::uint16_t>(f));
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
      return "::cursiv::ActKind::assign";
    case ActKind::append:
      return "::cursiv::ActKind::append";
    case ActKind::flag:
      return "::cursiv::ActKind::flag";
    case ActKind::nest:
      return "::cursiv::ActKind::nest";
    case ActKind::create:
    case ActKind::none:
      break;
  }
  throw std::logic_error("an owed row names neither a write nor an effect");
}

void AstEmitter::plan_held() {
  // The registry keeps `convCount` below `kNoIndex`; a held slot takes the
  // next number, and none may reach it.
  auto next = static_cast<std::uint16_t>(reg.convCount);
  for (std::uint32_t e = 0; e < a.count; ++e) {
    const auto* const value = plans.emplace_back(plan_conversions(e)).value;
    if (value == nullptr) continue;
    Run held, over;
    a.rows(e, held, over);
    for (const Run* run : {&held, &over})
      for (const std::uint16_t r : *run) {
        const AstRowGen& g = reg.rows[r];
        if (g.writes() && g.convPasses && g.convSlot != detail::kNoIndex &&
            heldSlots.try_emplace({g.convSlot, value->valueRule}, next)
                .second) {
          if (next == detail::kNoIndex)
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

void AstEmitter::emit_owe(Body& b, std::uint16_t r, int m) const {
  const AstRowGen& g = reg.rows[r];
  if (g.writes() && m < 0)
    b.say({"b.owe_write(id, ", b.param(u(g.fieldSlot)), ", ",
           b.param(u(conv_of(r))), ", ", b.param(u(g.narrowSlot)), ", ",
           b.param(act_name(g.act())), ");"});
  else if (g.writes())
    b.say({"b.owe_write(id, ", b.param(u(g.fieldSlot)), ", ",
           b.param(act_name(g.act())), ", m" + std::to_string(m) + ");"});
  else if (g.effects())
    // Only a nest's effect carries the class it builds.
    b.say({"b.owe_effect(id, ",
           b.param(u(g.kind == WriteKind::nest ? g.declType
                                                : detail::kNoIndex)),
           ", ", b.param(u(g.fieldSlot)), ", ", b.param(act_name(g.act())),
           ");"});
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
    unsigned opens = 0;
    for (const std::uint16_t r : held) {
      if (!reg.rows[r].opens()) continue;
      ++opens;
      if (opens > 1)
        throw std::logic_error("AST chain " + std::to_string(i) +
                               ": more than one creation in a base run");
    }
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
  const std::string lead = "const ::cursiv::Pending " + h;
  if (g.convSlot != detail::kNoIndex)
    b.say({lead + " = b.text_write(id, ", b.param(u(g.fieldSlot)), ", ",
           b.param(u(conv_of(r))), ", ", b.param(u(g.narrowSlot)), ", ",
           b.param(act_name(g.act())), ");"});
  else
    b.say({lead + " = b.node_write(id, ", b.param(u(g.fieldSlot)), ", ",
           b.param(u(g.narrowSlot)), ", ", b.param(act_name(g.act())), ");"});
  return h;
}

unsigned AstEmitter::fill_run(Body& b, std::uint16_t cr,
                              const Run& held) const {
  std::vector<std::string> slots;
  for (const std::uint16_t r : held)
    if (fills(cr, r)) slots.push_back(write_pending(b, r));
  // A nest whose member holds the class made here wraps that node, so it is
  // owed before the creation.
  std::vector<char> early(held.size(), 0);
  for (std::size_t k = 0; k < held.size(); ++k) {
    const AstRowGen& g = reg.rows[held[k]];
    if (g.act() != ActKind::nest || g.fieldSlot == kNoField) continue;
    if (const std::uint16_t family = reg.head_of(reg.fields[g.fieldSlot]);
        family == detail::kNoIndex || !derives(family, reg.rows[cr].declType))
      continue;
    early[k] = 1;
    emit_owe(b, held[k], -1);
  }
  const unsigned m = b.made++;
  const std::string made = "m" + std::to_string(m);
  const AstRowGen& c = reg.rows[cr];
  const std::string lead = "const ::cursiv::Made " + made + " = b.";
  const auto type = b.param(u(c.declType));
  const std::string nest = reg.anyNest ? "true" : "false";
  if (c.kind == WriteKind::close)
    b.say({lead + "adopt<" + nest + ">(", type, ", id);"});
  else
    b.say({lead + "create<kMakes[", type, "], ",
           b.param("&" + applier_name(c.declType)), ", " + nest + ">(", type,
           ", id);"});
  b.guard();
  std::size_t k = 0;
  for (const std::uint16_t r : held)
    if (fills(cr, r)) {
      // An adoption learns its node's class at runtime.
      const std::uint16_t f = reg.rows[r].fieldSlot;
      const bool checked = c.kind == WriteKind::close;
      b.say({"b.fill<", b.param(tags[f]), ", ",
             b.param(conv_addr(conv_of(r))), ", ",
             b.param(variant_flag(f)), ", ",
             b.param(checked ? "true" : "false"), ">(", b.param(u(f)),
             ", " + slots[k++] + ", " + made + ");"});
    }
  for (std::size_t j = 0; j < held.size(); ++j) {
    const std::uint16_t r = held[j];
    if (early[j]) continue;
    if (reg.rows[r].kind == WriteKind::flag &&
        derives(reg.rows[r].declType, reg.rows[cr].declType))
      b.say({"b.flag(id, ", b.param(u(reg.rows[r].fieldSlot)),
             ", " + made + ");"});
    else if (reg.rows[r].effects())
      emit_owe(b, r, -1);
  }
  return m;
}

void AstEmitter::run_held(Body& b, const Run& held, const Run& over, int cr,
                          int heldMade) const {
  if (cr < 0) {
    for (const std::uint16_t r : held) emit_owe(b, r, -1);
    for (const std::uint16_t r : over) emit_owe(b, r, -1);
    return;
  }
  unsigned m;
  if (heldMade >= 0) {
    m = static_cast<unsigned>(heldMade);
    b.say({"b.close_made(", b.param(u(reg.rows[cr].declType)),
           ", id, m" + std::to_string(m) + ");"});
  } else {
    m = fill_run(b, static_cast<std::uint16_t>(cr), held);
  }
  b.guard();
  const Run rest = spare(static_cast<std::uint16_t>(cr), held);
  if (const int restMade = made_row(rest); restMade >= 0) {
    // Two creations are two nodes over one record.
    b.say("b.push_made(id, m" + std::to_string(m) + ");");
    run_held(b, rest, over, restMade);
    return;
  }
  Run all = rest;
  all.insert(all.end(), over.begin(), over.end());
  if (std::ranges::none_of(
          all, [&](std::uint16_t r) { return reg.rows[r].writes(); }))
    b.say("b.push_made(id, m" + std::to_string(m) + ");");
  for (const std::uint16_t r : all) emit_owe(b, r, static_cast<int>(m));
}

AstEmitter::Body AstEmitter::entry_body(std::uint32_t e) const {
  Body b;
  const auto chains = a.chains(e);
  Run held = a.runs[chains.front()], over;
  int heldMade = -1;
  int heldRow = made_row(held);
  for (std::size_t k = 1; k < chains.size(); ++k) {
    const Run& added = a.runs[chains[k]];
    if (can_close_held(held, heldRow, over, added)) {
      if (heldMade >= 0 || !reg.anyNest) continue;
      // The maker's Made stays unpublished until the next scope; with a
      // nest the close still checks the wrapper it may have returned.
      heldMade = static_cast<int>(
          fill_run(b, static_cast<std::uint16_t>(heldRow), held));
      b.guard();
      held = added;
      heldRow = made_row(held);
      continue;
    }
    if (made_row(added) >= 0) {
      // A caller making a node closes the inner scope first, so a child of
      // the same type cannot claim its writes.
      run_held(b, held, over, heldRow, heldMade);
      held = added;
      heldRow = made_row(held);
      over.clear();
      heldMade = -1;
    } else {
      // Directives over one payload share its Made value.
      over.insert(over.end(), added.begin(), added.end());
    }
  }
  run_held(b, held, over, heldRow, heldMade);
  return b;
}

void AstEmitter::emit_entries() {
  check_base_chains();
  std::vector<Body> programs;
  {
    // `seen` copies each distinct program, and is freed before the forms
    // are grouped.
    std::map<std::string, std::string> seen;
    for (std::uint32_t e = 0; e < a.count; ++e) {
      holder = plans[e].value;
      Body b = entry_body(e);
      const auto [found, fresh] =
          seen.try_emplace(b.text, "r" + std::to_string(programs.size()));
      if (fresh) programs.push_back(std::move(b));
      names.push_back(found->second);
    }
  }
  std::map<std::string_view, std::vector<std::size_t>> shapes;
  for (std::size_t i = 0; i < programs.size(); ++i)
    shapes[programs[i].form].push_back(i);
  std::map<std::string, std::string> aliases;
  const auto arg = [&](const std::string& v) {
    if (v.find(',') == std::string::npos) return v;
    const auto [at, fresh] =
        aliases.try_emplace(v, "FormArg" + std::to_string(aliases.size()));
    if (fresh) out += "using " + at->second + " = " + v + ";\n";
    return at->second;
  };
  unsigned forms = 0;
  for (std::size_t i = 0; i < programs.size(); ++i) {
    const auto& users = shapes[programs[i].form];
    if (users.front() != i || users.size() < 2) continue;
    const Body& f = programs[i];
    const std::string fn = "CURSIV_FORM_" + std::to_string(forms);
    std::string decl = "#define " + fn + "(";
    decl += join(std::views::iota(std::size_t{0}, f.args.size()), ", ",
                 [](std::size_t k) { return "C" + std::to_string(k); }) +
            ")";
    for (std::size_t at = 0, nl;
         (nl = f.form.find('\n', at)) != std::string::npos; at = nl + 1)
      decl += " \\\n" + f.form.substr(at, nl - at);
    decl += "\n";
    const auto call = [&](std::size_t p, const auto& spell) {
      return "  " + fn + "(" + join(programs[p].args, ", ", spell) + ")\n";
    };
    std::size_t before = 0, after = decl.size();
    for (const std::size_t p : users) {
      before += programs[p].text.size();
      after += call(p, [](const std::string& v) { return v; }).size();
    }
    if (after >= before) continue;
    for (const std::size_t p : users) programs[p].text = call(p, arg);
    out += decl;
    ++forms;
  }
  for (std::size_t i = 0; i < programs.size(); ++i)
    out += programs[i].text.empty()
               ? "void r" + std::to_string(i) +
                     "(::cursiv::AstReducer&, ::cursiv::NodeId) {}\n"
               : "void r" + std::to_string(i) +
                     "(::cursiv::AstReducer& b, ::cursiv::NodeId id) {\n" +
                     programs[i].text + "}\n";
  for (unsigned i = 0; i < forms; ++i)
    out += "#undef CURSIV_FORM_" + std::to_string(i) + "\n";
}

void AstEmitter::emit_types() {
  out += emit_table(
      "constexpr ::cursiv::detail::MakeFn kMakes[]",
      std::span(reg.types).first(built),
      [](const auto& t) {
        return "&::cursiv::detail::make_node<" + t.source + ">";
      },
      "nullptr");
}

void AstEmitter::emit_fields() {
  if (!takesNodes) return;
  line("constexpr ::std::uint64_t kTakeBits[] = {");
  for (const auto& field : reg.fields) {
    std::string row = "    ";
    for (std::size_t w = 0; w < reg.typeWords; ++w) {
      std::uint64_t bits = 0;
      for (const std::uint16_t t : reg.targets_of(field))
        bits |= reg.derives[t * reg.typeWords + w];
      row += std::format("{}ull, ", bits);
    }
    line(row);
  }
  line("};");
}

std::string AstEmitter::conv_addr(std::uint16_t slot) {
  return slot == detail::kNoIndex ? std::string("nullptr")
                                  : "&kConvs[" + u(slot) + "]";
}
const char* AstEmitter::variant_flag(std::uint16_t f) const {
  return reg.fields[f].nature == detail::FieldNature::variant ? "true"
                                                               : "false";
}

std::string AstEmitter::applier_name(std::uint16_t t) const {
  return t < built && !accepted[t].empty() ? "a" + std::to_string(t)
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
    if (!g.writes() || g.convSlot == detail::kNoIndex)
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
    line("CURSIV_ALWAYS_INLINE inline void w" + std::to_string(f) +
         "(::cursiv::AstReducer& b, const ::cursiv::Pending& p, "
         "const ::cursiv::Made& m) {");
    if (convs.size() + (plain ? 1 : 0) == 1) {
      line("  b.write<" + tag + ", " +
           conv_addr(plain ? detail::kNoIndex : convs[0]) + ", " +
           variant_flag(f) + ">(" + args);
    } else {
      line("  switch (p.conv) {");
      for (const std::uint16_t c : convs)
        line("    case " + u(c) + ": b.write<" + tag + ", &kConvs[" + u(c) +
             "], " + variant_flag(f) + ">(" + args + " return;");
      line("    default: b.write<" + tag + ", nullptr, " + variant_flag(f) +
           ">(" + args + " return;");
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
    line("CURSIV_ALWAYS_INLINE inline bool " + appliers[t] +
         "(::cursiv::AstReducer& b, const ::cursiv::Pending& p, "
         "const ::cursiv::Made& m) {");
    line("  switch (p.field) {");
    for (const std::uint16_t f : accepted[t])
      line("    case " + u(f) + ": w" + std::to_string(f) +
           "(b, p, m); return true;");
    line("    default: return false;");
    line("  }");
    line("}");
  }
  if (none)
    line(
        "bool a_none(::cursiv::AstReducer&, const ::cursiv::Pending&, "
        "const ::cursiv::Made&) { return false; }");
  out += emit_table(
      "constexpr ::cursiv::AstApplyFn kApplies[]", appliers,
      [](const std::string& n) { return "&" + n; }, "nullptr");
}

void AstEmitter::emit_convs() {
  if (reg.convCount == 0) return;
  std::vector<const AstRowGen*> rep(reg.convCount, nullptr);
  for (const AstRowGen& g : reg.rows)
    if (g.convSlot != detail::kNoIndex && rep[g.convSlot] == nullptr)
      rep[g.convSlot] = &g;
  const auto site = [&](const AstRowGen& g) {
    return std::format(
        "::cursiv::detail::site_conv_t<{}, {}>, "
        "::cursiv::detail::conv_target_t<{}>, {}, {}, {}",
        g.convName, g.convOver.empty() ? std::string_view("void") : g.convOver,
        tags[g.fieldSlot], g.convRank, g.convJoins, g.convTextParts);
  };
  std::vector<std::string> slots;
  for (const AstRowGen* g : rep)
    slots.push_back("::cursiv::detail::conv_slot<" + site(*g) + ">()");
  slots.resize(reg.convCount + heldSlots.size());
  for (const auto& [held, slot] : heldSlots)
    slots[slot] = std::format("::cursiv::detail::held_slot<{}, {}>()",
                              held.holder, site(*rep[held.pass]));
  out += emit_table("constexpr ::cursiv::detail::ConvSlot kConvs[]", slots);
}

AstEmitter::ConvPlan AstEmitter::plan_conversions(std::uint32_t e) const {
  Run held, over;
  a.rows(e, held, over);
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
  t.input = t.steps.front()->textInput;
  t.children = data && t.input != TextInput::none;
  return t;
}

std::string AstEmitter::text_conv(const AstRowGen& g) {
  return g.textOver.empty()
             ? std::format("::cursiv::detail::converter_rule_t<{}>",
                           g.textConv)
             : std::format("::cursiv::detail::site_conv_t<{}, {}>", g.textConv,
                           g.textOver);
}

std::string AstEmitter::text_fn(const ConvPlan& p, const TextPlan& t) {
  if (t.steps.empty()) return "nullptr";
  if (t.steps.size() == 1 && t.lead)
    return std::format("&::cursiv::detail::denoted_text<{}, {}>",
                       text_conv(*t.steps.front()), p.data);
  std::string list;
  for (std::size_t i = 0; i < t.steps.size(); ++i)
    list += std::format(", ::cursiv::detail::text_step<{}, {}, {}>",
                        text_conv(*t.steps[i]), t.steps[i]->dataRule,
                        i == 0 && t.lead);
  return std::format("&::cursiv::detail::denoted_chain<{}{}>", p.data, list);
}

void AstEmitter::emit_conversions() {
  anyText = std::ranges::any_of(plans, [](const ConvPlan& p) {
    return !p.text.steps.empty() || !p.call.steps.empty();
  });
  // A grammar that converts no text names no table.
  if (anyText) {
    std::vector<std::string> texts, calls;
    std::vector<std::uint8_t> inputs;
    for (const ConvPlan& p : plans) {
      texts.push_back(text_fn(p, p.text));
      calls.push_back(text_fn(p, p.call));
      inputs.push_back(static_cast<std::uint8_t>(
          (p.text.children ? 1u : 0u) | (p.call.children ? 2u : 0u)));
    }
    out += emit_table("constexpr ::cursiv::detail::TextFn kTexts[]", texts);
    sharedCalls = calls == texts;
    if (!sharedCalls)
      out += emit_table("constexpr ::cursiv::detail::TextFn kCalls[]", calls);
    // A grammar whose records never read their children first names no table.
    anyInputs =
        std::ranges::any_of(inputs, [](std::uint8_t c) { return c != 0; });
    if (anyInputs)
      out += emit_table("constexpr ::std::uint8_t kTextInputs[]", inputs,
                        [](std::uint8_t c) { return u(c); });
  }
  // A call that read nothing is told from a token laid with no width only by
  // its record, and only a converter of parts counts the difference.
  std::vector<std::uint8_t> records;
  for (const ConvPlan& p : plans) records.push_back(p.data);
  anyCalls = std::ranges::any_of(
                 reg.rows, [](const AstRowGen& g) { return g.convParts; }) &&
             emit_flags("kCallRecords", records);
  // A grammar whose data rules pass no value on names no table either.
  anyValues = std::ranges::any_of(
      reg.rows, [](const AstRowGen& g) { return g.passes; });
  if (anyValues)
    out += emit_table(
        "constexpr ::cursiv::detail::ValueSlot kValues[]", plans,
        [](const ConvPlan& p) {
          return p.value != nullptr
                     ? std::format("::cursiv::detail::value_slot<{}>()",
                                   p.value->valueRule)
                     : std::string("{}");
        });
}

bool AstEmitter::emit_flags(std::string_view name,
                            const std::vector<std::uint8_t>& cells) {
  if (std::ranges::none_of(cells, [](std::uint8_t c) { return c != 0; }))
    return false;
  std::string row;
  for (const std::uint8_t c : cells) row += c != 0 ? "1," : "0,";
  line("constexpr ::std::uint8_t " + std::string(name) + "[] = {" + row + "};");
  return true;
}

std::string AstEmitter::run() {
  // Angle brackets: quoted, it would find a generated sibling of its name.
  out += std::format(R"(// generated by cursiv gen -- do not edit
#include <{}>
#include "AstReflection.hpp"

#include <cstdint>

#include "cursiv/engines/shared/gen/GrammarBindings.hpp"
#include "cursiv/engines/shared/runtime/AstConvThunks.hpp"
#include "cursiv/engines/shared/runtime/AstReducer.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/core/syntax-tree/AstMemberAccess.hpp"
#include "cursiv/engines/shared/runtime/AstMetaDataThunks.hpp"

namespace {} {{
namespace ast_detail {{
)",
                     header, ns);
  out += emit_ast_metadata(reg, tags);
  emit_types();
  emit_fields();
  plan_held();
  emit_convs();
  line("");
  emit_writers();
  line("");
  emit_entries();
  line("");
  out += emit_table(
      "constexpr ::cursiv::AstEntryFn kEntries[]", names,
      [](const std::string& n) { return "&" + n; }, "nullptr");
  emit_conversions();
  const bool turns = emit_flags("kTurns", a.turns);
  out += "\n}  // namespace ast_detail\n\n";
  out += emit_ast_metadata_record(reg);
  out += std::format(R"(constexpr ::cursiv::AstActions kAstActions{{
    .meta = &kAstMetaData,
    .makes = ast_detail::kMakes,
    .entries = ast_detail::kEntries,
    .texts = {3},
    .calls = {7},
    .callRecords = {8},
    .entryCount = {1}u,
    .applies = ast_detail::kApplies,
    .takeBits = {4},
    .anyNest = {2},
    .turnsByState = {5},
    .turns = {6},
    .textInputs = {9},
    .values = {10},
}};

}}  // namespace {0}
)",
                     ns, names.size(), reg.anyNest,
                     anyText ? "ast_detail::kTexts" : "nullptr",
                     takesNodes ? "ast_detail::kTakeBits" : "nullptr",
                     a.turnsByState, turns ? "ast_detail::kTurns" : "nullptr",
                     !anyText      ? "nullptr"
                     : sharedCalls ? "ast_detail::kTexts"
                                   : "ast_detail::kCalls",
                     anyCalls ? "ast_detail::kCallRecords" : "nullptr",
                     anyInputs ? "ast_detail::kTextInputs" : "nullptr",
                     anyValues ? "ast_detail::kValues" : "nullptr");
  return out;
}

std::string emit_ast(const AstRegistry& registry, const AstEntryTable& table,
                     std::string_view ns, std::string_view header) {
  return AstEmitter(registry, table, std::string(ns), std::string(header))
      .run();
}

std::string emit_ast_decl(std::string_view ns) {
  return std::format(R"(// generated by cursiv gen -- do not edit
#pragma once

#include "cursiv/core/syntax-tree/AstMetaData.hpp"
#include "cursiv/engines/shared/runtime/AstActions.hpp"

namespace {} {{
extern const ::cursiv::AstMetaData kAstMetaData;
extern const ::cursiv::AstActions kAstActions;
}}
)",
                     ns);
}

}  // namespace cursiv
