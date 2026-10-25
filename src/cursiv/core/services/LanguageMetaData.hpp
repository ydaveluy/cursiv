#pragma once

#include <string>
#include <vector>

// What identifies one language to a client and to the registry: the id a
// document declares, and what its files are called.

namespace cursiv {

struct LanguageMetaData {
  std::string languageId;
  std::vector<std::string> fileExtensions;
  // For the files an extension cannot route. An exact name outranks one.
  std::vector<std::string> fileNames = {};
};

}  // namespace cursiv
