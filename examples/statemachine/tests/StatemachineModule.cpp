#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <type_traits>

#include <statemachine/cli/Generator.hpp>
#include <statemachine/lsp/LspModule.hpp>
#include <statemachine/lsp/LspServices.hpp>

#include <pegium/core/references/DefaultNameProvider.hpp>
#include <statemachine/lsp/StatemachineFormatter.hpp>
#include <statemachine/core/validation/StatemachineValidator.hpp>

#include <pegium/examples/ExampleTestSupport.hpp>
#include <pegium/lsp/services/Services.hpp>

namespace statemachine::tests {
namespace {
[[nodiscard]] std::filesystem::path example_root() {
  return pegium::test::current_source_directory().parent_path() / "example";
}

[[nodiscard]] std::filesystem::path make_temp_directory() {
  const auto suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  auto path = std::filesystem::temp_directory_path() /
              ("pegium-statemachine-tests-" + suffix);
  std::filesystem::create_directories(path);
  return path;
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

std::string apply_text_edits(const pegium::workspace::Document &document,
                             std::vector<::lsp::TextEdit> edits) {
  const auto &textDocument = document.textDocument();
  auto text = std::string(textDocument.getText());
  std::ranges::sort(edits, [&textDocument](const auto &left, const auto &right) {
    return textDocument.offsetAt(left.range.start) >
           textDocument.offsetAt(right.range.start);
  });
  for (const auto &edit : edits) {
    const auto begin = textDocument.offsetAt(edit.range.start);
    const auto end = textDocument.offsetAt(edit.range.end);
    text.replace(begin, end - begin, edit.newText);
  }
  return text;
}

TEST(StatemachineModuleTest, SplitsValidationIntoDedicatedClass) {
  static_assert(std::is_class_v<
                statemachine::validation::StatemachineValidator>);
  static_assert(std::is_base_of_v<pegium::Services,
                                  statemachine::StatemachineServices>);
  SUCCEED();
}

TEST(StatemachineModuleTest, InstallsFormatterOverride) {
  auto shared = std::make_unique<statemachine::StatemachineSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto services =
      statemachine::createStatemachineLspServices(*shared, "statemachine");

  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->validator,
            nullptr);
  EXPECT_NE(dynamic_cast<pegium::references::DefaultNameProvider *>(
                services->references.nameProvider.get()),
            nullptr);
  EXPECT_NE(dynamic_cast<statemachine::StatemachineFormatter *>(
                services->lsp.formatter.get()),
            nullptr);

  static_assert(std::is_base_of_v<pegium::NamedAstNode, statemachine::ast::State>);
  statemachine::ast::State state;
  state.name = "Idle";
  EXPECT_EQ(services->references.nameProvider->getName(state),
            (std::optional<std::string>{"Idle"}));
}

TEST(StatemachineModuleTest, ValidatorWarnsOnLowerCaseStateName) {
  auto shared = std::make_unique<statemachine::StatemachineSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(statemachine::registerStatemachineLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("machine.statemachine"),
      "statemachine",
      "statemachine Light\n"
      "events Start\n"
      "initialState idle\n"
      "state idle end\n");

  ASSERT_NE(document, nullptr);
  EXPECT_TRUE(pegium::test::has_diagnostic_message(
      *document, "State name should start with a capital letter"));
}

TEST(StatemachineModuleTest, ValidatorReportsDuplicateStatesAndEvents) {
  auto shared = std::make_unique<statemachine::StatemachineSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(statemachine::registerStatemachineLspServices(*shared));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("machine-duplicates.statemachine"),
      "statemachine",
      "statemachine Light\n"
      "events\n"
      "  Start\n"
      "  Start\n"
      "initialState Idle\n"
      "state Idle end\n"
      "state Idle end\n");

