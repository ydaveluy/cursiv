#pragma once

#include <string>
#include <vector>

// What a language claims about completion at the handshake: the characters
// that open the list, and those that commit an item.

namespace cursiv {

struct CompletionProviderOptions {
  std::vector<std::string> triggerCharacters;
  std::vector<std::string> allCommitCharacters;
};

}  // namespace cursiv
