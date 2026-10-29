#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/syntax-tree/MemberTraits.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

// What a check emits and where it lands. The sink and document are borrowed
// for one validation pass; indexed anchors are cached only for its current node.

namespace cursiv {

struct Document;
class ValidationAcceptor;

// Two implementation details of this header, callable from nowhere else: the
// class a member pointer writes into, and the CRTP half the two builders share.
namespace detail {

// The owner class a member pointer writes into.
template <auto Member>
using MemberOwner =
    typename cursiv::member_traits<decltype(Member)>::class_type;

// The fluent base: everything every arm can say. `Derived` is the CRTP leaf,
// so each setter returns the most-derived builder and `.property` stays
// chainable after `.code`.
template <class Derived>
class ValidationDiagnosticBuilderBase {
 public:
  ValidationDiagnosticBuilderBase(const ValidationAcceptor& acceptor,
                                  DiagnosticSeverity severity,
                                  std::string_view message,
                                  const AstNode& node);
  ValidationDiagnosticBuilderBase(const ValidationAcceptor& acceptor,
                                  DiagnosticSeverity severity,
                                  std::string_view message, TextRange span);

  ValidationDiagnosticBuilderBase(const ValidationDiagnosticBuilderBase&) =
      delete;
  ValidationDiagnosticBuilderBase& operator=(
      const ValidationDiagnosticBuilderBase&) = delete;

  // Move hands the emission over: the source is disarmed, the destination emits
  // at its own destruction, and a diagnostic still goes out exactly once.
  ValidationDiagnosticBuilderBase(
      ValidationDiagnosticBuilderBase&& other) noexcept
      : _acceptor(other._acceptor),
        _node(other._node),
        _severity(other._severity),
        _message(std::move(other._message)),
        _code(std::move(other._code)),
        _codeDescription(std::move(other._codeDescription)),
        _tags(std::move(other._tags)),
        _relatedInformation(std::move(other._relatedInformation)),
        _data(std::move(other._data)),
        _range(other._range),
        _pending(other._pending) {
    other._acceptor = nullptr;
    other._pending = false;
  }
  ValidationDiagnosticBuilderBase& operator=(
      ValidationDiagnosticBuilderBase&& other) noexcept {
    if (this == &other) return *this;
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
    _range = other._range;
    _pending = other._pending;
    other._acceptor = nullptr;
    other._pending = false;
    return *this;
  }

  // The emission point. During unwinding it still emits what the chain had said
  // so far, and it never throws.
  ~ValidationDiagnosticBuilderBase() noexcept { emitNoexcept(); }

  // Absolute document offsets, the pair every diagnostic is published as.
  Derived& range(TextRange span) noexcept {
    _range = span;
    return derived();
  }

  // LSP's `integer | string` code, with an optional human link beside it.
  Derived& code(std::string_view value,
                std::optional<std::string_view> description = std::nullopt) {
    _code = DiagnosticCode(value);
    setCodeDescription(description);
    return derived();
  }
  Derived& code(std::int64_t value,
                std::optional<std::string_view> description = std::nullopt) {
    _code = DiagnosticCode(value);
    setCodeDescription(description);
    return derived();
  }

  Derived& tags(std::initializer_list<DiagnosticTag> tags) {
    _tags.assign(tags.begin(), tags.end());
    return derived();
  }

  // The four ways to point somewhere else as well: a span, a node, or ready
  // rows one by one or in bulk. `uri` empty means this same document.
  Derived& relatedInformation(TextRange span, std::string_view message,
                              std::string_view uri = {}) {
    _relatedInformation.emplace_back(std::string(uri), std::string(message),
                                     span);
    return derived();
  }
  Derived& relatedInformation(const AstNode& node, std::string_view message,
                              std::string_view uri = {});
  Derived& relatedInformation(
      std::initializer_list<DiagnosticRelatedInformation> entries) {
    _relatedInformation.insert(_relatedInformation.end(), entries.begin(),
                               entries.end());
    return derived();
  }
  Derived& relatedInformation(
      std::span<const DiagnosticRelatedInformation> entries) {
    _relatedInformation.insert(_relatedInformation.end(), entries.begin(),
                               entries.end());
    return derived();
  }

  // The machine payload round-tripped into `codeAction`.
  Derived& data(JsonValue value) {
    _data = std::move(value);
    return derived();
  }

  // Anchors on the `index`-th spelling of `keyword` inside the node. Keeps the
  // current range when the keyword is absent, or when this builder was made
  // over a bare span.
  Derived& keyword(std::string_view keyword, std::size_t index = 0) noexcept;

 protected:
  [[nodiscard]] const AstNode* node() const noexcept { return _node; }
  [[nodiscard]] const ValidationAcceptor* acceptor() const noexcept {
    return _acceptor;
  }
  void setRange(TextRange span) noexcept { _range = span; }

 private:
  Derived& derived() noexcept { return static_cast<Derived&>(*this); }

