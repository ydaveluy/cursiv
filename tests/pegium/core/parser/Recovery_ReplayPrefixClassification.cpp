/// `ReplayPrefixClass` classification on recovery candidates.
///
/// `OrderedChoice` dominance reads the closed `ReplayPrefixClass`
/// axis of the candidate envelope. Each candidate is classified at
/// construction via `classify_replay_prefix(hadEdits, hasDestructiveEdit)`
/// (editable candidates derive `hadEdits` as `editCount != 0`). The
/// dominance predicate reads the closed enum field instead of
/// re-deriving the family from raw edit counts and `hasDestructiveEdit`
/// flags.
///
/// This suite pins the closed mapping the classifier implements:
///
///   - no edits                         -> `Empty`
///   - edits with at least one delete   -> `ExtendedCommittedPrefix`
///   - insert-only edits                -> `NewLocalPrefix`
///
/// Cache neutrality and end-to-end behaviour of the dominance
/// predicate using this classification are exercised by the
/// cache-neutrality harness; this suite keeps to the unit-level
/// mapping contract.

#include <pegium/core/parser/RecoveryCandidate.hpp>

#include <gtest/gtest.h>

using pegium::parser::detail::classify_replay_prefix;
using pegium::parser::detail::EditableRecoveryCandidate;
using pegium::parser::detail::ReplayPrefixClass;

TEST(ReplayPrefixClassification, closed_mapping_over_edit_axes) {
  // Table-driven fold of the closed `{hadEdits, hasDestructiveEdit} ->
  // ReplayPrefixClass` mapping. Every distinct input pair and its
  // expected class from the former per-mapping tests survives as one
  // row, including the structural-progress angle on the destructive
  // pair (Deleted OR Replaced are both destructive and commit to a
  // strictly different replay prefix — same mapping as edit+delete).
  struct Row {
    bool hadEdits;
    bool hasDestructiveEdit;
    ReplayPrefixClass expected;
    const char *label;
  };
  static constexpr Row kRows[] = {
      {false, false, ReplayPrefixClass::Empty, "no edits, no destructive"},
      {false, true, ReplayPrefixClass::Empty, "no edits, destructive flag set"},
      {true, true, ReplayPrefixClass::ExtendedCommittedPrefix,
       "edits with delete/replace -> extended committed prefix"},
      {true, false, ReplayPrefixClass::NewLocalPrefix,
       "insert-only edits -> new local prefix"},
  };
  for (const auto &row : kRows) {
    SCOPED_TRACE(testing::Message()
                 << "case=" << row.label << " hadEdits=" << row.hadEdits
                 << " hasDestructiveEdit=" << row.hasDestructiveEdit);
    EXPECT_EQ(classify_replay_prefix(row.hadEdits, row.hasDestructiveEdit),
              row.expected);
  }
}

TEST(ReplayPrefixClassification, default_constructed_candidate_is_empty) {
  // The default value of `replayPrefix` on a freshly-constructed
  // candidate must be `Empty`, matching the classifier's mapping for
  // the zero-edit shape. This guarantees that placeholders (e.g.
  // `bestCandidate{}` accumulators) start at the safe class that
  // disqualifies them from dominance until they are filled. Both
  // OrderedChoice/Group and Repetition now share this single carrier.
  const EditableRecoveryCandidate candidate;
  EXPECT_EQ(candidate.replayPrefix, ReplayPrefixClass::Empty);
  EXPECT_EQ(candidate.editCount, 0u);
}
