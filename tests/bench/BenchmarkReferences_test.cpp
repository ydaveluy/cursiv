#include <memory>
#include <string_view>
#include <utility>

#include "cursiv/bench/LanguageBench.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/Check.hpp"
#include "workspace/lsp/LspModule.hpp"

namespace {
class WithoutReferences final : public cursiv::Parser {
 public:
  explicit WithoutReferences(std::unique_ptr<const cursiv::Parser> parser)
      : parser_(std::move(parser)) {}

  const cursiv::Grammar& getGrammar() const noexcept override {
    return parser_->getGrammar();
  }

  cursiv::ParseResult parse(std::string_view text,
                            const cursiv::CancellationToken& cancel =
                                cursiv::kNoCancel) const override {
    auto result = parser_->parse(text, cancel);
    CHECK(result.references && result.references->handles.empty());
    result.references.reset();
    return result;
  }

 private:
  std::unique_ptr<const cursiv::Parser> parser_;
};

bool register_language(cursiv::SharedCoreServices& shared) {
  auto services = ws::create_lsp_services(
      dynamic_cast<ws::WorkspaceSharedServices&>(shared));
  services->parser =
      std::make_unique<WithoutReferences>(std::move(services->parser));
  shared.serviceRegistry->registerServices(std::move(services));
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  const std::string_view mode = argc > 1 ? argv[1] : "both";
  if (mode == "both" || mode == "single") {
    cursiv::bench::Row row;
    row.references = row.resolved = 99;
    cursiv::bench::ws_stages<ws::WorkspaceSharedServices>(
        1, false, "workspace", ".ws", "datatype lowercase", row,
        register_language);
    CHECK(row.references == 0);
    CHECK(row.resolved == 0);
    CHECK(row.diagnostics == 1);
    CHECK(row.ms[cursiv::bench::kIndex] >= 0);
  }
  if (mode == "both" || mode == "multi") {
    cursiv::bench::MultiRow row;
    row.references = row.resolved = 99;
    cursiv::bench::multi_stages<ws::WorkspaceSharedServices>(
        1, "workspace", ".ws", {"datatype Text", "datatype lowercase"}, {0},
        row, register_language);
    CHECK(row.references == 0);
    CHECK(row.resolved == 0);
    CHECK(row.diagnostics == 1);
    CHECK(row.points.size() == 1);
  }
  return cursiv::test::report("BenchmarkReferences");
}