  ASSERT_NE(document, nullptr);
  std::size_t duplicateCount = 0;
  for (const auto &diagnostic : document->diagnostics) {
    if (diagnostic.message.find("Duplicate identifier name:") !=
        std::string::npos) {
      ++duplicateCount;
    }
  }
  EXPECT_EQ(duplicateCount, 4u);
}

TEST(StatemachineModuleTest, FormatterFormatsCompactMachine) {
  auto shared = std::make_unique<statemachine::StatemachineSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  auto registeredServices =
      statemachine::createStatemachineLspServices(*shared, "statemachine");

  ASSERT_NE(registeredServices, nullptr);
  shared->serviceRegistry->registerServices(std::move(registeredServices));

  auto document = pegium::test::open_and_build_document(
      *shared, pegium::test::make_file_uri("statemachine-format.statemachine"),
      "statemachine",
      "statemachine Light events Start Stop commands Open Close initialState Idle "
      "state Idle actions{Open Close} Start=>Running end state Running end");
  ASSERT_NE(document, nullptr);

  const auto *coreServices = &shared->serviceRegistry->getServices(document->uri);
  const auto *services = dynamic_cast<const pegium::Services *>(coreServices);
  ASSERT_NE(services, nullptr);
  ASSERT_NE(services->lsp.formatter, nullptr);

  ::lsp::DocumentFormattingParams params{};
  params.options.insertSpaces = true;
  params.options.tabSize = 2;
  const auto edits = services->lsp.formatter->formatDocument(
      *document, params, pegium::utils::default_cancel_token);

  ASSERT_FALSE(edits.empty());
  EXPECT_EQ(apply_text_edits(*document, edits),
            "statemachine Light\n"
            "events\n"
            "  Start\n"
            "  Stop\n"
            "commands\n"
            "  Open\n"
            "  Close\n"
            "initialState Idle\n"
            "\n"
            "state Idle actions { Open Close }\n"
            "  Start => Running\n"
            "end\n"
            "\n"
            "state Running end");
}

TEST(StatemachineModuleTest, CreateLanguageServicesReturnsTypedServices) {
  auto shared = std::make_unique<statemachine::StatemachineSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);

  auto services =
      statemachine::createStatemachineLspServices(*shared, "statemachine");

  ASSERT_NE(services, nullptr);
  EXPECT_NE(services->validator, nullptr);
}

