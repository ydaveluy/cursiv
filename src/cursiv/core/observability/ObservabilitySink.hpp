#pragma once

#include <cstdint>
#include <string>

// Where the runtime says what went wrong when no client is owed an answer: a
// diagnostic belongs to a document and travels to the editor, these do not.
// The code is the identity and the message is prose, so a consumer compares
// the code. `publish` is noexcept and may run on any thread.

namespace cursiv {

enum class ObservationSeverity : std::uint8_t { Trace, Info, Warning, Error };

enum class ObservationCode : std::uint8_t {
  // A directory the scan was refused: a workspace that silently opens empty.
  WorkspaceDirectoryReadFailed,
  // A root folder the scan was refused; the other folders are still read.
  WorkspaceBootstrapFailed,
  // Two languages claim one extension; the later registration owns it.
  LanguageMappingCollision,
  // A lookup failed before its document computed its local symbols: a single
  // reference is read again, a plural one keeps the failure.
  ScopeReadBeforeComputed,
  // A per-node check threw; the failure became a diagnostic on that node and
  // the pass carried on. Said here too, because a crashing check is the
  // language's bug, not the document's.
  ValidationCheckThrew,
  // A document-level preparation hook threw; same law, whole-document anchor.
  ValidationPreparationThrew,
  // A file the scan or an update found and then could not read: left out of
  // the store, the others read.
  WorkspaceDocumentReadFailed,
};

struct Observation {
  ObservationSeverity severity = ObservationSeverity::Info;
  ObservationCode code = ObservationCode::WorkspaceDirectoryReadFailed;
  std::string message;
  std::string uri;
  std::string languageId;
};

class ObservabilitySink {
 public:
  virtual ~ObservabilitySink() noexcept = default;
  virtual void publish(const Observation&) noexcept = 0;
};

}  // namespace cursiv
