// The session: a server endpoint on a stream, the server's methods registered
// on it, and its message loop. The endpoint owns the lifecycle -- initialize,
// shutdown, exit -- and the trace; the workspace side of every message belongs
// to `LanguageServer`.
#pragma once

#include <lsp/io/stream.h>

#include "cursiv/services/SharedLspServices.hpp"

namespace cursiv {

/**
 * Runs one session on an already-open stream. Returns 0 if the client
 * asked for `shutdown` before `exit`, 1 otherwise. `shared` must already
 * carry its language server and languages; the session installs a
 * language client on it for the call and removes it before returning.
 * Run one session per `shared`: its services keep what a session told
 * them, such as the client's settings and the diagnostics it was sent.
 */
[[nodiscard]] int start_language_server(SharedLspServices& shared,
                                        lsp::io::Stream& stream);

namespace detail {

// The same session over the process's standard input and output.
[[nodiscard]] int run_language_server_stdio(SharedLspServices& shared);

}  // namespace detail

}  // namespace cursiv
