#include "cursiv/McpServer.hpp"

#include <lsp/json/json.h>
#include <lsp/jsonrpc/jsonrpc.h>
#include <lsp/thread_pool.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

namespace cursiv {

namespace {

namespace json = lsp::json;
namespace rpc = lsp::jsonrpc;

constexpr const char* kVersions[] = {"2026-07-28", "2025-11-25", "2025-06-18",
                                     "2025-03-26", "2024-11-05"};
constexpr std::string_view kProtocol = kVersions[0];

constexpr std::string_view kVersionKey =
    "io.modelcontextprotocol/protocolVersion";
constexpr std::string_view kCapabilitiesKey =
    "io.modelcontextprotocol/clientCapabilities";

const json::Value kNullId;

json::Array supported_versions() {
  return json::Array(std::begin(kVersions), std::end(kVersions));
}

const json::Value* field(const json::Value* object, std::string_view key) {
  return object && object->isObject() ? object->object().find(key) : nullptr;
}

json::Value envelope(const json::Value* id, std::string_view key,
                     json::Value value) {
  json::Object message;
  message["jsonrpc"] = "2.0";
  if (id) message["id"] = *id;
  message[key] = std::move(value);
  return message;
}

json::Value error_response(const json::Value* id, json::Integer code,
                           std::string message, json::Value data = {}) {
  json::Object error;
  error["code"] = code;
  error["message"] = std::move(message);
  if (!data.isNull()) error["data"] = std::move(data);
  return envelope(id, "error", std::move(error));
}

// Reads up to the next newline, which it drops; false at the end of the input.
bool read_line(std::FILE* input, std::string& line) {
  line.clear();
  int c = std::getc(input);
  if (c == EOF) return false;
  for (; c != EOF && c != '\n'; c = std::getc(input))
    line.push_back(static_cast<char>(c));
  return true;
}

bool valid_id(const json::Value& id) {
  return id.isString() ||
         (id.isNumber() && std::abs(id.number()) <= 9007199254740991.0 &&
          std::trunc(id.number()) == id.number());
}

using RequestId = std::variant<std::int64_t, std::string>;

RequestId request_key(const json::Value& id) {
  if (id.isString()) return id.string();
  return static_cast<std::int64_t>(id.number());
}

std::optional<json::Value> parse_message(std::string_view text,
                                         json::Value& message) {
  try {
    message = json::parse(text);
    return std::nullopt;
  } catch (const std::exception& error) {
    return error_response(nullptr, rpc::Error::ParseError, error.what());
  }
}

json::Value text_content(std::string&& text) {
  json::Object item;
  item["type"] = "text";
  item["text"] = std::move(text);
  json::Array content;
  content.emplace_back(std::move(item));
  return content;
}

json::Value list_entries(std::string_view key, const auto& entries) {
  json::Array items(entries.size());
  for (const auto& [name, entry] : entries)
    items[entry.order] = entry.descriptor;
  json::Object result;
  result[key] = std::move(items);
  return result;
}

}  // namespace

McpServer::McpServer(std::string name, std::string version)
    : _name(std::move(name)), _version(std::move(version)) {}

void McpServer::registerTool(McpTool tool, McpToolFunction execute) {
  json::Object item;
  item["name"] = tool.name;
  item["annotations"] = json::Object{{"readOnlyHint", tool.readOnlyHint}};
  item["inputSchema"] = tool.inputSchema.isNull()
                            ? json::Value(json::Object{{"type", "object"}})
                            : std::move(tool.inputSchema);
  if (!tool.title.empty()) item["title"] = std::move(tool.title);
  if (!tool.description.empty())
    item["description"] = std::move(tool.description);
  const auto [it, added] = _tools.try_emplace(
      std::move(tool.name), std::move(item), std::move(execute), _tools.size());
  if (!added)
    throw std::invalid_argument("cursiv: a tool named '" + it->first +
                                "' is already registered");
}

void McpServer::registerResource(McpResource resource,
                                 McpResourceFunction read) {
  json::Object item;
  item["uri"] = resource.uri;
  item["name"] = std::move(resource.name);
  if (!resource.description.empty())
    item["description"] = std::move(resource.description);
  if (!resource.mimeType.empty())
    item["mimeType"] = std::move(resource.mimeType);
  const auto [it, added] =
      _resources.try_emplace(std::move(resource.uri), std::move(item),
                             std::move(read), _resources.size());
  if (!added)
    throw std::invalid_argument("cursiv: a resource at '" + it->first +
                                "' is already registered");
}

void McpServer::addInstructions(std::string_view paragraph) {
  if (!_instructions.empty()) _instructions += '\n';
  _instructions += paragraph;
}

std::optional<json::Value> McpServer::handle(
    json::Value message, const CancellationToken& token,
    std::string_view transportVersion) const {
  const auto session = std::atomic_ref(_negotiated);
  const auto* selected = session.load(std::memory_order_relaxed);
  const std::string_view wire =
      transportVersion.empty() && selected ? selected : transportVersion;
  if (!message.isArray() || wire != "2025-03-26" || message.array().empty())
    return handleMessage(message, token, wire, transportVersion.empty());
  json::Array answers;
  for (const auto& item : message.array()) {
    if (auto answer = handleMessage(item, token, wire, false))
      answers.push_back(std::move(*answer));
  }
  return answers.empty() ? std::nullopt
                         : std::optional<json::Value>(std::move(answers));
}

std::optional<json::Value> McpServer::handleMessage(
    const json::Value& message, const CancellationToken& token,
    std::string_view wire, bool rememberVersion) const {
  const auto* id = field(&message, "id");
  const auto* protocol = field(&message, "jsonrpc");
  const auto* methodName = field(&message, "method");
  const auto* params = field(&message, "params");
  const auto* meta = field(params, "_meta");
  const auto* version = field(meta, kVersionKey);
  const auto* capabilities = field(meta, kCapabilitiesKey);
  const std::string_view method = methodName && methodName->isString()
                                      ? std::string_view(methodName->string())
                                      : std::string_view{};
  const bool modern =
      version || capabilities || method == "server/discover" ||
      wire == kProtocol ||
      (wire.empty() && method != "initialize" && method != "ping");
  const auto* unknownId = modern || wire.empty() ? nullptr : &kNullId;
  if (id && !valid_id(*id))
    return error_response(unknownId, rpc::Error::InvalidRequest,
                          "invalid MCP request id");
  const bool jsonRpc = protocol && *protocol == json::Value("2.0");
  if (!modern && jsonRpc && id && !methodName &&
      (field(&message, "result") != nullptr) !=
          (field(&message, "error") != nullptr))
    return std::nullopt;
  if (!jsonRpc || !methodName || !methodName->isString())
    return error_response(id ? id : unknownId, rpc::Error::InvalidRequest,
                          "expected a JSON-RPC 2.0 request");
  if (!id) return std::nullopt;
  const auto unsupported = [&](const json::Value& requested) {
    return error_response(id, -32022, "unsupported MCP protocol version",
                          json::Object{{"requested", requested},
                                       {"supported", supported_versions()}});
  };
  if (modern) {
    if (!version || !version->isString() || !capabilities ||
        !capabilities->isObject())
      return error_response(id, rpc::Error::InvalidParams,
                            "request metadata must include protocolVersion and "
                            "clientCapabilities");
    if (version->string() != kProtocol) return unsupported(*version);
  } else if (!wire.empty() &&
             std::ranges::find(kVersions, wire) == std::end(kVersions)) {
    return unsupported(wire);
  }
  const auto serverInfo = [&] {
    return json::Object{{"name", _name}, {"version", _version}};
  };
  const auto complete = [&](json::Value result) {
    if (!modern) return envelope(id, "result", std::move(result));
    auto& body = result.object();
    body["resultType"] = "complete";
    body["_meta"] =
        json::Object{{"io.modelcontextprotocol/serverInfo", serverInfo()}};
    if (method != "tools/call") {
      body["ttlMs"] = 0;
      body["cacheScope"] = "private";
    }
    return envelope(id, "result", std::move(result));
  };
  try {
    if (method == "server/discover" || (!modern && method == "initialize")) {
      json::Object result{
          {"capabilities", json::Object{{"tools", json::Object{}},
                                        {"resources", json::Object{}}}}};
      if (modern) {
        result["supportedVersions"] = supported_versions();
      } else {
        const auto* requested = field(params, "protocolVersion");
        const auto legacy = std::span(kVersions).subspan(1);
        const auto it = requested && requested->isString()
                            ? std::ranges::find(legacy, requested->string())
                            : legacy.end();
        const char* chosen = it == legacy.end() ? legacy.front() : *it;
        result["protocolVersion"] = chosen;
        result["serverInfo"] = serverInfo();
        if (rememberVersion)
          std::atomic_ref(_negotiated).store(chosen, std::memory_order_relaxed);
      }
      if (!_instructions.empty()) result["instructions"] = _instructions;
      return complete(std::move(result));
    }
    if (!modern && method == "ping") return complete(json::Object{});
    if (method == "tools/list" || method == "resources/list" ||
        method == "resources/templates/list") {
      if (field(params, "cursor"))
        return error_response(id, rpc::Error::InvalidParams,
                              "unknown pagination cursor");
      if (method == "tools/list")
        return complete(list_entries("tools", _tools));
      if (method == "resources/list")
        return complete(list_entries("resources", _resources));
      return complete(json::Object{{"resourceTemplates", json::Array{}}});
    }
    if (method == "tools/call") {
      const json::Value* const name = field(params, "name");
      if (name == nullptr || !name->isString())
        return error_response(id, rpc::Error::InvalidParams,
                              "tools/call names its tool");
      const auto entry = _tools.find(name->string());
      if (entry == _tools.end())
        return error_response(id, rpc::Error::InvalidParams,
                              "unknown tool: " + name->string());
      const json::Value* const arguments = field(params, "arguments");
      if (arguments != nullptr && !arguments->isObject())
        return error_response(id, rpc::Error::InvalidParams,
                              "tool arguments must be an object");
      McpToolResult outcome;
      try {
        const json::Value empty(json::Object{});
        outcome = entry->second.handle(arguments ? *arguments : empty, token);
      } catch (const std::exception& error) {
        outcome = {error.what(), true};
      }
      json::Object result;
      result["content"] = text_content(std::move(outcome.text));
      if (outcome.isError) result["isError"] = json::Value(true);
      return complete(std::move(result));
    }
    if (method == "resources/read") {
      const json::Value* const uri = field(params, "uri");
      if (uri == nullptr || !uri->isString())
        return error_response(id, rpc::Error::InvalidParams,
                              "resources/read names its uri");
      const auto entry = _resources.find(uri->string());
      if (entry == _resources.end())
        return error_response(id, modern ? rpc::Error::InvalidParams : -32002,
                              "resource not found: " + uri->string());
      json::Object item;
      item["uri"] = *uri;
      if (const auto* mimeType = entry->second.descriptor.find("mimeType"))
        item["mimeType"] = *mimeType;
      item["text"] = entry->second.handle();
      json::Array contents;
      contents.emplace_back(std::move(item));
      json::Object result;
      result["contents"] = std::move(contents);
      return complete(std::move(result));
    }
    return error_response(id, rpc::Error::MethodNotFound,
                          "unknown method: " + methodName->string());
  } catch (const std::exception& error) {
    return error_response(id, rpc::Error::InternalError, error.what());
  } catch (...) {
    return error_response(id, rpc::Error::InternalError, "MCP request failed");
  }
}

std::optional<std::string> McpServer::handleText(
    std::string_view text, const CancellationToken& token) const {
  json::Value message;
  auto answer = parse_message(text, message);
  if (!answer)
    answer = handle(std::move(message), token);
  else if (std::atomic_ref(_negotiated).load(std::memory_order_relaxed))
    answer->object()["id"] = nullptr;
  return answer ? std::optional(json::stringify(*answer)) : std::nullopt;
}

bool McpServer::serveStdio(std::FILE* input, std::FILE* output) const {
  std::mutex mutex;
  std::unordered_map<RequestId, std::stop_source> pending;
  const auto write = [&](const json::Value& answer) {
    const std::string text = json::stringify(answer) + '\n';
    if (std::fwrite(text.data(), 1, text.size(), output) == text.size())
      std::fflush(output);
  };
  lsp::ThreadPool worker(0, 1);
  for (std::string line; read_line(input, line);) {
    if (line.empty()) continue;
    json::Value message;
    auto error = parse_message(line, message);
    const auto* id = field(&message, "id");
    const auto* method = field(&message, "method");
    const std::scoped_lock lock(mutex);
    if (std::ferror(output) != 0) break;
    const auto* protocol = field(&message, "jsonrpc");
    if (!id && method && *method == json::Value("notifications/cancelled") &&
        protocol && *protocol == json::Value("2.0")) {
      const auto* target = field(field(&message, "params"), "requestId");
      if (target && valid_id(*target)) {
        if (const auto it = pending.find(request_key(*target));
            it != pending.end())
          it->second.request_stop();
      }
      continue;
    }
    RequestId key;
    std::stop_token token;
    if (id && valid_id(*id)) {
      key = request_key(*id);
      const auto [it, fresh] = pending.try_emplace(key);
      if (!fresh) {
        write(error_response(id, rpc::Error::InvalidRequest,
                             "request id is already in use"));
        continue;
      }
      token = it->second.get_token();
    }
    worker.addTask([&, key = std::move(key), token, answer = std::move(error),
                    message = std::move(message)]() mutable {
      if (answer &&
          std::atomic_ref(_negotiated).load(std::memory_order_relaxed))
        answer->object()["id"] = nullptr;
      if (!answer && !token.stop_requested())
        answer = handle(std::move(message), token);
      const std::scoped_lock finished(mutex);
      if (token.stop_possible()) pending.erase(key);
      if (answer && !token.stop_requested() && std::ferror(output) == 0)
        write(*answer);
    });
  }
  worker.waitUntilFinished();
  return std::ferror(output) == 0;
}

}  // namespace cursiv
