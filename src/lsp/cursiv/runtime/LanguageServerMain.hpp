// A server binary's whole `main`: a shared container of the language's type,
// its languages registered, and the transport chosen from the command line.
#pragma once

#include <concepts>
#include <functional>
#include <string_view>

#include "cursiv/services/SharedLspServices.hpp"

namespace cursiv {

namespace detail {

[[nodiscard]] int run_language_server_main_on(
    int argc, char** argv, std::string_view serverName,
    const std::function<bool(SharedLspServices&)>& registerLanguageServices,
    SharedLspServices& sharedServices);

}  // namespace detail

// `TShared` is the container the session is run on: a language with
// workspace-wide services of its own passes its own type, and its module
// recovers it by reference. Default-constructible, as the runner builds it.
template <class TShared = SharedLspServices>
  requires std::derived_from<TShared, SharedLspServices>
[[nodiscard]] int run_language_server_main(
    int argc, char** argv, std::string_view serverName,
    const std::function<bool(SharedLspServices&)>& registerLanguageServices) {
  TShared sharedServices;
  return detail::run_language_server_main_on(
      argc, argv, serverName, registerLanguageServices, sharedServices);
}

}  // namespace cursiv
