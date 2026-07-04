#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <concepts>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <pegium/core/services/JsonValue.hpp>
#include <pegium/core/services/Diagnostic.hpp>
#include <pegium/core/syntax-tree/AstNode.hpp>
#include <pegium/core/utils/FunctionRef.hpp>
#include <pegium/core/validation/DiagnosticRanges.hpp>

namespace pegium::validation {

class ValidationAcceptor;
template <typename Node> class ValidationDiagnosticBuilder;

/// Fluent base builder used to enrich one diagnostic before emission.
template <typename Derived> class ValidationDiagnosticBuilderBase {
public:
  ValidationDiagnosticBuilderBase(const ValidationAcceptor &acceptor,
                                  pegium::DiagnosticSeverity severity,
                                  std::string_view message,
                                  const AstNode &node);

  ValidationDiagnosticBuilderBase(const ValidationDiagnosticBuilderBase &) =
      delete;
  ValidationDiagnosticBuilderBase &
  operator=(const ValidationDiagnosticBuilderBase &) = delete;

  ValidationDiagnosticBuilderBase(
      ValidationDiagnosticBuilderBase &&other) noexcept
      : _acceptor(other._acceptor), _node(other._node),
        _severity(other._severity), _message(std::move(other._message)),
        _code(std::move(other._code)),
        _codeDescription(std::move(other._codeDescription)),
        _tags(std::move(other._tags)),
        _relatedInformation(std::move(other._relatedInformation)),
        _data(std::move(other._data)), _begin(other._begin), _end(other._end),
        _pending(other._pending) {
    other._acceptor = nullptr;
    other._pending = false;
  }

  ValidationDiagnosticBuilderBase &
  operator=(ValidationDiagnosticBuilderBase &&other) noexcept {
    if (this == &other) {
      return *this;
    }

    emitNoexcept();

    _acceptor = other._acceptor;
    _node = other._node;
    _severity = other._severity;
    _message = std::move(other._message);
    _code = std::move(other._code);
    _codeDescription = std::move(other._codeDescription);
    _tags = std::move(other._tags);
    _relatedInformation = std::move(other._relatedInformation);
    _data = std::move(other._data);
    _begin = other._begin;
    _end = other._end;
    _pending = other._pending;

    other._acceptor = nullptr;
    other._pending = false;
    return *this;
  }

  ~ValidationDiagnosticBuilderBase() noexcept { emitNoexcept(); }

  Derived &range(TextOffset begin, TextOffset end) noexcept {
    const auto baseBegin = _begin;
    const auto baseEnd = _end;
    const auto selectedBegin = std::min(baseBegin + begin, baseEnd);
    const auto selectedEnd = std::min(baseBegin + end, baseEnd);
    _begin = selectedBegin;
    _end = std::max(selectedBegin, selectedEnd);
    return derived();
  }

  Derived &code(std::string_view codeValue,
                std::optional<std::string_view> description =
                    std::nullopt) {
    _code = pegium::DiagnosticCode(std::string(codeValue));
    _codeDescription =
        description.has_value() ? std::optional<std::string>(std::string(*description))
                                : std::nullopt;
    return derived();
  }

  Derived &tags(std::initializer_list<pegium::DiagnosticTag> tags) {
    _tags.assign(tags.begin(), tags.end());
    return derived();
  }

  Derived &relatedInformation(TextOffset begin, TextOffset end,
                                  std::string_view message,
                                  std::string_view uri = {}) {
    _relatedInformation.push_back(pegium::DiagnosticRelatedInformation{
        .uri = std::string(uri),
        .message = std::string(message),
        .begin = begin,
        .end = std::max(begin, end)});
    return derived();
  }

  Derived &relatedInformation(const AstNode &node, std::string_view message,
                                  std::string_view uri = {}) {
    const auto [begin, end] = range_of(node);
    return relatedInformation(begin, end, message, uri);
  }

  Derived &relatedInformation(
      std::initializer_list<pegium::DiagnosticRelatedInformation> entries) {
    _relatedInformation.insert(_relatedInformation.end(), entries.begin(),
                               entries.end());
    return derived();
  }

  Derived &relatedInformation(
      std::span<const pegium::DiagnosticRelatedInformation> entries) {
    _relatedInformation.insert(_relatedInformation.end(), entries.begin(),
                               entries.end());
    return derived();
  }

  Derived &data(pegium::JsonValue dataValue) {
    _data = std::move(dataValue);
    return derived();
  }

  /// Anchors the diagnostic on the `index`-th `keyword` literal parsed for the
  /// node (e.g. `.keyword("extends")` underlines just that keyword instead of
  /// the whole node). Keeps the whole-node range when the keyword is absent
  /// from the node's CST (an optional keyword that was not written).
  Derived &keyword(std::string_view keyword, std::size_t index = 0) noexcept {
    if (const auto *anchorNode = node(); anchorNode != nullptr) {
      setRange(range_for_keyword(*anchorNode, keyword, index));
    }
    return derived();
  }

protected:
  void emit();
  void emitNoexcept() noexcept;

  [[nodiscard]] const AstNode *node() const noexcept { return _node; }

  void setRange(std::pair<TextOffset, TextOffset> range) noexcept {
    std::tie(_begin, _end) = range;
  }

private:
  Derived &derived() noexcept { return static_cast<Derived &>(*this); }

  const ValidationAcceptor *_acceptor = nullptr;
  const AstNode *_node = nullptr;
  pegium::DiagnosticSeverity _severity =
      pegium::DiagnosticSeverity::Error;
  std::string _message;
  std::optional<pegium::DiagnosticCode> _code;
  std::optional<std::string> _codeDescription;
  std::vector<pegium::DiagnosticTag> _tags;
  std::vector<pegium::DiagnosticRelatedInformation> _relatedInformation;
  std::optional<pegium::JsonValue> _data;
  TextOffset _begin = 0;
  TextOffset _end = 0;
  bool _pending = true;
};

template <typename Node>
/// Fluent diagnostic builder bound to one AST node type.
class ValidationDiagnosticBuilder final
    : public ValidationDiagnosticBuilderBase<ValidationDiagnosticBuilder<Node>> {
public:
  using ValidationDiagnosticBuilderBase<
      ValidationDiagnosticBuilder<Node>>::ValidationDiagnosticBuilderBase;

  template <auto Feature>
    requires pegium::detail::AstNodeFeature<Node, Feature>
  ValidationDiagnosticBuilder &property() noexcept {
    const auto &typedNode = static_cast<const Node &>(*this->node());
    this->setRange(range_for_feature<Feature>(typedNode));
    return *this;
  }

  template <auto Feature>
    requires pegium::detail::VectorAstNodeFeature<Node, Feature>
  ValidationDiagnosticBuilder &property(std::size_t index) noexcept {
    const auto &typedNode = static_cast<const Node &>(*this->node());
    this->setRange(range_for_feature<Feature>(typedNode, index));
    return *this;
  }
};

/// Callback adapter used by validation checks to emit diagnostics.
///
/// Holds a non-owning `function_ref` to the diagnostic sink. The callback
/// must outlive the acceptor — typically a stack-allocated lambda in
/// `validateDocument` does the trick.
class ValidationAcceptor {
public:
  using Callback = utils::function_ref<void(pegium::Diagnostic)>;

