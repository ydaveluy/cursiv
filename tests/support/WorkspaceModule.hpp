#pragma once

#include <memory>

namespace cursiv {
class Parser;
struct AstReflection;
}

namespace support {

// The linked workspace variant supplies these factories.
std::unique_ptr<cursiv::Parser> create_workspace_parser();
std::unique_ptr<cursiv::AstReflection> create_workspace_reflection();

}  // namespace support
