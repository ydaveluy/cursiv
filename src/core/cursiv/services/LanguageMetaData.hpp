#pragma once

#include <string>
#include <vector>

// What identifies one language to a client and to the registry: the id a
// document declares, and what its files are called.

namespace cursiv {

struct LanguageMetaData {
  std::string languageId;
  std::vector<std::string> fileExtensions;
  // For files an extension cannot route; an exact name wins over an extension.
  std::vector<std::string> fileNames = {};
};

}  // namespace cursiv
