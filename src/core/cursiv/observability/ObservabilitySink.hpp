#pragma once

#include <cstdint>
#include <string>

// Where the runtime reports problems that belong to no document. The code
// identifies the problem; the message is for people. `publish` is noexcept and
// may run on any thread.

namespace cursiv {

enum class ObservationSeverity : std::uint8_t { Trace, Info, Warning, Error };

enum class ObservationCode : std::uint8_t {
  // A directory the scan could not read; the workspace may open empty.
  WorkspaceDirectoryReadFailed,
  // A root folder the scan could not read; the other folders are still read.
  WorkspaceBootstrapFailed,
  // Two languages claim one extension; the later registration owns it.
  LanguageMappingCollision,
  // A lookup failed before its document computed its local symbols: a single
  // reference is read again, a plural one keeps the failure.
  ScopeReadBeforeComputed,
  // A per-node check threw; the failure became a diagnostic on that node and
  // the pass carried on. Reported here too: a throwing check is a bug of the
  // language, not of the document.
  ValidationCheckThrew,
  // A document-level preparation hook threw; likewise, with the diagnostic on
  // the whole document.
  ValidationPreparationThrew,
  // A file the scan or an update found and then could not read: the scan leaves
  // it out of the store, an update leaves it empty, and the others are read.
  WorkspaceDocumentReadFailed,
  // A scope provider threw while a reference was resolved; the reference stays
  // unresolved and the others are still linked.
  ReferenceResolutionThrew,
  // A language server notification's handler threw; the notification is lost
  // and the session goes on.
  NotificationThrew,
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