  ValidationAcceptor() = default;

  explicit ValidationAcceptor(Callback callback) noexcept
      : _callback(callback) {}

  void operator()(pegium::Diagnostic diagnostic) const {
    if (_callback) {
      _callback(std::move(diagnostic));
    }
  }


  template <typename Node>
    requires std::derived_from<std::remove_cvref_t<Node>, AstNode>
  ValidationDiagnosticBuilder<std::remove_cvref_t<Node>>
  error(const Node &node, std::string_view message) const {
    return ValidationDiagnosticBuilder<std::remove_cvref_t<Node>>(
        *this, pegium::DiagnosticSeverity::Error, message, node);
  }

  template <typename Node>
    requires std::derived_from<std::remove_cvref_t<Node>, AstNode>
  ValidationDiagnosticBuilder<std::remove_cvref_t<Node>>
  warning(const Node &node, std::string_view message) const {
    return ValidationDiagnosticBuilder<std::remove_cvref_t<Node>>(
        *this, pegium::DiagnosticSeverity::Warning, message, node);
  }

  template <typename Node>
    requires std::derived_from<std::remove_cvref_t<Node>, AstNode>
  ValidationDiagnosticBuilder<std::remove_cvref_t<Node>>
  info(const Node &node, std::string_view message) const {
    return ValidationDiagnosticBuilder<std::remove_cvref_t<Node>>(
        *this, pegium::DiagnosticSeverity::Information, message, node);
  }

  template <typename Node>
    requires std::derived_from<std::remove_cvref_t<Node>, AstNode>
  ValidationDiagnosticBuilder<std::remove_cvref_t<Node>>
  hint(const Node &node, std::string_view message) const {
    return ValidationDiagnosticBuilder<std::remove_cvref_t<Node>>(
        *this, pegium::DiagnosticSeverity::Hint, message, node);
  }

private:
  Callback _callback;
};

template <typename Derived>
inline ValidationDiagnosticBuilderBase<Derived>::ValidationDiagnosticBuilderBase(
    const ValidationAcceptor &acceptor, pegium::DiagnosticSeverity severity,
    std::string_view message, const AstNode &node)
    : _acceptor(&acceptor), _node(&node), _severity(severity), _message(message) {
  std::tie(_begin, _end) = range_of(node);
}

template <typename Derived>
inline void ValidationDiagnosticBuilderBase<Derived>::emit() {
  if (!_pending || _acceptor == nullptr) {
    return;
  }

  pegium::Diagnostic diagnostic{
      .severity = _severity,
      .message = std::move(_message),
      .code = std::move(_code),
      .codeDescription = std::move(_codeDescription),
      .tags = std::move(_tags),
      .relatedInformation = std::move(_relatedInformation),
      .data = std::move(_data),
      .begin = _begin,
      .end = _end >= _begin ? _end : _begin};
  _pending = false;
  _acceptor->operator()(std::move(diagnostic));
}

template <typename Derived>
inline void ValidationDiagnosticBuilderBase<Derived>::emitNoexcept() noexcept {
  try {
    emit();
  } catch (...) {
    _pending = false;
  }
}

} // namespace pegium::validation
