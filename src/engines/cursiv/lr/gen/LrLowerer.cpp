#include "cursiv/lr/gen/LrLowerer.hpp"

#include <algorithm>
#include <span>
#include <stdexcept>

#include "cursiv/lr/gen/lr_intern_name.hpp"
#include "cursiv/lr/gen/lr_productions.hpp"
#include "cursiv/lr/gen/lr_lower_passes.hpp"
#include "cursiv/shared/table/Symbol.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

std::uint16_t LrLowerer::intern_name(std::string_view text) {
  return lr_intern_name(_cfg, text);
}

void LrLowerer::declare_start() {
  _cfg.nnts = 1;
  name_row(_cfg.nts[0], "$accept");
}

void LrLowerer::declare_rules() {
  const std::uint16_t first = _cfg.nnts;
  name_rules(_cfg.nts, _cfg.nnts);
  for (std::uint16_t n = first; n < _cfg.nnts; ++n)
    _cfg.nts[n].node = stage(_ntRule[(n - first) % _ntRule.size()]).kind !=
                       ImageRuleKind::data;
}

Descent<ImageId> LrLowerer::ended(ImageId id) {
  if (const auto it = _ended.find(id); it != _ended.end()) co_return it->second;
  const ImageNode n = at(id);
  ImageId out = id;
  if (n.kind == ImageKind::eof || (n.kind == ImageKind::andOf &&
                                   at(child_at(n, 0)).kind == ImageKind::eof)) {
    out = section_end();
  } else if (n.kind == ImageKind::implied) {
    const ImageId tok = child_at(n, 0);
    const ImageId cond = co_await ended(child_at(n, 1));
    if (cond != child_at(n, 1)) {
      const ImageId both[] = {tok, cond};
      out = _im.derive(n, both);
    }
  } else {
    switch (n.kind) {
      case ImageKind::lit:
      case ImageKind::gapHas:
      case ImageKind::gapIs:
        break;
      default:
        out = co_await map_children(id, [&](ImageId k) { return ended(k); });
    }
  }
  _ended.emplace(id, out);
  co_return out;
}

LrLowered LrLowerer::run() {
  index();
  _o.optionalGate = _d.optionalGate;
  _lex.declare_word(_d.bound);
  _p.rowKind = _rowKind;
  const std::uint32_t entry = _im.rule(*_d.entry);
  _cfg.entryNt = static_cast<std::uint16_t>(1 + _ntIdx.at(entry));
  declare();
  define();
  // An end assertion also reduces on a dedent, so the input's end guards it: a
  // repair invents dedents inside the text.
  if (std::ranges::any_of(
          std::span(_cfg.terms.data(), _cfg.nterms),
          [](const TermInfo& t) { return t.synthetic == kSyntheticDedent; })) {
    std::uint8_t end = 0;
    for (std::uint16_t q = 0; q < _cfg.nprods; ++q) {
      if (!_cfg.prods[q].endOnly) continue;
      if (end == 0)
        end = static_cast<std::uint8_t>(gap_cond(0, _lex.make(ImageKind::eof)) +
                                        1u);
      _cfg.prods[q].gap = end;
    }
  }
  _p.owner = kNoElement;
  const LrLowerSlot start{.sym = nt_sym(_cfg.entryNt)};
  _cfg.startProd = _cfg.nprods;
  lr_push_prod(_o, _p, 0, std::span(&start, 1));
  lr_no_rule_without_production(_o);
  lr_bind_infix_ops(_o);
  lr_intern_aux(_cfg);
  lr_drop_twins(_o);
  lr_drop_unread_stops(_o);
  if (_d.entryCreate != nullptr &&
      descend(holds(body_in(0, entry), [&](const ImageNode& n) {
        return n.kind == ImageKind::create && n.row == _d.entryCreate;
      }))) {
    const std::uint16_t r = row_id(_d.entryCreate);
    _cfg.entryRow = _o.writes.intern_chain(&r, 1);
  }
  _o.writes.note_makes(_rowKind);
  lr_run_passes(_o, _rowKind);
  for (std::uint16_t k = 0; k < _cfg.nprods; ++k)
    if (_cfg.prods[k].turn)
      _cfg.prods[k].turnNode = _o.writes.turn_chain(_cfg.prods[k].node);
  _o.writes.note_makes(_rowKind);
  return std::move(_o);
}

LrLowered lr_lower(GrammarImage& image, const LowerDesc& desc) {
  if (desc.optionalGate > kOptionalGateMost)
    throw std::invalid_argument(
        "cursiv: the optional gate exceeds fifteen positions");
  return LrLowerer(image, desc).run();
}

}  // namespace cursiv::engine
