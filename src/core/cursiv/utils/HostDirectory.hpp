#pragma once

#ifdef __wasi__
#include <cstdlib>
#include <filesystem>
#include <system_error>
#endif

namespace cursiv::detail {

// Moves to the caller's directory. A WASI module starts at its root, so it
// reads the host's directory from `PWD`; elsewhere the process is already
// there.
inline void enter_host_directory() {
#ifdef __wasi__
  if (const char* const directory = std::getenv("PWD")) {
    std::error_code ignored;
    std::filesystem::current_path(directory, ignored);
  }
#endif
}

}  // namespace cursiv::detail
