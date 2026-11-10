// The workspace fixture language with default services, as most tests
// register it.
#pragma once

#include <functional>
#include <string_view>

#include "support/World.hpp"
#include "support/WorkspaceModule.hpp"

namespace support {

template <class Services, class Shared>
Services& add_workspace(Shared& shared,
                        const std::function<void(Services&)>& tweak = {}) {
  return add_language<Services>(
      shared, {"workspace", {".ws"}, {}},
      create_workspace_parser(), create_workspace_reflection(), tweak);
}

// That language alone in a workspace of its own.
template <class Shared, class Services>
struct WorkspaceWorld : World<Shared> {
  Services* ws;

  explicit WorkspaceWorld(const std::function<void(Services&)>& tweak = {})
      : ws(&add_workspace<Services>(this->shared, tweak)) {}
};

using CoreWorkspace =
    WorkspaceWorld<cursiv::SharedCoreServices, cursiv::CoreServices>;
using LspWorkspace =
    WorkspaceWorld<cursiv::SharedLspServices, cursiv::LspServices>;

// A package holding a three-line comment and an entity, for outline and
// folding tests.
inline constexpr std::string_view kPackage =
    "package p {\n"
    "  /* a block comment\n"
    "     over three lines\n"
    "  */\n"
    "  datatype String\n"
    "  entity Person {\n"
    "    name : String\n"
    "  }\n"
    "}\n";

// A document every rule of the formatter has something to fix in.
inline constexpr std::string_view kMessy =
    "entity   Person{\n"
    "name   :String\n"
    "     age:String\n"
    "}\n"
    "datatype     String\n";

}  // namespace support
