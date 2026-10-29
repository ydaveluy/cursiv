#include "cursiv/mcp/McpServer.hpp"

#include <lsp/json/json.h>
#include <lsp/jsonrpc/jsonrpc.h>

#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace cursiv {

namespace {

namespace json = lsp::json;
namespace rpc = lsp::jsonrpc;

constexpr std::string_view kLatestProtocol = "2025-06-18";
constexpr std::string_view kKnownProtocols[] = {"2024-11-05", "2025-03-26",
                                                "2025-06-18"};
// resources/read on a URI nobody registered; the code the spec reserves.
constexpr json::Integer kResourceNotFound = -32002;

json::Value envelope(const rpc::MessageId& id, std::string_view member,
                     json::Value value) {
  json::Object message;
  message["jsonrpc"] = json::Value(json::String("2.0"));
  message["id"] = std::visit([](const auto& v) { return json::Value(v); }, id);
  message[member] = std::move(value);
  return json::Value(std::move(message));
}

json::Value response(const rpc::MessageId& id, json::Value result) {
  return envelope(id, "result", std::move(result));
}

json::Value error_response(const rpc::MessageId& id, json::Integer code,
                           json::String message) {
  json::Object error;
  error["code"] = json::Value(code);
  error["message"] = json::Value(std::move(message));
  return envelope(id, "error", json::Value(std::move(error)));
}

const json::Value* find_param(const std::optional<json::Value>& params,
                              std::string_view key) {
  if (!params.has_value() || !params->isObject()) return nullptr;
  return params->object().find(key);
}

json::Value text_content(std::string&& text) {
  json::Object item;
  item["type"] = json::Value(json::String("text"));
  item["text"] = json::Value(std::move(text));
  json::Array content;
  content.emplace_back(std::move(item));
  return json::Value(std::move(content));
}

}  // namespace

McpServer::McpServer(std::string name, std::string version)
    : _name(std::move(name)), _version(std::move(version)) {}

void McpServer::registerTool(McpTool tool, McpToolFunction execute) {
  _tools.emplace_back(std::move(tool), std::move(execute));
}

void McpServer::registerResource(McpResource resource,
                                 McpResourceFunction read) {
  _resources.emplace_back(std::move(resource), std::move(read));
}

void McpServer::addInstructions(std::string_view paragraph) {
  if (!_instructions.empty()) _instructions += '\n';
  _instructions += paragraph;
}

json::Value McpServer::initializeResult(
    const std::optional<json::Value>& params) const {
  // The requested version when it is a known one, the latest otherwise.
  json::String protocol{kLatestProtocol};
  if (const json::Value* const v = find_param(params, "protocolVersion");
      v != nullptr && v->isString())
    for (const std::string_view known : kKnownProtocols)
      if (v->string() == known) protocol = v->string();
  json::Object capabilities;
  capabilities["tools"] = json::Value(json::Object{});
  capabilities["resources"] = json::Value(json::Object{});
  json::Object serverInfo;
  serverInfo["name"] = json::Value(json::String(_name));
  serverInfo["version"] = json::Value(json::String(_version));
  json::Object result;
  result["protocolVersion"] = json::Value(std::move(protocol));
  result["capabilities"] = json::Value(std::move(capabilities));
  result["serverInfo"] = json::Value(std::move(serverInfo));
  if (!_instructions.empty())
    result["instructions"] = json::Value(json::String(_instructions));
  return json::Value(std::move(result));
}

json::Value McpServer::listTools() const {
  json::Array tools;
  for (const ToolEntry& entry : _tools) {
    json::Object tool;
    tool["name"] = json::Value(json::String(entry.tool.name));
    if (!entry.tool.title.empty())
      tool["title"] = json::Value(json::String(entry.tool.title));
    if (!entry.tool.description.empty())
      tool["description"] = json::Value(json::String(entry.tool.description));
    if (entry.tool.inputSchema.isNull()) {
      json::Object schema;
      schema["type"] = json::Value(json::String("object"));
      tool["inputSchema"] = json::Value(std::move(schema));
    } else {
      tool["inputSchema"] = entry.tool.inputSchema;
    }
    json::Object annotations;
    annotations["readOnlyHint"] = json::Value(entry.tool.readOnlyHint);
    tool["annotations"] = json::Value(std::move(annotations));
    tools.emplace_back(std::move(tool));
  }
  json::Object result;
  result["tools"] = json::Value(std::move(tools));
  return json::Value(std::move(result));
}

