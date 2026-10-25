// The program one grammar lowers to: a body per rule, kept as the shape its
// author wrote, over one packed symbol space and the token graph of every
// terminal.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/engines/ll/table/LlCellBuild.hpp"
#include "cursiv/engines/ll/table/LlClimb.hpp"
#include "cursiv/engines/ll/table/LlDecision.hpp"
#include "cursiv/engines/ll/table/LlNode.hpp"
#include "cursiv/engines/ll/table/LlRuleInfo.hpp"
#include "cursiv/engines/shared/gen/LowerPools.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/Vec.hpp"
#include "cursiv/engines/shared/gen/optional_shape.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"

namespace cursiv {

// Every row grows to meet its write: nothing here is counted in advance, and
// no frozen program holds any of it -- what a reader gets is emitted as plain
// arrays instead.
struct LlLowCfg : LowerPools {
  std::uint16_t nterms = 0;
  std::uint16_t nrules = 0;
  std::uint32_t nnodes = 0;
  std::uint32_t nkids = 0;
  std::uint16_t ndecisions = 0;
  std::uint16_t nclimbs = 0;
  std::uint16_t nclimbOps = 0;
  std::uint16_t entryRule = 0;
  // The class a keyword may not run into, as the grammar declares it.
  ByteSet wordBytes{};

  // The optional positions a sequence spells out as subsets.
  unsigned optionalGate = kOptionalGate;

  Vec<LlRuleInfo> rules{};
  Vec<LlNode> nodes{};
  Vec<std::uint16_t> kids{};
  Vec<LlDecision> decisions{};
  Vec<LlClimb> climbs{};
  Vec<LlClimbOp> climbOps{};

  Vec<std::uint8_t> ruleCfg{};

  // What only the lowering reads, beside the cells and not in them: a table
  // is emitted without any of it.
  Vec<LlRuleBuild> ruleBuild{};
  Vec<LlNodeBuild> nodeBuild{};
  Vec<LlDecisionBuild> decisionBuild{};

  std::span<const std::uint16_t> children(const LlNode& node) const {
    if (node.kind == LlShape::rep) return {&node.sym, 1};
    if (node.kind == LlShape::seq || node.kind == LlShape::alt)
      return {kids.data() + node.sym, node.count};
    return {};
  }

  // A one-token operator may have one conjoined condition in front of it.
  // Its terminal keeps the direct-switch path; other bodies are descended.
  std::uint16_t atomic_operator(std::uint16_t body) const {
    const auto& n = nodes[body];
    if (n.kind == LlShape::seq && n.count == 2 &&
        nodes[kids[n.sym]].kind == LlShape::condition)
      body = kids[n.sym + 1];
    return nodes[body].kind == LlShape::term ? body : kNoRow;
  }

  // Terminated: a generated table prints these as C strings. A name is
  // addressed by a sixteen-bit offset, and past that the offset wraps: the
  // second name would be written over the first, and every name read after it
  // would be another one. The walk carries on writing a well-formed value; the
  // refusal stands.
  std::uint16_t intern_name(std::string_view s) {
    if (std::size_t{nameLen} + s.size() + 1u > 0xffffu) {
      lower_refuse(*this, LowerFail::namePool);
      return 0;
    }
    const std::uint16_t off = nameLen;
    char* const put = names.row(off, s.size() + 1);
    std::ranges::copy(s, put);
    put[s.size()] = '\0';
    nameLen = static_cast<std::uint16_t>(off + s.size() + 1);
    return off;
  }

  std::string_view rule_name(std::uint16_t r) const {
    return name_view(rules[r].nameOff, rules[r].nameLen);
  }
};

}  // namespace cursiv
