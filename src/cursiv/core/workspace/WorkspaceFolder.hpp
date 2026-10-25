#pragma once

#include <string>

// A root an editor opened, and what it is called there. This storey may not
// name the LSP protocol -- the same reason `text/Position` exists -- so it
// carries the two fields itself.

namespace cursiv {

struct WorkspaceFolder {
  std::string uri;
  // What the editor shows for it. Empty when the caller had no name to give --
  // a command line scanning a path -- and nothing reads it but a message.
  std::string name;
};

}  // namespace cursiv