json::Value McpServer::listResources() const {
  json::Array resources;
  for (const ResourceEntry& entry : _resources) {
    json::Object resource;
    resource["uri"] = json::Value(json::String(entry.resource.uri));
    resource["name"] = json::Value(json::String(entry.resource.name));
    if (!entry.resource.description.empty())
      resource["description"] =
          json::Value(json::String(entry.resource.description));
    if (!entry.resource.mimeType.empty())
      resource["mimeType"] = json::Value(json::String(entry.resource.mimeType));
    resources.emplace_back(std::move(resource));
  }
  json::Object result;
  result["resources"] = json::Value(std::move(resources));
  return json::Value(std::move(result));
}

std::optional<json::Value> McpServer::handle(
    json::Value message, const CancellationToken& token) const {
  if (!message.isObject())
    return error_response(nullptr, rpc::Error::InvalidRequest,
                          message.isArray() ? "batches are not supported"
                                            : "a message is a json object");
  rpc::Message parsed;
  try {
    parsed = rpc::messageFromJson(std::move(message.object()));
  } catch (const rpc::ProtocolError& error) {
    return error_response(nullptr, rpc::Error::InvalidRequest, error.what());
  }
  auto* const request = std::get_if<rpc::Request>(&parsed);
  // The server sends no requests of its own, so a response has no home; a
  // notification answers nothing by construction.
  if (request == nullptr || request->isNotification()) return std::nullopt;
  const rpc::MessageId& id = *request->id;
  const std::string& method = request->method;
  try {
    if (method == "initialize")
      return response(id, initializeResult(request->params));
    if (method == "ping") return response(id, json::Value(json::Object{}));
    if (method == "tools/list") return response(id, listTools());
    if (method == "tools/call") {
      const json::Value* const name = find_param(request->params, "name");
      if (name == nullptr || !name->isString())
        return error_response(id, rpc::Error::InvalidParams,
                              "tools/call names its tool");
      for (const ToolEntry& entry : _tools) {
        if (entry.tool.name != name->string()) continue;
        json::Value arguments;
        if (const json::Value* const a =
                find_param(request->params, "arguments");
            a != nullptr)
          arguments = *a;
        McpToolResult outcome;
        try {
          outcome = entry.execute(arguments, token);
        } catch (const std::exception& error) {
          outcome = {error.what(), true};
        }
        json::Object result;
        result["content"] = text_content(std::move(outcome.text));
        if (outcome.isError) result["isError"] = json::Value(true);
        return response(id, json::Value(std::move(result)));
      }
      return error_response(id, rpc::Error::InvalidParams,
                            "unknown tool: " + name->string());
    }
    if (method == "resources/list") return response(id, listResources());
    if (method == "resources/read") {
      const json::Value* const uri = find_param(request->params, "uri");
      if (uri == nullptr || !uri->isString())
        return error_response(id, rpc::Error::InvalidParams,
                              "resources/read names its uri");
      for (const ResourceEntry& entry : _resources) {
        if (entry.resource.uri != uri->string()) continue;
        json::Object item;
        item["uri"] = json::Value(json::String(entry.resource.uri));
        if (!entry.resource.mimeType.empty())
          item["mimeType"] = json::Value(json::String(entry.resource.mimeType));
        item["text"] = json::Value(entry.read());
        json::Array contents;
        contents.emplace_back(std::move(item));
        json::Object result;
        result["contents"] = json::Value(std::move(contents));
        return response(id, json::Value(std::move(result)));
      }
      return error_response(id, kResourceNotFound,
                            "resource not found: " + uri->string());
    }
    return error_response(id, rpc::Error::MethodNotFound,
                          "unknown method: " + method);
  } catch (const std::exception& error) {
    return error_response(id, rpc::Error::InternalError, error.what());
  }
}

std::optional<std::string> McpServer::handleText(
    std::string_view text, const CancellationToken& token) const {
  json::Value message;
  try {
    message = json::parse(text);
  } catch (const std::exception& error) {
    // Not just `ParseError`: a number the parser hands to `std::stod` answers
    // `out_of_range`, and this transport has no caller left to unwind into.
    return json::stringify(
        error_response(nullptr, rpc::Error::ParseError, error.what()));
  }
  const std::optional<json::Value> response = handle(std::move(message), token);
  if (!response.has_value()) return std::nullopt;
  return json::stringify(*response);
}

}  // namespace cursiv