TEST(StatemachineModuleTest, GeneratorMatchesExpectedExampleOutput) {
  auto shared = std::make_unique<statemachine::StatemachineSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(statemachine::registerStatemachineLspServices(*shared));

  auto document = shared->workspace.documents->createDocument(
      pegium::test::make_file_uri("trafficlight.statemachine"),
      "statemachine TrafficLight\n"
      "\n"
      "events\n"
      "    switchCapacity\n"
      "    next\n"
      "\n"
      "initialState PowerOff\n"
      "\n"
      "state PowerOff\n"
      "    switchCapacity => RedLight\n"
      "end\n"
      "\n"
      "state RedLight\n"
      "    switchCapacity => PowerOff\n"
      "    next => GreenLight\n"
      "end\n"
      "\n"
      "state YellowLight\n"
      "    switchCapacity => PowerOff\n"
      "    next => RedLight\n"
      "end\n"
      "\n"
      "state GreenLight\n"
      "    switchCapacity => PowerOff\n"
      "    next => YellowLight\n"
      "end\n");
  ASSERT_NE(document, nullptr);

  pegium::workspace::BuildOptions options;
  options.validation = true;
  const std::array<std::shared_ptr<pegium::workspace::Document>, 1> documents{
      document};
  shared->workspace.documentBuilder->build(documents, options);

  ASSERT_TRUE(document->diagnostics.empty());
  auto *model = pegium::ast_ptr_cast<ast::Statemachine>(document->parseResult.value);
  ASSERT_NE(model, nullptr);

  EXPECT_EQ(statemachine::generate_cpp_content(*model),
            "#include <iostream>\n"
            "#include <map>\n"
            "#include <string>\n"
            "\n"
            "class TrafficLight;\n"
            "\n"
            "class State {\n"
            "protected:\n"
            "    TrafficLight *statemachine;\n"
            "\n"
            "public:\n"
            "    virtual ~State() {}\n"
            "\n"
            "    void set_context(TrafficLight *statemachine) {\n"
            "        this->statemachine = statemachine;\n"
            "    }\n"
            "\n"
            "    virtual std::string get_name() {\n"
            "        return \"Unknown\";\n"
            "    }\n"
            "\n"
            "    virtual void switchCapacity() {\n"
            "        std::cout << \"Impossible event for the current state.\" << std::endl;\n"
            "    }\n"
            "\n"
            "    virtual void next() {\n"
            "        std::cout << \"Impossible event for the current state.\" << std::endl;\n"
            "    }\n"
            "};\n"
            "\n"
            "\n"
            "class TrafficLight {\n"
            "private:\n"
            "    State* state = nullptr;\n"
            "\n"
            "public:\n"
            "    TrafficLight(State* initial_state) {\n"
            "        initial_state->set_context(this);\n"
            "        state = initial_state;\n"
            "        std::cout << \"[\" << state->get_name() << \"]\" << std::endl;\n"
            "    }\n"
            "\n"
            "    ~TrafficLight() {\n"
            "        if (state != nullptr) {\n"
            "            delete state;\n"
            "        }\n"
            "    }\n"
            "\n"
            "    void transition_to(State *new_state) {\n"
            "        std::cout << state->get_name() << \" ===> \" << new_state->get_name() << std::endl;\n"
            "        if (state != nullptr) {\n"
            "            delete state;\n"
            "        }\n"
            "        new_state->set_context(this);\n"
            "        state = new_state;\n"
            "    }\n"
            "\n"
            "    void switchCapacity() {\n"
            "        state->switchCapacity();\n"
            "    }\n"
            "\n"
            "    void next() {\n"
            "        state->next();\n"
            "    }\n"
            "};\n"
            "\n"
            "\n"
            "class PowerOff : public State {\n"
            "public:\n"
            "    std::string get_name() override { return \"PowerOff\"; }\n"
            "    void switchCapacity() override;\n"
            "};\n"
            "\n"
            "class RedLight : public State {\n"
            "public:\n"
            "    std::string get_name() override { return \"RedLight\"; }\n"
            "    void switchCapacity() override;\n"
            "    void next() override;\n"
            "};\n"
            "\n"
            "class YellowLight : public State {\n"
            "public:\n"
            "    std::string get_name() override { return \"YellowLight\"; }\n"
            "    void switchCapacity() override;\n"
            "    void next() override;\n"
            "};\n"
            "\n"
            "class GreenLight : public State {\n"
            "public:\n"
            "    std::string get_name() override { return \"GreenLight\"; }\n"
            "    void switchCapacity() override;\n"
            "    void next() override;\n"
            "};\n"
            "\n"
            "// PowerOff\n"
            "void PowerOff::switchCapacity() {\n"
            "    statemachine->transition_to(new RedLight);\n"
            "}\n"
            "\n"
            "\n"
            "// RedLight\n"
            "void RedLight::switchCapacity() {\n"
            "    statemachine->transition_to(new PowerOff);\n"
            "}\n"
            "\n"
            "\n"
            "void RedLight::next() {\n"
            "    statemachine->transition_to(new GreenLight);\n"
            "}\n"
            "\n"
            "\n"
            "// YellowLight\n"
            "void YellowLight::switchCapacity() {\n"
            "    statemachine->transition_to(new PowerOff);\n"
            "}\n"
            "\n"
            "\n"
            "void YellowLight::next() {\n"
            "    statemachine->transition_to(new RedLight);\n"
            "}\n"
            "\n"
            "\n"
            "// GreenLight\n"
            "void GreenLight::switchCapacity() {\n"
            "    statemachine->transition_to(new PowerOff);\n"
            "}\n"
            "\n"
            "\n"
            "void GreenLight::next() {\n"
            "    statemachine->transition_to(new YellowLight);\n"
            "}\n"
            "\n"
            "\n"
            "typedef void (TrafficLight::*Event)();\n"
            "\n"
            "int main() {\n"
            "    TrafficLight *statemachine = new TrafficLight(new PowerOff);\n"
            "\n"
            "    static std::map<std::string, Event> event_by_name;\n"
            "    event_by_name[\"switchCapacity\"] = &TrafficLight::switchCapacity;\n"
            "    event_by_name[\"next\"] = &TrafficLight::next;\n"
            "\n"
            "    for (std::string input; std::getline(std::cin, input);) {\n"
            "        std::map<std::string, Event>::const_iterator event_by_name_it = event_by_name.find(input);\n"
            "        if (event_by_name_it == event_by_name.end()) {\n"
            "            std::cout << \"There is no event <\" << input << \"> in the TrafficLight statemachine.\" << std::endl;\n"
            "            continue;\n"
            "        }\n"
            "        Event event_invoker = event_by_name_it->second;\n"
            "        (statemachine->*event_invoker)();\n"
            "    }\n"
            "\n"
            "    delete statemachine;\n"
            "    return 0;\n"
            "}\n");
}

