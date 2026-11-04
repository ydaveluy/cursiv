#pragma once

#ifdef __wasi__
#include <cstdlib>
#include <filesystem>
#include <system_error>
#endif

namespace cursiv {

// Stands where the caller stands. A WASI module starts at its root whatever the
// host's directory is, so a path its command line names relative to that
// directory would read another file; its runner says where that is in `PWD`.
// Elsewhere the process already stands there.
inline void enter_host_directory() {
#ifdef __wasi__
  if (const char* const directory = std::getenv("PWD")) {
    std::error_code ignored;
    std::filesystem::current_path(directory, ignored);
  }
#endif
}

}  // namespace cursiv
