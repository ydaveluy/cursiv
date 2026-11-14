#pragma once

#include <lsp/messages.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/formatting/Formatter.hpp"
#include "cursiv/formatting/FormattingAction.hpp"
#include "cursiv/formatting/FormattingRules.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstUtils.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

// What a language's author writes, and the base that turns it into edits.
// Dispatch uses `node->type`, the exact runtime type.

namespace cursiv {

template <class NodeT>
class NodeFormatter {
  // The AST class that declares the member `Ptr` points to: a rule may name a
  // member of its node's class or of one of its bases.
  template <auto Ptr>
  using property_owner = typename detail::field_tag<Ptr>::owner;

 public:
  NodeFormatter(const Document& document, const AstNode* node,
                FormattingCollector* collector) noexcept
      : _cst(&document.parseResult.cst()),
        _document(&document),
        _node(node),
        _collector(collector) {}

  // The record a node was built from -- the construction's own, or any node
  // already in hand.
  [[nodiscard]] FormattingRegion node(const AstNode& node) const {
    return FormattingRegion(node.cstNodeId, _cst, _collector);
  }

  [[nodiscard]] FormattingRegion nodes(
      std::span<const AstNode* const> nodes) const {
    std::vector<NodeId> found;
    for (const AstNode* const node : nodes)
      if (node != nullptr && node->cstNodeId != kNoNode)
        found.push_back(node->cstNodeId);
    return FormattingRegion(_cst, std::move(found), _collector);
  }

  // The record that writes `Feature`; the first when the member is a list.
  template <auto Feature>
    requires std::derived_from<NodeT, property_owner<Feature>>
  [[nodiscard]] FormattingRegion property() const {
    return property<Feature>(std::size_t{0});
  }

  // The `index`-th record writing `Feature`, clamped to the last one; found as
  // a member-anchored diagnostic finds it.
  template <auto Feature>
    requires std::derived_from<NodeT, property_owner<Feature>>
  [[nodiscard]] FormattingRegion property(std::size_t index) const {
    if (!searchable()) return FormattingRegion(_cst, {}, _collector);
    const CstNodeView at = find_node_for_property(
        *_node, cursiv::detail::member_identifier<Feature>(), index, _document);
    if (!at) return FormattingRegion(_cst, {}, _collector);
    return FormattingRegion(at.id, _cst, _collector);
  }

  // Every record that writes any of the given members, member by member in
  // the order given.
  template <auto... Features>
    requires(sizeof...(Features) > 0 &&
             (std::derived_from<NodeT, property_owner<Features>> && ...))
  [[nodiscard]] FormattingRegion properties() const {
    std::vector<NodeId> found;
    if (searchable())
      (appendIds(found,
                 find_nodes_for_property(
                     *_node, cursiv::detail::member_identifier<Features>(),
                     _document)),
       ...);
    return FormattingRegion(_cst, std::move(found), _collector);
  }

  // The `index`-th `keyword` among this construction's visible descendants, not
  // only its children. An index past the end selects nothing.
  [[nodiscard]] FormattingRegion keyword(std::string_view keyword,
                                         std::size_t index = 0) const {
    if (!searchable()) return FormattingRegion(_cst, {}, _collector);
    // The first one is found without listing the others.
    if (index == 0) {
      const CstNodeView first =
          find_node_for_keyword(*_node, keyword, 0, _document);
      if (!first) return FormattingRegion(_cst, {}, _collector);
      return FormattingRegion(first.id, _cst, _collector);
    }
    const std::vector<CstNodeView> found =
        find_nodes_for_keyword(*_node, keyword, _document);
    if (index >= found.size()) return FormattingRegion(_cst, {}, _collector);
    return FormattingRegion(found[index].id, _cst, _collector);
  }

  template <class... Keywords>
    requires(sizeof...(Keywords) > 0 &&
             (std::convertible_to<Keywords, std::string_view> && ...))
  [[nodiscard]] FormattingRegion keywords(Keywords&&... keywords) const {
    std::vector<NodeId> found;
    if (searchable())
      (appendIds(found,
                 find_nodes_for_keyword(
                     *_node, std::string_view(std::forward<Keywords>(keywords)),
                     _document)),
       ...);
    return FormattingRegion(_cst, std::move(found), _collector);
  }

  // The hidden records this construction encloses, by terminal rule name
  // (`hidden("SL_COMMENT")`), in document order; a record that opens a node of
  // its own is that node's. A rule on one sets its margin only.
  [[nodiscard]] FormattingRegion hidden(std::string_view ruleName,
                                        std::size_t index = 0) const {
    if (!searchable()) return FormattingRegion(_cst, {}, _collector);
    const std::vector<CstNodeView> found =
        find_hidden_nodes(*_node, ruleName, _document);
    if (index >= found.size()) return FormattingRegion(_cst, {}, _collector);
    return FormattingRegion(found[index].id, _cst, _collector);
  }