  void setCodeDescription(std::optional<std::string_view> description) {
    if (description.has_value())
      _codeDescription = std::string(*description);
    else
      _codeDescription = std::nullopt;
  }

  void emit();
  void emitNoexcept() noexcept {
    try {
      emit();
    } catch (...) {
      _pending = false;
    }
  }

  const ValidationAcceptor* _acceptor = nullptr;
  const AstNode* _node = nullptr;
  DiagnosticSeverity _severity = DiagnosticSeverity::Error;
  std::string _message;
  std::optional<DiagnosticCode> _code;
  std::optional<std::string> _codeDescription;
  std::vector<DiagnosticTag> _tags;
  std::vector<DiagnosticRelatedInformation> _relatedInformation;
  std::optional<JsonValue> _data;
  TextRange _range{};
  bool _pending = true;
};
}  // namespace detail

// The node-typed leaf: knowing `Node` is what makes `.property<&T::m>` a
// member-pointer anchor rather than a string.
template <class Node>
class ValidationDiagnosticBuilder final
    : public detail::ValidationDiagnosticBuilderBase<
          ValidationDiagnosticBuilder<Node>> {
 public:
  using detail::ValidationDiagnosticBuilderBase<
      ValidationDiagnosticBuilder<Node>>::ValidationDiagnosticBuilderBase;

  // Anchors on the record that wrote this member -- the `index`-th for a list
  // member, source order, clamped to the last. When nothing wrote it, or the
  // provider cannot answer, the current range stays.
  template <auto Member, class Owner = detail::MemberOwner<Member>>
    requires std::derived_from<Node, Owner>
  ValidationDiagnosticBuilder& property(std::size_t index = 0) noexcept {
    static constexpr std::string_view kMember =
        cursiv::detail::member_identifier<Member>();
    this->anchorOnFeature(kMember, index);
    return *this;
  }

 private:
  void anchorOnFeature(std::string_view member, std::size_t index) noexcept;
};

class ValidationAcceptor {
 public:
  // The collector must outlive the acceptor. By rvalue, and every arm below
  // with it: a `Diagnostic` is hundreds of bytes of optionals and vectors, and
  // building one per hop -- emit, accept, the sink, the collector -- copies
  // them all four times.
  using Sink = cursiv::function_ref<void(Diagnostic&&)>;

  // Precondition: `document` keeps its `ParseResult` for the acceptor's life,
  // which the validating phase guarantees. On a tree that names no grammar the
  // `.property`/`.keyword` anchors degrade to the node's span.
  ValidationAcceptor(Sink sink, const Document& document,
                     std::string_view source) noexcept;

  // The four severity arms, each a builder. The three-argument spelling
  // stamps the code on the way in.
  template <class Node>
    requires std::derived_from<Node, AstNode>
  ValidationDiagnosticBuilder<Node> error(const Node& node,
                                          std::string_view message,
                                          std::string_view code = {}) const {
    return make(node, DiagnosticSeverity::Error, message, code);
  }
  template <class Node>
    requires std::derived_from<Node, AstNode>
  ValidationDiagnosticBuilder<Node> warning(const Node& node,
                                            std::string_view message,
                                            std::string_view code = {}) const {
    return make(node, DiagnosticSeverity::Warning, message, code);
  }
  template <class Node>
    requires std::derived_from<Node, AstNode>
  ValidationDiagnosticBuilder<Node> information(
      const Node& node, std::string_view message,
      std::string_view code = {}) const {
    return make(node, DiagnosticSeverity::Information, message, code);
  }
  template <class Node>
    requires std::derived_from<Node, AstNode>
  ValidationDiagnosticBuilder<Node> hint(const Node& node,
                                         std::string_view message,
                                         std::string_view code = {}) const {
    return make(node, DiagnosticSeverity::Hint, message, code);
  }

  // ...and over a span the check computed itself. No node, so no
  // `.property`/`.keyword`; everything else chains.
  ValidationDiagnosticBuilder<AstNode> error(TextRange span,
                                             std::string_view message,
                                             std::string_view code = {}) const {
    return make(span, DiagnosticSeverity::Error, message, code);
  }
  ValidationDiagnosticBuilder<AstNode> warning(
      TextRange span, std::string_view message,
      std::string_view code = {}) const {
    return make(span, DiagnosticSeverity::Warning, message, code);
  }
  ValidationDiagnosticBuilder<AstNode> information(
      TextRange span, std::string_view message,
      std::string_view code = {}) const {
    return make(span, DiagnosticSeverity::Information, message, code);
  }
  ValidationDiagnosticBuilder<AstNode> hint(TextRange span,
                                            std::string_view message,
                                            std::string_view code = {}) const {
    return make(span, DiagnosticSeverity::Hint, message, code);
  }

  // The immediate arms: what the runtime itself calls, and the funnel every
  // builder emits through -- the one place the source is stamped.
  void accept(const AstNode& node, DiagnosticSeverity severity,
              std::string_view message, std::string_view code = {}) const;
  void accept(TextRange span, DiagnosticSeverity severity,
              std::string_view message, std::string_view code = {}) const;
  void accept(Diagnostic&& diagnostic) const;

