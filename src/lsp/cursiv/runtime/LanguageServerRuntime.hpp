// The session: a server endpoint on a stream, the server's methods registered
// on it, and its message loop.
#pragma once

#include <lsp/io/stream.h>

#include "cursiv/services/SharedLspServices.hpp"

namespace cursiv {

// Runs one session on an open stream: 0 if the client asked for `shutdown`
// before `exit`, else 1. `shared` must hold its server and languages, and
// serves one session only: its services keep what the session told them.
[[nodiscard]] int start_language_server(SharedLspServices& shared,
                                        lsp::io::Stream& stream);

namespace detail {

// The same session over the process's standard input and output.
[[nodiscard]] int run_language_server_stdio(SharedLspServices& shared);

}  // namespace detail

}  // namespace cursiv
