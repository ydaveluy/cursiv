#include "cursiv/lsp/runtime/LanguageServerMain.hpp"

#include <lsp/types.h>

#include <charconv>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/utils/UriUtils.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/LocalFileSystemProvider.hpp"
#include "cursiv/core/workspace/WorkspaceFolder.hpp"
#include "cursiv/core/workspace/WorkspaceManager.hpp"
#include "cursiv/lsp/completion/CompletionProvider.hpp"
#include "cursiv/lsp/mcp/LspMcpService.hpp"
#include "cursiv/lsp/runtime/LanguageServerRuntime.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/mcp/McpServer.hpp"
#include "cursiv/mcp/McpService.hpp"

namespace cursiv::detail {

namespace {

// `FILE:LINE:CHARACTER`, LSP's own zero-based numbering.
struct CompletionQuery {
  std::string path;
  unsigned line = 0;
  unsigned character = 0;
};

std::optional<CompletionQuery> parse_complete_at(std::string_view spec) {
  const std::size_t c = spec.rfind(':');
  if (c == std::string_view::npos || c == 0) return std::nullopt;
  const std::size_t l = spec.rfind(':', c - 1);
  if (l == std::string_view::npos || l == 0) return std::nullopt;
  CompletionQuery query;
  query.path = std::string(spec.substr(0, l));
  const auto number = [](std::string_view t, unsigned& out) {
    if (t.empty()) return false;
    const auto [end, error] = std::from_chars(t.data(), t.data() + t.size(), out);
    return error == std::errc{} && end == t.data() + t.size();
  };
  if (!number(spec.substr(l + 1, c - l - 1), query.line)) return std::nullopt;
  if (!number(spec.substr(c + 1), query.character)) return std::nullopt;
  return query;
}

// Drives content assist by hand, through the same container an editor
// would: the scope provider reaches the workspace index, filled by
// scanning a folder -- the one given, else the file's own -- with the
// transport left out.
int run_completion(const CompletionQuery& query,
                   std::optional<std::string_view> folderPath,
                   SharedLspServices& shared) {
  const std::filesystem::path file =
      std::filesystem::absolute(std::filesystem::path(query.path));
  const std::filesystem::path folder =
      folderPath.has_value()
          ? std::filesystem::absolute(std::filesystem::path(*folderPath))
          : file.parent_path();
  const std::vector<WorkspaceFolder> folders{
      {path_to_file_uri(folder.string()), {}}};
  shared.workspaceManager->initializeWorkspace(folders);

  const std::string uri = path_to_file_uri(file.string());
  const std::shared_ptr<Document> document = shared.documents->getDocument(uri);
  if (document == nullptr) {
    std::cerr << "no such document in the workspace: " << uri << '\n';
    return 1;
  }
  const auto* services = dynamic_cast<const LspServices*>(document->language);
  const CompletionProvider* const provider =
      services == nullptr ? nullptr : services->completionProvider.get();
  if (provider == nullptr) {
    std::cerr << "this language installs no completion provider\n";
    return 1;
  }

  lsp::Position position;
  position.line = query.line;
  position.character = query.character;
  const lsp::CompletionList list = provider->getCompletion(*document, position);
  std::cout << file.filename().string() << ':' << query.line << ':'
            << query.character << "  " << list.items.size() << " item(s)"
            << (list.isIncomplete ? " (incomplete)" : "") << '\n';
  for (const lsp::CompletionItem& item : list.items) {
    std::cout << "  " << item.label;
    if (item.detail.has_value()) std::cout << "  [" << *item.detail << ']';
    if (item.documentation.has_value())
      if (const auto* markup =
              std::get_if<lsp::MarkupContent>(&*item.documentation))
        std::cout << "  -- " << markup->value;
    std::cout << '\n';
  }
  return 0;
}

// The MCP session over stdio: one JSON message per line in, one per
// answer out. Saved files only -- the workspace is scanned from the
// folder, with no editor buffer involved.
int run_mcp(std::string_view folderPath, SharedLspServices& shared) {
  const McpService* const service = shared.mcpService.get();
  if (service == nullptr) {
    std::cerr << "no MCP service is installed\n";
    return 1;
  }
  const std::filesystem::path folder =
      std::filesystem::absolute(std::filesystem::path(folderPath));
  const std::vector<WorkspaceFolder> folders{
      {path_to_file_uri(folder.string()), {}}};
  shared.workspaceManager->initializeWorkspace(folders);
  const std::unique_ptr<const McpServer> server = service->createServer();
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;
    if (const std::optional<std::string> answer = server->handleText(line))
      std::cout << *answer << '\n' << std::flush;
  }
  return 0;
}

}  // namespace

int run_language_server_main_on(
    int argc, char** argv, std::string_view serverName,
    const std::function<bool(SharedLspServices&)>& registerLanguageServices,
    SharedLspServices& sharedServices) {
  // A language server reads the disk, and says so: without this, the shared
  // default would be the empty provider.
  if (sharedServices.fileSystemProvider == nullptr)
    sharedServices.fileSystemProvider =
        std::make_unique<LocalFileSystemProvider>();
  install_default_shared_lsp_services(sharedServices);
  try {
    if (registerLanguageServices && !registerLanguageServices(sharedServices)) {
      std::cerr << serverName << ": no language could be registered\n";
      return 1;
    }
  } catch (const std::exception& error) {
    std::cerr << serverName << ": " << error.what() << '\n';
    return 1;
  }
  if (sharedServices.mcpService == nullptr)
    sharedServices.mcpService = std::make_unique<LspMcpService>(
        sharedServices, McpServiceOptions{std::string(serverName), "0.1.0"});

  bool mcp = false;
  std::optional<std::string_view> folder;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--mcp")
      mcp = true;
    else if (arg == "--folder" && i + 1 < argc)
      folder = argv[++i];
    else if (arg.starts_with("--folder="))
      folder = arg.substr(9);
  }
  if (mcp) return run_mcp(folder.value_or("."), sharedServices);

  constexpr std::string_view kFlag = "--complete-at";
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    std::string_view spec;
    if (arg == kFlag && i + 1 < argc)
      spec = argv[++i];
    else if (arg.starts_with(kFlag) && arg.size() > kFlag.size() &&
             arg[kFlag.size()] == '=')
      spec = arg.substr(kFlag.size() + 1);
    else
      continue;
    const std::optional<CompletionQuery> query = parse_complete_at(spec);
    if (!query.has_value()) {
      std::cerr << serverName << ' ' << kFlag
                << " FILE:LINE:CHARACTER   (line and character are 0-based)\n";
      return 2;
    }
    return run_completion(*query, folder, sharedServices);
  }
  return run_language_server_stdio(sharedServices);
}

}  // namespace cursiv::detail