  // The CST span of a node, `{0, 0}` for one no CST node produced.
  [[nodiscard]] TextRange spanOf(const AstNode& node) const noexcept;
  // The span of a name the node borrows from the document (or from its
  // translated text), and the node's own span when the view does not slice it.
  [[nodiscard]] TextRange spanOfName(std::string_view borrowed,
                                     const AstNode& node) const noexcept;

  // The CST anchors behind `.property`/`.keyword`. `nullopt` means "no better
  // anchor": the caller keeps what it has.
  [[nodiscard]] std::optional<TextRange> featureSpan(
      const AstNode& node, std::string_view member,
      std::size_t index) const noexcept;
  [[nodiscard]] std::optional<TextRange> keywordSpan(
      const AstNode& node, std::string_view keyword,
      std::size_t index) const noexcept;

 private:
  struct Anchor {
    std::string name;
    bool keyword;
    std::vector<CstNodeView> nodes;
  };

  std::optional<TextRange> anchorSpan(const AstNode& node,
                                     std::string_view name, std::size_t index,
                                     bool keyword) const noexcept;
  // The CST a node was read into, null where it was read into none.
  const Cst* cstOf(const AstNode& node) const noexcept;

  template <class Node>
  ValidationDiagnosticBuilder<Node> make(const Node& node,
                                         DiagnosticSeverity severity,
                                         std::string_view message,
                                         std::string_view code) const {
    ValidationDiagnosticBuilder<Node> builder(*this, severity, message, node);
    if (!code.empty()) builder.code(code);
    return builder;
  }
  ValidationDiagnosticBuilder<AstNode> make(TextRange span,
                                            DiagnosticSeverity severity,
                                            std::string_view message,
                                            std::string_view code) const {
    ValidationDiagnosticBuilder<AstNode> builder(*this, severity, message,
                                                 span);
    if (!code.empty()) builder.code(code);
    return builder;
  }

  Sink _sink;
  const Cst* _cst = nullptr;
  std::string_view _text;
  std::string_view _source;
  // A check commonly visits every index of one list. Keep that node's
  // collections through its checks, releasing them when another node is asked.
  mutable NodeId _anchorNode = kNoNode;
  mutable const Cst* _anchorCst = nullptr;
  mutable std::vector<Anchor> _anchors;
};

// Builder bodies that need the acceptor's surface.

template <class Derived>
inline detail::ValidationDiagnosticBuilderBase<Derived>::
    ValidationDiagnosticBuilderBase(const ValidationAcceptor& acceptor,
                                    DiagnosticSeverity severity,
                                    std::string_view message,
                                    const AstNode& node)
    : _acceptor(&acceptor),
      _node(&node),
      _severity(severity),
      _message(message),
      _range(acceptor.spanOf(node)) {}

template <class Derived>
inline detail::ValidationDiagnosticBuilderBase<Derived>::
    ValidationDiagnosticBuilderBase(const ValidationAcceptor& acceptor,
                                    DiagnosticSeverity severity,
                                    std::string_view message, TextRange span)
    : _acceptor(&acceptor),
      _severity(severity),
      _message(message),
      _range(span) {}

template <class Derived>
inline Derived&
detail::ValidationDiagnosticBuilderBase<Derived>::relatedInformation(
    const AstNode& node, std::string_view message, std::string_view uri) {
  return relatedInformation(
      _acceptor == nullptr ? TextRange{} : _acceptor->spanOf(node), message,
      uri);
}

template <class Derived>
inline Derived& detail::ValidationDiagnosticBuilderBase<Derived>::keyword(
    std::string_view keyword, std::size_t index) noexcept {
  if (_acceptor != nullptr && _node != nullptr)
    if (const std::optional<TextRange> found =
            _acceptor->keywordSpan(*_node, keyword, index);
        found.has_value())
      _range = *found;
  return derived();
}

template <class Derived>
inline void detail::ValidationDiagnosticBuilderBase<Derived>::emit() {
  if (!_pending || _acceptor == nullptr) return;
  _pending = false;
  Diagnostic diagnostic{
      TextRange{_range.begin, std::max(_range.begin, _range.end)},
      _severity,
      std::move(_message),
      {},
      std::move(_code),
      std::move(_codeDescription),
      std::move(_tags),
      std::move(_relatedInformation),
      std::move(_data)};
  _acceptor->accept(std::move(diagnostic));
}

template <class Node>
inline void ValidationDiagnosticBuilder<Node>::anchorOnFeature(
    std::string_view member, std::size_t index) noexcept {
  if (this->acceptor() != nullptr && this->node() != nullptr)
    if (const std::optional<TextRange> found =
            this->acceptor()->featureSpan(*this->node(), member, index);
        found.has_value())
      this->setRange(*found);
}

}  // namespace cursiv
