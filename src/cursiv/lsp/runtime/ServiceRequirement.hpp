#pragma once

#include "cursiv/core/workspace/DocumentState.hpp"

// What a request waits for: one document reaching a rung, or the whole
// workspace reaching it. An answer read from the inverse index is a workspace
// answer, and it is wrong until every document has climbed.

namespace cursiv {

struct ServiceRequirement {
  enum class Type { Document, Workspace };

  // A bare state is the document scope: the two spellings are one type, so a
  // route that needs no workspace rung reads as the state alone.
  constexpr ServiceRequirement(DocumentState s) noexcept  // NOLINT
      : state(s) {}
  constexpr ServiceRequirement(Type t, DocumentState s) noexcept
      : type(t), state(s) {}

  Type type = Type::Document;
  DocumentState state = DocumentState::Changed;
};

// One constant per rung, workspace-scoped.
namespace WorkspaceState {

inline constexpr ServiceRequirement Parsed{ServiceRequirement::Type::Workspace,
                                           DocumentState::Parsed};
inline constexpr ServiceRequirement IndexedContent{
    ServiceRequirement::Type::Workspace, DocumentState::IndexedContent};
inline constexpr ServiceRequirement ComputedScopes{
    ServiceRequirement::Type::Workspace, DocumentState::ComputedScopes};
inline constexpr ServiceRequirement Linked{ServiceRequirement::Type::Workspace,
                                           DocumentState::Linked};
inline constexpr ServiceRequirement IndexedReferences{
    ServiceRequirement::Type::Workspace, DocumentState::IndexedReferences};
inline constexpr ServiceRequirement Validated{
    ServiceRequirement::Type::Workspace, DocumentState::Validated};

}  // namespace WorkspaceState

}  // namespace cursiv
