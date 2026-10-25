// One text into a built document, and the sweep that takes it back out. The
// document is added to the workspace store, so what a helper returns is
// reachable by URI until `clearDocuments` removes it.
#pragma once

#include <memory>
#include <span>
#include <string>

#include "cursiv/test/ParseHelperOptions.hpp"

namespace cursiv {
struct CoreServices;
struct Document;
struct SharedCoreServices;
}  // namespace cursiv

namespace cursiv::test {

[[nodiscard]] std::shared_ptr<Document> parseHelper(
    const CoreServices& services, std::string text,
    const ParseHelperOptions& options = {});

// The same, refusing a document the parse left no tree on.
[[nodiscard]] std::shared_ptr<Document> parseDocument(
    const CoreServices& services, std::string text,
    const ParseHelperOptions& options = {});

// The named documents, or -- empty -- every one the workspace holds.
void clearDocuments(const SharedCoreServices& shared,
                    std::span<const std::shared_ptr<Document>> documents = {});
void clearDocuments(const CoreServices& services,
                    std::span<const std::shared_ptr<Document>> documents = {});

}  // namespace cursiv::test
