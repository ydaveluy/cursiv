#include <memory>
#include <string>
#include <string_view>

#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "domainmodel/core/ast.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"
#include "cursiv/test/RecoveryLane.hpp"
#include "cursiv/test/MutationOracle.hpp"

namespace {
using namespace cursiv::test;

#if !defined(CURSIV_ENGINE_LL)
void split_keywords_preserve_the_package(const cursiv::Parser& engine) {
  const auto seed = document("valid.dmodel");
  const auto start = seed.find("entity E3");
  CHECK(start != std::string::npos);
  if (start == std::string::npos) return;
  const auto reflection = dm::generated::DomainModel::create_ast_reflection();
  for (const char blank : {' ', '\t', '\n', '\r'}) {
    for (unsigned offset = 1; offset < 5; ++offset) {
      for (const bool replace : {false, true}) {
        auto text = seed;
        if (replace)
          text[start + offset] = blank;
        else
          text.insert(start + offset, 1, blank);
        const auto result = engine.parse(text);
        const auto* model = result.value_as<dm::ast::DomainModel>();
        CHECK(result.accepted && model && model->elements.size() == 6);
        CHECK(result.diagnostics.size() == 1);
        if (result.diagnostics.size() == 1) {
          const auto& d = result.diagnostics[0];
          CHECK(d.kind == cursiv::DiagnosticKind::replaced);
          CHECK(d.begin == start && d.end == start + (replace ? 6u : 7u));
        }
        if (!model || model->elements.size() != 6) continue;
        const auto* element = model->elements.back();
        CHECK(element->name == "baz");
        const bool package = reflection->isInstance(
            *element, &cursiv::ast_type_tag<dm::ast::PackageDeclaration>);
        CHECK(package);
        if (!package) continue;
        const auto* baz = static_cast<const dm::ast::PackageDeclaration*>(element);
        CHECK(baz->elements.size() == 3);
        if (baz->elements.size() != 3) continue;
        for (unsigned i = 0; i < 2; ++i) {
          const auto* child = baz->elements[i];
          const bool entity = reflection->isInstance(
              *child, &cursiv::ast_type_tag<dm::ast::Entity>);
          CHECK(entity && child->name == (i == 0 ? "E3" : "E4"));
          if (!entity) continue;
          const auto* e = static_cast<const dm::ast::Entity*>(child);
          CHECK(e->features.size() == (i == 0 ? 3u : 0u));
          if (i == 0 && e->features.size() == 3) {
            CHECK(e->features[0]->name == "that");
            CHECK(e->features[1]->name == "other");
            CHECK(e->features[2]->name == "nested");
          }
        }
        const auto* child = baz->elements[2];
        const bool nested = reflection->isInstance(
            *child, &cursiv::ast_type_tag<dm::ast::PackageDeclaration>);
        CHECK(nested && child->name == "nested");
        if (!nested) continue;
        const auto* p = static_cast<const dm::ast::PackageDeclaration*>(child);
        CHECK(p->elements.size() == 1);
        if (p->elements.size() == 1) CHECK(p->elements[0]->name == "E5");
      }
    }
  }
}
#endif

void run(Lane& lane) {
  lane.lang = "domainmodel";
  const std::unique_ptr<cursiv::Parser> engine =
      dm::generated::DomainModel::create_parser();
#if !defined(CURSIV_ENGINE_LL)
  split_keywords_preserve_the_package(*engine);
  {
    // EOF repair must retain the already read package, including its complete
    // entity and the empty entity whose closing brace is missing.
    const auto text = document("06_truncated.dmodel");
    const auto result = engine->parse(text);
    const auto* model = result.value_as<dm::ast::DomainModel>();
    const auto reflection = dm::generated::DomainModel::create_ast_reflection();
    CHECK(result.accepted && model);
    const dm::ast::PackageDeclaration* baz = nullptr;
    if (model) {
      for (const auto* element : model->elements)
        if (element->name == "baz" &&
            reflection->isInstance(
                *element, &cursiv::ast_type_tag<dm::ast::PackageDeclaration>))
          baz = static_cast<const dm::ast::PackageDeclaration*>(element);
    }
    CHECK(baz && baz->elements.size() == 2);
    if (baz && baz->elements.size() == 2) {
      for (unsigned i = 0; i < 2; ++i) {
        const auto* element = baz->elements[i];
        const bool entity = reflection->isInstance(
            *element, &cursiv::ast_type_tag<dm::ast::Entity>);
        CHECK(entity);
        CHECK(element->name == (i == 0 ? "E3" : "E4"));
        if (entity)
          CHECK(static_cast<const dm::ast::Entity*>(element)->features.size() ==
                (i == 0 ? 3u : 0u));
      }
    }
  }
#endif
  language(lane, *engine, "valid.dmodel",
           {"01_typo1.dmodel", "02_typo2.dmodel", "03_closer_block.dmodel",
            "04_closer_eof.dmodel", "05_intruder.dmodel", "06_truncated.dmodel",
            "07_two_faults.dmodel", "08_head_fault.dmodel",
            "09_list_delim.dmodel", "10_noise.dmodel", "11_head_typo.dmodel"});
  mutation_oracle(*engine, "domainmodel",
                  document("valid.dmodel"));
}
}  // namespace

int main(int argc, char** argv) {
  cursiv::test::Lane lane;
  lane.write = argc > 1 && std::string_view(argv[1]) == "--write";
  run(lane);
  lane.close();
  return cursiv::test::report("domainmodel_recovery_test");
}
