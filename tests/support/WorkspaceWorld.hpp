// The workspace fixture's grammar as a language whose every other slot is a
// default: what most lanes register before they ask a service.
#pragma once

#include <functional>
#include <string_view>

#include "support/World.hpp"
#include "workspace/generated/Workspace/Module.hpp"

namespace support {

template <class Services, class Shared>
Services& add_workspace(Shared& shared,
                        const std::function<void(Services&)>& tweak = {}) {
  return add_language<Services>(
      shared, {"workspace", {".ws"}, {}},
      ws::generated::Workspace::create_parser(),
      ws::generated::Workspace::create_ast_reflection(), tweak);
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

// A package holding a comment over three lines and an entity: the document an
// outline and its folds are read off.
// 0: package p {
// 1:   /* a block comment
// 2:      over three lines
// 3:   */
// 4:   datatype String
// 5:   entity Person {
// 6:     name : String
// 7:   }
// 8: }
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
