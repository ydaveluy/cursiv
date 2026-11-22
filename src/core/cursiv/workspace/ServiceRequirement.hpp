#pragma once

#include "cursiv/workspace/DocumentState.hpp"

// What a request waits for: one document reaching a state, or the whole
// workspace reaching it. An answer read from the inverse index is a workspace
// answer, wrong until every document has reached the state.

namespace cursiv {

struct ServiceRequirement {
  enum class Type { Document, Workspace };

  // A bare state is the document scope: the two spellings are one type, so a
  // route that needs no workspace state reads as the state alone.
  constexpr ServiceRequirement(DocumentState s) noexcept  // NOLINT
      : state(s) {}
  constexpr ServiceRequirement(Type t, DocumentState s) noexcept
      : type(t), state(s) {}

  Type type = Type::Document;
  DocumentState state = DocumentState::Changed;
};

// One constant per state, workspace-scoped.
namespace WorkspaceState {

inline constexpr ServiceRequirement kParsed{ServiceRequirement::Type::Workspace,
                                           DocumentState::Parsed};
inline constexpr ServiceRequirement kIndexedContent{
    ServiceRequirement::Type::Workspace, DocumentState::IndexedContent};
inline constexpr ServiceRequirement kComputedScopes{
    ServiceRequirement::Type::Workspace, DocumentState::ComputedScopes};
inline constexpr ServiceRequirement kLinked{ServiceRequirement::Type::Workspace,
                                           DocumentState::Linked};
inline constexpr ServiceRequirement kIndexedReferences{
    ServiceRequirement::Type::Workspace, DocumentState::IndexedReferences};
inline constexpr ServiceRequirement kValidated{
    ServiceRequirement::Type::Workspace, DocumentState::Validated};

}  // namespace WorkspaceState

}  // namespace cursiv