TEST(StatemachineModuleTest, GeneratorDedupesTransitionsSharingAnEvent) {
  auto shared = std::make_unique<statemachine::StatemachineSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);
  ASSERT_TRUE(statemachine::registerStatemachineLspServices(*shared));

  auto document = shared->workspace.documents->createDocument(
      pegium::test::make_file_uri("dup-event.statemachine"),
      "statemachine M\n"
      "\n"
      "events\n"
      "    go\n"
      "\n"
      "initialState A\n"
      "\n"
      "state A\n"
      "    go => B\n"
      "    go => C\n"
      "end\n"
      "\n"
      "state B\n"
      "end\n"
      "\n"
      "state C\n"
      "end\n");
  ASSERT_NE(document, nullptr);

  pegium::workspace::BuildOptions options;
  options.validation = true;
  const std::array<std::shared_ptr<pegium::workspace::Document>, 1> documents{
      document};
  shared->workspace.documentBuilder->build(documents, options);

  auto *model = pegium::ast_ptr_cast<ast::Statemachine>(document->parseResult.value);
  ASSERT_NE(model, nullptr);

  const auto content = statemachine::generate_cpp_content(*model);
  const auto count = [&content](std::string_view needle) {
    std::size_t found = 0;
    for (auto pos = content.find(needle); pos != std::string::npos;
         pos = content.find(needle, pos + needle.size())) {
      ++found;
    }
    return found;
  };
  // A's two transitions on `go` must collapse to exactly one method; a
  // duplicate override/definition would not compile.
  EXPECT_EQ(count("void A::go()"), 1u);
  EXPECT_EQ(count("void go() override;"), 1u);
}

TEST(StatemachineModuleTest, CliGenerateCreatesExpectedFile) {
  const auto tempDirectory = make_temp_directory();
  const auto outputDirectory = tempDirectory / "generated-output";
  const auto inputPath = std::filesystem::absolute(
      example_root() / "trafficlight.statemachine");
  const std::string command = std::string("\"") +
                              PEGIUM_EXAMPLE_STATEMACHINE_CLI_PATH +
                              "\" generate \"" + inputPath.string() +
                              "\" -d \"" + outputDirectory.string() + "\"";

  const auto exitCode = std::system(command.c_str());
  ASSERT_EQ(exitCode, 0);

  const auto generatedFile = outputDirectory / "trafficlight.cpp";
  EXPECT_TRUE(std::filesystem::exists(generatedFile));
  EXPECT_NE(read_file(generatedFile).find("class TrafficLight;"),
            std::string::npos);

  std::filesystem::remove_all(tempDirectory);
}

TEST(StatemachineModuleTest, CliGenerateRejectsUnexpectedFileExtensionCleanly) {
  const auto tempDirectory = make_temp_directory();
  const auto outputFile = tempDirectory / "statemachine-cli-invalid.out";
  const auto inputPath = tempDirectory / "invalid.txt";
  {
    std::ofstream out(inputPath, std::ios::binary);
    out << "statemachine Demo";
  }

  const std::string command = std::string("\"") +
                              PEGIUM_EXAMPLE_STATEMACHINE_CLI_PATH +
                              "\" generate \"" + inputPath.string() +
                              "\" > \"" + outputFile.string() + "\" 2>&1";

  const auto exitCode = std::system(command.c_str());
  ASSERT_EQ(exitCode, 3 << 8);

  const auto output = read_file(outputFile);
  EXPECT_NE(output.find(
                "Please choose a file with one of these extensions: "
                ".statemachine."),
            std::string::npos);

  std::filesystem::remove_all(tempDirectory);
}

} // namespace
} // namespace statemachine::tests
