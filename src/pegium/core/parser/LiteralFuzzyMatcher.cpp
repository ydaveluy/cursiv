#include <pegium/core/parser/LiteralFuzzyMatcher.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

#include <pegium/core/parser/ContextShared.hpp>
#include <pegium/core/parser/TerminalTextClass.hpp>
#include <pegium/core/utils/TextUtils.hpp>

namespace pegium::parser::detail {

namespace {

constexpr std::uint32_t kNoMatch =
    std::numeric_limits<std::uint32_t>::max() / 4u;
constexpr std::uint32_t kInsertionCost =
    default_edit_cost(ParseDiagnosticKind::Inserted);
constexpr std::uint32_t kDeletionCost =
    default_edit_cost(ParseDiagnosticKind::Deleted);
constexpr std::uint32_t kSubstitutionCost =
    default_edit_cost(ParseDiagnosticKind::Replaced);
constexpr std::uint32_t kTranspositionCost = kSubstitutionCost;
constexpr std::uint32_t kReferenceLiteralLength = 5u;

enum class ParentOp : std::uint8_t {
  None,
  Match,
  Substitute,
  InsertMissingInputCodepoint,
  DeleteUnexpectedInputCodepoint,
  TransposeAdjacentCodepoints,
};

struct DpCell {
  std::uint32_t operations = kNoMatch;
  std::uint32_t weightedCost = kNoMatch;
  ParentOp parent = ParentOp::None;
};

struct DpState {
  DpCell anyEdit;
  DpCell noSubstitution;
};

/// Compact per-cell parent record (one `ParentOp` per lane, 2 bytes total)
/// kept for the full DP grid so a candidate can be reconstructed, while the
/// costly `operations`/`weightedCost` lanes only need a 3-row rolling window
/// during the fill. Shrinks the full-grid footprint from ~24 to 2 bytes/cell.
struct ParentTrace {
  ParentOp anyEdit = ParentOp::None;
  ParentOp noSubstitution = ParentOp::None;
};

enum class DpLane : std::uint8_t {
  AnyEdit,
  NoSubstitution,
};

[[nodiscard]] constexpr bool is_better_cell(const DpCell &lhs,
                                            const DpCell &rhs) noexcept {
  if (lhs.weightedCost != rhs.weightedCost) {
    return lhs.weightedCost < rhs.weightedCost;
  }
  return lhs.operations < rhs.operations;
}

void consider_transition(DpCell &best, const DpCell &candidate,
                         ParentOp parent, std::uint32_t operationDelta,
                         std::uint32_t weightDelta) noexcept {
  if (candidate.operations == kNoMatch) {
    return;
  }

  const DpCell next{.operations = candidate.operations + operationDelta,
                    .weightedCost = candidate.weightedCost + weightDelta,
                    .parent = parent};
  if (best.operations == kNoMatch || is_better_cell(next, best)) {
    best = next;
  }
}

[[nodiscard]] constexpr std::uint32_t
ceil_div(std::uint32_t numerator, std::uint32_t denominator) noexcept {
  return denominator == 0u ? numerator
                           : (numerator + denominator - 1u) / denominator;
}

/// A decoded UTF-8 sequence: one entry per codepoint, plus a trailing
/// byte-offset table so a codepoint index can be mapped back to a byte offset
/// (the contract callers rely on for `consumed`). `byteOffset[i]` is the byte
/// offset of codepoint `i`; `byteOffset.back()` is the total byte length, so
/// `byteOffset[k]` is the byte length of the first `k` codepoints. Decoding is
/// lossy-tolerant: a truncated/invalid lead byte advances one byte and yields
/// the lead byte as the codepoint (mirrors `decode_utf8_codepoint`'s fallback),
/// so adversarial input still produces a well-formed, monotone offset table.
struct DecodedText {
  std::vector<std::uint32_t> codepoints;
  std::vector<std::size_t> byteOffset; // size == codepoints.size() + 1
};

[[nodiscard]] DecodedText decode_text(std::string_view text) noexcept {
  DecodedText decoded;
  decoded.codepoints.reserve(text.size());
  decoded.byteOffset.reserve(text.size() + 1u);
  std::size_t index = 0u;
  while (index < text.size()) {
    decoded.byteOffset.push_back(index);
    const char *const cursor = text.data() + index;
    const auto length = utils::utf8_codepoint_length(*cursor);
    if (length == 0u || length > text.size() - index) {
      // Truncated / invalid: consume a single byte as its own codepoint so the
      // offset table stays monotone and the byte<->codepoint mapping is exact.
      decoded.codepoints.push_back(static_cast<unsigned char>(*cursor));
      ++index;
      continue;
    }
    decoded.codepoints.push_back(utils::decode_utf8_codepoint(cursor));
    index += length;
  }
  decoded.byteOffset.push_back(text.size());
  return decoded;
}

[[nodiscard]] constexpr RecoveryCost
make_fuzzy_recovery_cost(std::size_t literalCodepointCount,
                         std::uint32_t rawWeightedCost) noexcept {
  // The denominator is the literal's CODEPOINT count (not its byte size): a
  // multibyte keyword such as `café` (4 codepoints / 5 bytes) must be bucketed
  // by its 4 codepoints so the cost-per-length normalisation matches the
  // codepoint-granular DP. kReferenceLiteralLength is left untuned.
  return make_recovery_cost(
      rawWeightedCost,
      ceil_div(rawWeightedCost * kReferenceLiteralLength,
               static_cast<std::uint32_t>(
                   std::max<std::size_t>(literalCodepointCount, 1u))));
}

[[nodiscard]] LiteralFuzzyCandidate
reconstruct_candidate(const std::vector<ParentTrace> &parents, std::size_t cols,
                      const DecodedText &literal, const DecodedText &input,
                      std::size_t consumedCodepoints, const DpCell &cell,
                      DpLane lane) noexcept {
  const auto parent_at =
      [&parents, cols, lane](std::size_t literalIndex,
                             std::size_t consumedIndex) noexcept {
    const auto &trace = parents[literalIndex * cols + consumedIndex];
    return lane == DpLane::AnyEdit ? trace.anyEdit : trace.noSubstitution;
  };

  LiteralFuzzyCandidate candidate{
      // `consumed` is the BYTE length of the first `consumedCodepoints`
      // codepoints of the input — callers advance the cursor by this byte span.
      .consumed = input.byteOffset[consumedCodepoints],
      .consumedCodepoints = consumedCodepoints,
      .distance = cell.operations,
      // .operationCount is recomputed from the reconstructed op counts below.
      .rawWeightedCost = cell.weightedCost,
      .cost = make_fuzzy_recovery_cost(literal.codepoints.size(),
                                       cell.weightedCost),
  };

  std::size_t literalIndex = literal.codepoints.size();
  std::size_t consumedIndex = consumedCodepoints;
  while (literalIndex > 0u || consumedIndex > 0u) {
    const auto parent = parent_at(literalIndex, consumedIndex);
    switch (parent) {
    case ParentOp::None:
      literalIndex = 0u;
      consumedIndex = 0u;
      break;
    case ParentOp::Match:
      --literalIndex;
      --consumedIndex;
      break;
    case ParentOp::Substitute:
      ++candidate.substitutionCount;
      --literalIndex;
      --consumedIndex;
      break;
    case ParentOp::InsertMissingInputCodepoint:
      ++candidate.insertionCount;
      --literalIndex;
      break;
    case ParentOp::DeleteUnexpectedInputCodepoint:
      ++candidate.deletionCount;
      --consumedIndex;
      break;
    case ParentOp::TransposeAdjacentCodepoints:
      ++candidate.transpositionCount;
      literalIndex -= 2u;
      consumedIndex -= 2u;
      break;
    }
  }

  candidate.operationCount = candidate.insertionCount + candidate.deletionCount +
                             candidate.substitutionCount +
                             candidate.transpositionCount;
  return candidate;
}

[[nodiscard]] constexpr bool
same_candidate(const LiteralFuzzyCandidate &lhs,
               const LiteralFuzzyCandidate &rhs) noexcept {
  return lhs.consumed == rhs.consumed &&
         lhs.consumedCodepoints == rhs.consumedCodepoints &&
         lhs.distance == rhs.distance &&
         lhs.operationCount == rhs.operationCount &&
         lhs.rawWeightedCost == rhs.rawWeightedCost &&
         lhs.cost.budgetCost == rhs.cost.budgetCost &&
         lhs.cost.primaryRankCost == rhs.cost.primaryRankCost &&
         lhs.insertionCount == rhs.insertionCount &&
         lhs.deletionCount == rhs.deletionCount &&
         lhs.substitutionCount == rhs.substitutionCount &&
         lhs.transpositionCount == rhs.transpositionCount;
}

[[nodiscard]] constexpr bool
is_better_candidate(const LiteralFuzzyCandidate &lhs,
                    const LiteralFuzzyCandidate &rhs) noexcept {
  return lhs.cost.primaryRankCost < rhs.cost.primaryRankCost;
}

[[nodiscard]] constexpr bool
dominates_candidate(const LiteralFuzzyCandidate &lhs,
                    const LiteralFuzzyCandidate &rhs) noexcept {
  const bool noWorse =
      lhs.cost.primaryRankCost <= rhs.cost.primaryRankCost &&
      lhs.rawWeightedCost <= rhs.rawWeightedCost &&
      lhs.distance <= rhs.distance &&
      lhs.substitutionCount <= rhs.substitutionCount &&
      lhs.consumed >= rhs.consumed;
  if (!noWorse) {
    return false;
  }
  return lhs.cost.primaryRankCost < rhs.cost.primaryRankCost ||
         lhs.rawWeightedCost < rhs.rawWeightedCost ||
         lhs.distance < rhs.distance ||
         lhs.substitutionCount < rhs.substitutionCount ||
         lhs.consumed > rhs.consumed;
}

constexpr void push_candidate(LiteralFuzzyCandidates &candidates,
                              const LiteralFuzzyCandidate &candidate) {
  for (const auto &existing : candidates) {
    if (same_candidate(existing, candidate)) {
      return;
    }
  }
  candidates.push_back(candidate);
}

[[nodiscard]] LiteralFuzzyCandidates
prune_dominated_candidates(const LiteralFuzzyCandidates &candidates) {
  LiteralFuzzyCandidates frontier;
  frontier.reserve(candidates.size());
  std::vector<bool> dominated(candidates.size(), false);
  for (std::size_t index = 0u; index < candidates.size(); ++index) {
    for (std::size_t otherIndex = 0u; otherIndex < candidates.size();
         ++otherIndex) {
      if (index == otherIndex) {
        continue;
      }
      if (dominates_candidate(candidates[otherIndex], candidates[index])) {
        dominated[index] = true;
        break;
      }
    }
  }
  for (std::size_t index = 0u; index < candidates.size(); ++index) {
    if (!dominated[index]) {
      // The input is already duplicate-free (built solely via push_candidate)
      // and we only emit non-dominated entries, so push_candidate's dedup scan
      // could never short-circuit here — a plain push_back is equivalent.
      frontier.push_back(candidates[index]);
    }
  }
  std::ranges::sort(frontier,
                    [](const LiteralFuzzyCandidate &lhs,
                       const LiteralFuzzyCandidate &rhs) noexcept {
                      return is_better_candidate(lhs, rhs);
                    });
  return frontier;
}

[[nodiscard]] bool
has_word_like_local_anchor(std::string_view literal, std::string_view input,
                           bool caseSensitive) noexcept {
  if (!is_word_like_terminal(literal)) {
    return true;
  }

  constexpr std::size_t kAnchorWindow = 3u;
  const auto literalWindow = std::min(literal.size(), kAnchorWindow);
  const auto inputWindow = std::min(input.size(), kAnchorWindow);
  if (literalWindow <= 1u || inputWindow <= 1u) {
    return true;
  }

  // A multi-codepoint UTF-8 window may have continuation bytes that look
  // word-like at the byte level but encode punctuation. Decode codepoints
  // so we don't accidentally classify e.g. `«` (U+00AB) as an anchor
  // letter.
  if (input_window_has_non_identifier_codepoint(input, kAnchorWindow)) {
    return true;
  }

  // The anchor walk below indexes both strings by BYTE (not codepoint). It is
  // intentionally ASCII-only: a multibyte window that survives the guard above
  // can still carry whole non-ASCII letters whose continuation bytes are
  // compared here as raw bytes, so for such windows the loop may under-admit an
  // anchor. That only declines to short-circuit (returning false runs the
  // codepoint-granular recovery DP anyway), so the byte-level walk stays
  // conservative; multibyte anchor parity is deferred to the DP.
  for (std::size_t literalIndex = 0u; literalIndex < literalWindow;
       ++literalIndex) {
    for (std::size_t inputIndex = 0u; inputIndex < inputWindow; ++inputIndex) {
      const auto indexDistance =
          literalIndex > inputIndex ? literalIndex - inputIndex
                                    : inputIndex - literalIndex;
      if (indexDistance > 1u) {
        continue;
      }
      if (equal_codepoint(static_cast<unsigned char>(literal[literalIndex]),
                          static_cast<unsigned char>(input[inputIndex]),
                          caseSensitive)) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] bool equals_text(std::string_view lhs, std::string_view rhs,
                               bool caseSensitive) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (!equal_codepoint(static_cast<unsigned char>(lhs[index]),
                         static_cast<unsigned char>(rhs[index]),
                         caseSensitive)) {
      return false;
    }
  }
  return true;
}

} // namespace

LiteralFuzzyCandidatesCache::LiteralFuzzyCandidatesCache() {
  // Allocate raw uninitialised storage and only zero the generation field
  // of every entry. Other fields are read only after the generation check
  // confirms the slot is live, so leaving them uninitialised is safe and
  // saves the per-construction memset of every Entry (memset of the full
  // ~256 KiB array dominated the cost of growing the slot count to 4096
  // before this trick).
  void *const raw = ::operator new(sizeof(std::array<Entry, kSlotCount>));
  _entries.reset(static_cast<std::array<Entry, kSlotCount> *>(raw));
  for (auto &entry : *_entries) {
    entry.generation = 0;
  }
}

LiteralFuzzyCandidatesCache::~LiteralFuzzyCandidatesCache() noexcept {
  // Free only the slots that ever transitioned to live, recorded in
  // `_liveSlots` (each live index appears exactly once — see header). Entries
  // never written keep `generation == 0` and an uninitialised `resultData`
  // that must not be dereferenced; skipping them avoids both the stride over
  // all `kSlotCount` slots and any read of garbage. A live slot may legitimately
  // hold `resultData == nullptr` (empty pruned result); `::operator delete`
  // tolerates nullptr, so no extra guard is needed.
  for (const auto index : _liveSlots) {
    ::operator delete((*_entries)[index].resultData);
  }
}

namespace {

[[nodiscard]] LiteralFuzzyCandidates
compute_pruned_literal_fuzzy_candidates(std::string_view literal,
                                        std::string_view input,
                                        bool caseSensitive) noexcept {
  // Decode both strings to codepoint sequences. The Levenshtein DP is indexed
  // by CODEPOINT — distance, operationCount and transposition are all counted
  // in codepoints — so a multibyte typo (`café`->`cafe`) costs one edit, not
  // two byte edits. `consumed` is mapped back to a byte offset at
  // reconstruction time via the decoded input's byte-offset table, preserving
  // the byte-offset contract callers depend on. For ASCII (codepoint == byte)
  // this is bit-for-bit identical to the prior byte DP.
  const DecodedText literalDecoded = decode_text(literal);
  const DecodedText inputDecoded = decode_text(input);
  const auto &literalCps = literalDecoded.codepoints;
  const auto &inputCps = inputDecoded.codepoints;

  LiteralFuzzyCandidates candidates;
  candidates.reserve(inputCps.size() * 2u);
  const auto rows = literalCps.size() + 1u;
  const auto cols = inputCps.size() + 1u;
  // Full parent trace (2 bytes/cell) for candidate reconstruction, plus a
  // 3-row rolling window of costs. Cell (literalIndex, consumed) reads row
  // literalIndex (delete, same row, prior column), literalIndex-1
  // (match/substitute/insert) and literalIndex-2 (transposition), so three
  // cost rows suffice while the parent grid stays full for the backtrace.
  // Rows/cols are codepoint counts now, not byte counts.
  std::vector<ParentTrace> parents(rows * cols);
  std::array<std::vector<DpState>, 3u> rowRing{};
  for (auto &row : rowRing) {
    row.assign(cols, DpState{});
  }
  const auto row_for =
      [&rowRing](std::size_t literalIndex) noexcept -> std::vector<DpState> & {
    return rowRing[literalIndex % 3u];
  };

  for (std::size_t literalIndex = 0u; literalIndex <= literalCps.size();
       ++literalIndex) {
    auto &currentRow = row_for(literalIndex);
    for (std::size_t consumed = 0u; consumed <= inputCps.size(); ++consumed) {
      DpCell anyBest;
      DpCell noSubBest;
      if (literalIndex == 0u && consumed == 0u) {
        anyBest = {.operations = 0u,
                   .weightedCost = 0u,
                   .parent = ParentOp::None};
        noSubBest = anyBest;
      } else if (literalIndex == 0u) {
        consider_transition(anyBest, currentRow[consumed - 1u].anyEdit,
                            ParentOp::DeleteUnexpectedInputCodepoint, 1u,
                            kDeletionCost);
        consider_transition(noSubBest, currentRow[consumed - 1u].noSubstitution,
                            ParentOp::DeleteUnexpectedInputCodepoint, 1u,
                            kDeletionCost);
      } else if (consumed == 0u) {
        const auto &prevRow = row_for(literalIndex - 1u);
        consider_transition(anyBest, prevRow[0u].anyEdit,
                            ParentOp::InsertMissingInputCodepoint, 1u,
                            kInsertionCost);
        consider_transition(noSubBest, prevRow[0u].noSubstitution,
                            ParentOp::InsertMissingInputCodepoint, 1u,
                            kInsertionCost);
      } else {
        const auto &prevRow = row_for(literalIndex - 1u);
        const auto literalCp = literalCps[literalIndex - 1u];
        const auto inputCp = inputCps[consumed - 1u];

        if (equal_decoded_codepoint(literalCp, inputCp, caseSensitive)) {
          consider_transition(anyBest, prevRow[consumed - 1u].anyEdit,
                              ParentOp::Match, 0u, 0u);
          consider_transition(noSubBest, prevRow[consumed - 1u].noSubstitution,
                              ParentOp::Match, 0u, 0u);
        }

        consider_transition(anyBest, prevRow[consumed - 1u].anyEdit,
                            ParentOp::Substitute, 1u, kSubstitutionCost);
        consider_transition(anyBest, prevRow[consumed].anyEdit,
                            ParentOp::InsertMissingInputCodepoint, 1u,
                            kInsertionCost);
        consider_transition(noSubBest, prevRow[consumed].noSubstitution,
                            ParentOp::InsertMissingInputCodepoint, 1u,
                            kInsertionCost);
        consider_transition(anyBest, currentRow[consumed - 1u].anyEdit,
                            ParentOp::DeleteUnexpectedInputCodepoint, 1u,
                            kDeletionCost);
        consider_transition(noSubBest, currentRow[consumed - 1u].noSubstitution,
                            ParentOp::DeleteUnexpectedInputCodepoint, 1u,
                            kDeletionCost);

        if (literalIndex >= 2u && consumed >= 2u &&
            equal_decoded_codepoint(literalCps[literalIndex - 1u],
                                    inputCps[consumed - 2u], caseSensitive) &&
            equal_decoded_codepoint(literalCps[literalIndex - 2u],
                                    inputCps[consumed - 1u], caseSensitive)) {
          const auto &prevPrevRow = row_for(literalIndex - 2u);
          consider_transition(anyBest, prevPrevRow[consumed - 2u].anyEdit,
                              ParentOp::TransposeAdjacentCodepoints, 1u,
                              kTranspositionCost);
          consider_transition(noSubBest,
                              prevPrevRow[consumed - 2u].noSubstitution,
                              ParentOp::TransposeAdjacentCodepoints, 1u,
                              kTranspositionCost);
        }
      }

      currentRow[consumed] = {.anyEdit = anyBest, .noSubstitution = noSubBest};
      parents[literalIndex * cols + consumed] = {
          .anyEdit = anyBest.parent, .noSubstitution = noSubBest.parent};
    }
  }

  const bool wordLikeCandidateSpace =
      is_word_like_terminal(literal) && is_word_like_terminal(input);
  const auto &finalRow = row_for(literalCps.size());
  for (std::size_t consumed = 1u; consumed <= inputCps.size(); ++consumed) {
    const auto collect_candidate =
        [&](const DpCell &cell, DpLane lane) noexcept {
          if (cell.operations == kNoMatch || cell.operations == 0u) {
            return;
          }
          auto candidate = reconstruct_candidate(
              parents, cols, literalDecoded, inputDecoded, consumed, cell, lane);
          if (wordLikeCandidateSpace && consumed < inputCps.size()) {
            // Trailing unconsumed CODEPOINTS, charged as deletions — the
            // codepoint analogue of the prior byte-count trailing penalty.
            const auto trailing =
                static_cast<std::uint32_t>(inputCps.size() - consumed);
            candidate.distance += trailing;
            candidate.deletionCount += trailing;
            candidate.rawWeightedCost += trailing * kDeletionCost;
            candidate.operationCount += trailing;
            candidate.cost = make_fuzzy_recovery_cost(
                literalCps.size(), candidate.rawWeightedCost);
          }
          push_candidate(candidates, candidate);
        };
    const auto &state = finalRow[consumed];
    collect_candidate(state.anyEdit, DpLane::AnyEdit);
    collect_candidate(state.noSubstitution, DpLane::NoSubstitution);
  }

  return prune_dominated_candidates(candidates);
}

[[nodiscard]] std::size_t
fuzzy_cache_slot_index(std::string_view literal, std::string_view input,
                       bool caseSensitive) noexcept {
  const auto literalHash =
      reinterpret_cast<std::uintptr_t>(literal.data()) ^
      (literal.size() * 0x9E3779B97F4A7C15ULL);
  const auto inputHash =
      reinterpret_cast<std::uintptr_t>(input.data()) ^
      (input.size() * 0xBF58476D1CE4E5B9ULL);
  return (literalHash ^ inputHash ^
          (caseSensitive ? 0xDEADBEEFULL : 0u)) &
         (LiteralFuzzyCandidatesCache::kSlotCount - 1);
}

void store_pruned_into_slot(LiteralFuzzyCandidatesCache &cache,
                            std::size_t slotIndex, std::string_view literal,
                            std::string_view input, bool caseSensitive,
                            const LiteralFuzzyCandidates &pruned) noexcept {
  auto &slot = *cache.slotAt(slotIndex);
  // Free any previous owner of this slot (collision eviction). On the first
  // write the slot is still at generation 0 (never owned heap memory); record
  // it as live exactly once on that 0->1 transition so the destructor frees it.
  if (slot.generation != 0) {
    ::operator delete(slot.resultData);
  } else {
    cache.markSlotLive(slotIndex);
  }
  slot.literalData = literal.data();
  slot.literalSize = literal.size();
  slot.inputData = input.data();
  slot.inputSize = input.size();
  slot.caseSensitive = caseSensitive;
  if (pruned.empty()) {
    slot.resultData = nullptr;
    slot.resultSize = 0;
  } else {
    const auto bytes = pruned.size() * sizeof(LiteralFuzzyCandidate);
    slot.resultData =
        static_cast<LiteralFuzzyCandidate *>(::operator new(bytes));
    // LiteralFuzzyCandidate is trivially copyable; raw byte copy is safe.
    std::memcpy(slot.resultData, pruned.data(), bytes);
    slot.resultSize = static_cast<std::uint32_t>(pruned.size());
  }
  slot.generation = 1; // mark live
}

} // namespace

std::span<const LiteralFuzzyCandidate>
find_literal_fuzzy_candidates_view(std::string_view literal,
                                   std::string_view input,
                                   bool caseSensitive,
                                   LiteralFuzzyCandidatesCache &cache) noexcept {
  if (literal.empty() || input.empty()) {
    return {};
  }

  // Cache lookup must run before `equals_text` / `has_word_like_local_anchor`
  // so the dominant cache-hit path skips both per-call O(N) scans.
  const auto slotIndex = fuzzy_cache_slot_index(literal, input, caseSensitive);
  auto &slot = *cache.slotAt(slotIndex);
  if (slot.generation != 0 && slot.literalData == literal.data() &&
      slot.literalSize == literal.size() &&
      slot.inputData == input.data() && slot.inputSize == input.size() &&
      slot.caseSensitive == caseSensitive) {
    return {slot.resultData, slot.resultSize};
  }

  // Miss: compute (locally-pruned misses are also cached so adversarial
  // inputs that bail at the anchor / equality check don't keep paying that
  // cost each time the parser revisits them).
  LiteralFuzzyCandidates pruned;
  if (!equals_text(literal, input, caseSensitive) &&
      has_word_like_local_anchor(literal, input, caseSensitive)) {
    pruned = compute_pruned_literal_fuzzy_candidates(literal, input,
                                                     caseSensitive);
  }
  store_pruned_into_slot(cache, slotIndex, literal, input, caseSensitive,
                         pruned);
  return {slot.resultData, slot.resultSize};
}

bool literal_has_single_edit_strict_match(std::string_view literal,
                                          std::string_view window,
                                          bool caseSensitive) noexcept {
  const auto N = literal.size();
  const auto W = window.size();
  if (N == 0u) {
    return false;
  }
  const auto try_alignment = [&](std::size_t K, int diff) noexcept {
    if (K > W) {
      return false;
    }
    std::size_t p = 0u;
    const std::size_t pmax = std::min(N, K);
    while (p < pmax && equal_codepoint(static_cast<unsigned char>(literal[p]),
                                       static_cast<unsigned char>(window[p]),
                                       caseSensitive)) {
      ++p;
    }
    std::size_t s = 0u;
    const std::size_t smax = std::min(N - p, K - p);
    while (s < smax &&
           equal_codepoint(
               static_cast<unsigned char>(literal[N - 1u - s]),
               static_cast<unsigned char>(window[K - 1u - s]),
               caseSensitive)) {
      ++s;
    }
    const std::size_t litRemain = N - p - s;
    const std::size_t winRemain = K - p - s;
    if (diff == 0 && litRemain == 1u && winRemain == 1u) {
      return true; // substitution
    }
    if (diff == 1 && litRemain == 0u && winRemain == 1u) {
      return true; // insertion in window
    }
    if (diff == -1 && litRemain == 1u && winRemain == 0u) {
      return true; // deletion from window
    }
    if (diff == 0 && litRemain == 2u && winRemain == 2u && N >= 2u &&
        equal_codepoint(static_cast<unsigned char>(literal[p]),
                        static_cast<unsigned char>(window[p + 1u]),
                        caseSensitive) &&
        equal_codepoint(static_cast<unsigned char>(literal[p + 1u]),
                        static_cast<unsigned char>(window[p]),
                        caseSensitive)) {
      return true; // adjacent transposition
    }
    return false;
  };
  if (try_alignment(N, 0)) {
    return true;
  }
  if (W >= N + 1u && try_alignment(N + 1u, 1)) {
    return true;
  }
  if (N >= 1u && W + 1u >= N && try_alignment(N - 1u, -1)) {
    return true;
  }
  return false;
}

} // namespace pegium::parser::detail
