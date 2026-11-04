#pragma once

#include <string>
#include <vector>

// What a language claims at the handshake about completion: the bytes that
// open the list unprompted, and the bytes that commit an item when the client
// can't decide per item. `resolveProvider` and per-item capabilities are the
// server's own answer, not a language's, so they live elsewhere.

namespace cursiv {

struct CompletionProviderOptions {
  // Empty is the honest "nothing to announce": a character listed nowhere never
  // reopens the list on its own.
  std::vector<std::string> triggerCharacters;
  std::vector<std::string> allCommitCharacters;
};

}  // namespace cursiv