  [[nodiscard]] FormattingRegion hiddens(std::string_view ruleName) const {
    std::vector<NodeId> found;
    if (searchable())
      appendIds(found, find_hidden_nodes(*_node, ruleName, _document));
    return FormattingRegion(_cst, std::move(found), _collector);
  }

  [[nodiscard]] FormattingRegion cst(std::span<const NodeId> nodes) const {
    return FormattingRegion(
        _cst, std::vector<NodeId>(nodes.begin(), nodes.end()), _collector);
  }

  // What lies between two single-record regions -- an opening and a closing
  // brace, whose interior is what gets indented.
  [[nodiscard]] FormattingRegion interior(const FormattingRegion& open,
                                          const FormattingRegion& close) const {
    if (_cst == nullptr || open.nodes().size() != 1 ||
        close.nodes().size() != 1)
      return FormattingRegion(_cst, {}, _collector);
    return FormattingRegion(
        _cst, interior_nodes(*_cst, open.nodes()[0], close.nodes()[0]),
        _collector);
  }

 private:
  // A node to search from: a builder may be handed none.
  [[nodiscard]] bool searchable() const noexcept { return _node != nullptr; }

  static void appendIds(std::vector<NodeId>& out,
                        const std::vector<CstNodeView>& found) {
    for (const CstNodeView& at : found) out.push_back(at.id);
  }

  const Cst* _cst;
  // The node's document, given to every search so none walks up to the root.
  const Document* _document;
  const AstNode* _node;
  FormattingCollector* _collector;
};

// The one argument a rule method receives besides its node.
class FormattingBuilder {
 public:
  FormattingBuilder(const Document& document,
                    FormattingCollector& collector) noexcept
      : _document(&document), _collector(&collector) {}

  template <class NodeT>
    requires std::derived_from<std::remove_cv_t<NodeT>, AstNode>
  [[nodiscard]] NodeFormatter<std::remove_cv_t<NodeT>> getNodeFormatter(
      const NodeT* node) const {
    return NodeFormatter<std::remove_cv_t<NodeT>>(*_document, node, _collector);
  }

 private:
  const Document* _document;
  FormattingCollector* _collector;
};

class AbstractFormatter : public Formatter, protected DefaultCoreService {
 public:
  [[nodiscard]] std::vector<lsp::TextEdit> formatDocument(
      const Document& document, const lsp::DocumentFormattingParams& params,
      const CancellationToken& token = {}) const override;

  [[nodiscard]] std::vector<lsp::TextEdit> formatDocumentRange(
      const Document& document,
      const lsp::DocumentRangeFormattingParams& params,
      const CancellationToken& token = {}) const override;

  [[nodiscard]] std::vector<lsp::TextEdit> formatDocumentOnType(
      const Document& document,
      const lsp::DocumentOnTypeFormattingParams& params,
      const CancellationToken& token = {}) const override;

 protected:
  explicit AbstractFormatter(const CoreServices& core) noexcept
      : DefaultCoreService(core) {}

  // Declared as members so a rule reads `oneSpace` and `formatBlock`
  // directly, never namespace-qualified.
  using MultilineCommentFormatOptions = cursiv::MultilineCommentFormatOptions;
  using HiddenNodeFormatter = cursiv::HiddenNodeFormatter;
  using BlockFormatOptions = cursiv::BlockFormatOptions;
  using SeparatedListFormatOptions = cursiv::SeparatedListFormatOptions;
  using LineCommentFormatOptions = cursiv::LineCommentFormatOptions;

  static constexpr KeepActionBuilder keep{};
  static constexpr SpaceActionBuilder noSpace{0};
  static constexpr SpaceActionBuilder oneSpace{1};
  static constexpr LineActionBuilder newLine{1};
  static constexpr IndentActionBuilder indent{};
  static constexpr NoIndentActionBuilder noIndent{};

  [[nodiscard]] static FormattingAction spaces(
      std::int32_t count, FormattingActionOptions options = {});
  [[nodiscard]] static FormattingAction newLines(
      std::int32_t count, FormattingActionOptions options = {});

  // Several candidate shapes, and the engine keeps the one the author already
  // wrote: `fit(oneSpace, newLine)` leaves a one-line construction on one line
  // and a spread one spread.
  template <class... Actions>
    requires(sizeof...(Actions) > 0 &&
             (std::convertible_to<Actions, FormattingAction> && ...))
  [[nodiscard]] static FormattingAction fit(Actions&&... actions) {
    return fitAll(
        {static_cast<FormattingAction>(std::forward<Actions>(actions))...});
  }

