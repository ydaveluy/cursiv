#include "cursiv/engines/lr/gen/LrLowerer.hpp"

#include <span>

#include "cursiv/engines/lr/gen/lr_intern_name.hpp"
#include "cursiv/engines/lr/gen/lr_lower_define.hpp"
#include "cursiv/engines/lr/gen/lr_lower_settle.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv {

namespace {

void lr_note_lead(ChainPool& w, std::span<const WriteKind> kind) {
  for (std::uint16_t i = 0; i < w.nchains; ++i) {
    const Chain& ch = w.chains[i];
    for (std::uint16_t k = 0; k < ch.count; ++k) {
      const WriteKind what = kind[w.acts[ch.off + k]];
      if (what != WriteKind::assign && what != WriteKind::append) continue;
      w.chainBuild[i].lead = w.acts[ch.off + k];
      break;
    }
  }
}

}  // namespace

LrLowerer::LrLowerer(GrammarImage& image, const LowerDesc& desc)
    : Lowerer(image, desc, lowered.cfg, lowered.cfg.nterms,
              lowered.cfg.wordBytes, lowered.writes, lowered.fault, pool),
      _o(lowered),
      _p(pool),
      _cfg(lowered.cfg) {}

std::uint16_t LrLowerer::intern_name(std::string_view text) {
  return lr_intern_name(_cfg, text);
}

void LrLowerer::declare_start() {
  _cfg.nnts = 1;
  const std::uint16_t startName = lr_intern_name(_cfg, "$accept");
  _cfg.nts[0].nameOff = startName;
  _cfg.nts[0].nameLen = 7;
}

void LrLowerer::declare_rules() {
  for (const std::uint32_t rule : _ntRule) {
    std::uint16_t len = 0;
    const std::uint16_t off = name_of(rule, len);
    _cfg.nts[_cfg.nnts].nameOff = off;
    _cfg.nts[_cfg.nnts].nameLen = len;
    ++_cfg.nnts;
  }
  for (std::size_t k = 1; k < _cfgs.size(); ++k)
    for (std::size_t i = 0; i < _ntRule.size(); ++i) {
      const auto row = _cfg.nts[1 + i];
      _cfg.nts[_cfg.nnts++] = row;
    }
}

ImageId LrLowerer::ended(ImageId id) {
  if (const auto it = _ended.find(id); it != _ended.end()) return it->second;
  const ImageNode n = at(id);
  ImageId out = id;
  if (n.kind == ImageKind::eof ||
      (n.kind == ImageKind::andOf && at(kid(n, 0)).kind == ImageKind::eof)) {
    out = section_end();
  } else if (n.kind == ImageKind::implied) {
    const ImageId tok = kid(n, 0);
    const ImageId cond = ended(kid(n, 1));
    if (cond != kid(n, 1)) {
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
        out = map_kids(id, [&](ImageId k) { return ended(k); });
    }
  }
  _ended.emplace(id, out);
  return out;
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
  lr_no_non_consuming(_o, _p);
  _p.owner = kNoElement;
  const LrLowerSlot start{.sym = nt_sym(_cfg.entryNt)};
  _cfg.startProd = _cfg.nprods;
  lr_push_prod(_o, _p, 0, std::span(&start, 1));
  lr_no_rule_without_production(_o);
  lr_settle_infix_ops(_o);
  lr_intern_aux(_cfg);
  lr_drop_twins(_o);
  lr_drop_unread_stops(_o);
  if (_d.entryCreate != nullptr &&
      creates(body_in(0, entry), _d.entryCreate)) {
    const std::uint16_t r = row_id(_d.entryCreate);
    _cfg.entryRow = _o.writes.intern_chain(&r, 1);
  }
  _o.writes.note_makes(_rowKind);
  lr_settle(_o);
  for (std::uint16_t k = 0; k < _cfg.nprods; ++k)
    if (_cfg.prodBuild[k].turn)
      _cfg.prodBuild[k].turnNode = _o.writes.turn_chain(_cfg.prods[k].node);
  _o.writes.note_makes(_rowKind);
  lr_note_lead(_o.writes, _rowKind);
  return std::move(_o);
}

LrLowered lr_lower(GrammarImage& image, const LowerDesc& desc) {
  return LrLowerer(image, desc).run();
}

}  // namespace cursiv