  // An empty block stays on its line; a block with content opens a line
  // after `open` and before `close`, and indents the content.
  static void formatBlock(FormattingRegion open, FormattingRegion close,
                          FormattingRegion content,
                          const BlockFormatOptions& options);
  static void formatBlock(FormattingRegion open, FormattingRegion close,
                          FormattingRegion content = {}) {
    formatBlock(std::move(open), std::move(close), std::move(content),
                BlockFormatOptions{});
  }

  static void formatSeparatedList(FormattingRegion separators,
                                  const SeparatedListFormatOptions& options);
  static void formatSeparatedList(FormattingRegion separators) {
    formatSeparatedList(std::move(separators), SeparatedListFormatOptions{});
  }

  [[nodiscard]] static std::string formatLineComment(
      std::string_view text, const LineCommentFormatOptions& options);
  [[nodiscard]] static std::string formatLineComment(std::string_view text) {
    return formatLineComment(text, LineCommentFormatOptions{});
  }
  [[nodiscard]] static std::string formatLineComment(
      const HiddenNodeFormatter& comment) {
    return formatLineComment(comment.text(), LineCommentFormatOptions{});
  }
  [[nodiscard]] static std::string formatLineComment(
      const HiddenNodeFormatter& comment,
      const LineCommentFormatOptions& options) {
    return formatLineComment(comment.text(), options);
  }

  [[nodiscard]] static std::string formatMultilineComment(
      std::string_view text, const MultilineCommentFormatOptions& options);
  [[nodiscard]] static std::string formatMultilineComment(
      std::string_view text) {
    return formatMultilineComment(text, MultilineCommentFormatOptions{});
  }
  // With a record, an empty line ending is taken from the document.
  [[nodiscard]] static std::string formatMultilineComment(
      const HiddenNodeFormatter& comment) {
    return formatMultilineComment(comment, MultilineCommentFormatOptions{});
  }
  [[nodiscard]] static std::string formatMultilineComment(
      const HiddenNodeFormatter& comment,
      MultilineCommentFormatOptions options) {
    if (options.newlineStyle.empty())
      options.newlineStyle = comment.newlineStyle();
    return formatMultilineComment(comment.text(), options);
  }


  // Registers a rule method of this formatter for exactly `NodeT`.
  template <class NodeT, class FormatterT>
    requires std::derived_from<std::remove_cv_t<NodeT>, AstNode> &&
             std::derived_from<std::remove_cv_t<FormatterT>, AbstractFormatter>
  void on(void (FormatterT::*method)(FormattingBuilder&,
                                     const std::remove_cv_t<NodeT>*) const) {
    if (method == nullptr) return;
    using Node = std::remove_cv_t<NodeT>;
    _formatters.insert_or_assign(
        &cursiv::ast_type_tag<Node>,
        [method](const AbstractFormatter& self, FormattingBuilder& builder,
                 const AstNode* node) {
          (static_cast<const FormatterT&>(self).*method)(builder,
                                                         ast_cast<Node>(node));
        });
  }

  // Registers a rule method for one hidden terminal -- a comment.
  template <class FormatterT>
    requires std::derived_from<std::remove_cv_t<FormatterT>, AbstractFormatter>
  void onHidden(std::string_view terminalRuleName,
                void (FormatterT::*method)(HiddenNodeFormatter&) const) {
    if (terminalRuleName.empty() || method == nullptr) return;
    _hiddenFormatters.insert_or_assign(
        std::string(terminalRuleName),
        [method](const AbstractFormatter& self, HiddenNodeFormatter& hidden) {
          (static_cast<const FormatterT&>(self).*method)(hidden);
        });
  }

  // A range is formattable when every error lies after it: an error after the
  // selection cannot have moved what is inside it.
  [[nodiscard]] bool isFormatRangeErrorFree(const Document& document,
                                            const lsp::Range& range) const;

  // Declares the rules for one AST node. The default dispatches what `on<T>`
  // registered, by exact type; override only for a custom dispatch.
  virtual void format(FormattingBuilder& builder, const AstNode* node) const;

  // ...and for one hidden record, by the terminal rule that produced it.
  virtual void formatHidden(HiddenNodeFormatter& hidden) const;

  [[nodiscard]] std::vector<lsp::TextEdit> doDocumentFormat(
      const Document& document, const lsp::FormattingOptions& options,
      const std::optional<lsp::Range>& range,
      const CancellationToken& token) const;

 private:
  // The moves of every action, ordered from the tightest to the loosest.
  [[nodiscard]] static FormattingAction fitAll(
      std::initializer_list<FormattingAction> actions);

  using FormatCallback = std::function<void(
      const AbstractFormatter&, FormattingBuilder&, const AstNode*)>;
  using HiddenCallback =
      std::function<void(const AbstractFormatter&, HiddenNodeFormatter&)>;

  std::unordered_map<const AstTypeTag*, FormatCallback> _formatters;
  std::map<std::string, HiddenCallback, std::less<>> _hiddenFormatters;
};

}  // namespace cursiv
