#pragma once

#include <lsp/messages.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstUtils.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/formatting/Formatter.hpp"
#include "cursiv/lsp/formatting/FormattingAction.hpp"
#include "cursiv/lsp/formatting/FormattingRules.hpp"

// What a language's author writes, and the base that turns it into edits.
// Nothing here names a grammar: which element writes a member is asked of
// the reified model, so one archive serves every language. Dispatch is keyed
// on `node->type` (`ast_type_tag<T>`'s address): exact runtime type, no RTTI.

namespace cursiv {

// Every visible descendant, not just a direct child: a `{` written inside a
// group is not a child of the construction that owns it.
[[nodiscard]] std::vector<NodeId> keyword_nodes(const Cst& cst, NodeId root,
                                                std::string_view keyword);

// Every hidden record of a construction, in document order. A record that
// opens a node of its own answers for itself, as it does for a keyword.
[[nodiscard]] std::vector<NodeId> hidden_nodes(const Cst& cst, NodeId root,
                                               std::string_view ruleName);

// Which records write this member, as a consteval bitset over the element
// table. Level-invariant by construction: a collapsed grammar relabels the
// payload's record, and the field column travels with it.
template <auto Ptr>
using feature_owner = typename cursiv::detail::field_tag<Ptr>::owner;

template <class NodeT>
class NodeFormatter {
 public:
  NodeFormatter(const Cst* cst, NodeId root,
                FormattingCollector* collector) noexcept
      : _cst(cst), _root(root), _collector(collector) {}

  // The record a node was built from -- the construction's own, or any node
  // already in hand.
  [[nodiscard]] FormattingRegion node(const AstNode& node) const {
    if (node.cstNodeId == kNoNode)
      return FormattingRegion(_cst, {}, _collector);
    return FormattingRegion(_cst, {node.cstNodeId}, _collector);
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
    requires std::derived_from<NodeT, feature_owner<Feature>>
  [[nodiscard]] FormattingRegion property() const {
    return property<Feature>(std::size_t{0});
  }

  // The `index`-th record writing `Feature`, clamped to the last one written.
  template <auto Feature>
    requires std::derived_from<NodeT, feature_owner<Feature>>
  [[nodiscard]] FormattingRegion property(std::size_t index) const {
    if (_cst == nullptr || _root == kNoNode)
      return FormattingRegion(_cst, {}, _collector);
    const std::optional<CstNodeView> at = find_node_for_feature(
        CstNodeView{_cst, _root}, cursiv::detail::member_identifier<Feature>(),
        index);
    if (!at.has_value()) return FormattingRegion(_cst, {}, _collector);
    return FormattingRegion(_cst, {at->id}, _collector);
  }

  // Every record that writes any of the given members, member by member in
  // the order given.
  template <auto... Features>
    requires(sizeof...(Features) > 0 &&
             (std::derived_from<NodeT, feature_owner<Features>> && ...))
  [[nodiscard]] FormattingRegion properties() const {
    std::vector<NodeId> found;
    (appendFeatureNodes<Features>(found), ...);
    return FormattingRegion(_cst, std::move(found), _collector);
  }

  // The `index`-th occurrence of `keyword` among this construction's own
  // literals. An index past the end selects nothing -- a rule about a word that
  // is not there must not silently land on another one.
  [[nodiscard]] FormattingRegion keyword(std::string_view keyword,
                                         std::size_t index = 0) const {
    if (_cst == nullptr) return FormattingRegion(_cst, {}, _collector);
    std::vector<NodeId> found = keyword_nodes(*_cst, _root, keyword);
    if (index >= found.size()) return FormattingRegion(_cst, {}, _collector);
    return FormattingRegion(_cst, {found[index]}, _collector);
  }

  template <class... Keywords>
    requires(sizeof...(Keywords) > 0 &&
             (std::convertible_to<Keywords, std::string_view> && ...))
  [[nodiscard]] FormattingRegion keywords(Keywords&&... keywords) const {
    std::vector<NodeId> found;
    if (_cst != nullptr)
      (appendKeywordNodes(found,
                          std::string_view(std::forward<Keywords>(keywords))),
       ...);
    return FormattingRegion(_cst, std::move(found), _collector);
  }

  // The hidden records this construction encloses, by the terminal rule that
  // produced them -- `hidden("SL_COMMENT")` is the grammar's own name.
  [[nodiscard]] FormattingRegion hidden(std::string_view ruleName,
                                        std::size_t index = 0) const {
    if (_cst == nullptr) return FormattingRegion(_cst, {}, _collector);
    std::vector<NodeId> found = hidden_nodes(*_cst, _root, ruleName);
    if (index >= found.size()) return FormattingRegion(_cst, {}, _collector);
    return FormattingRegion(_cst, {found[index]}, _collector);
  }

  [[nodiscard]] FormattingRegion hiddens(std::string_view ruleName) const {
    if (_cst == nullptr) return FormattingRegion(_cst, {}, _collector);
    return FormattingRegion(_cst, hidden_nodes(*_cst, _root, ruleName),
                            _collector);
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
  // By name, through the reified grammar: the same door a member-anchored
  // diagnostic uses, so the two can never disagree about where a member was
  // written.
  template <auto Feature>
  void appendFeatureNodes(std::vector<NodeId>& out) const {
    if (_cst == nullptr || _root == kNoNode) return;
    for (const CstNodeView& at :
         find_nodes_for_feature(CstNodeView{_cst, _root},
                                cursiv::detail::member_identifier<Feature>()))
      out.push_back(at.id);
  }

  void appendKeywordNodes(std::vector<NodeId>& out,
                          std::string_view keyword) const {
    const std::vector<NodeId> found = keyword_nodes(*_cst, _root, keyword);
    out.insert(out.end(), found.begin(), found.end());
  }

  const Cst* _cst;
  NodeId _root;
  FormattingCollector* _collector;
};

// The one argument a rule method receives besides its node.
class FormattingBuilder {
 public:
  FormattingBuilder(const Cst& cst, FormattingCollector& collector) noexcept
      : _cst(&cst), _collector(&collector) {}

  template <class NodeT>
    requires std::derived_from<std::remove_cv_t<NodeT>, AstNode>
  [[nodiscard]] NodeFormatter<std::remove_cv_t<NodeT>> getNodeFormatter(
      const NodeT* node) const {
    return NodeFormatter<std::remove_cv_t<NodeT>>(
        _cst, node == nullptr ? kNoNode : node->cstNodeId, _collector);
  }

 private:
  const Cst* _cst;
  FormattingCollector* _collector;
};

class AbstractFormatter : public Formatter {
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
  explicit AbstractFormatter(const CoreServices& core) noexcept : _core(core) {}

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
      std::int32_t count, FormattingActionOptions options = {}) {
    return make_spaces(count, options);
  }
  [[nodiscard]] static FormattingAction newLines(
      std::int32_t count, FormattingActionOptions options = {}) {
    return make_new_lines(count, options);
  }

  // Several candidate shapes, and the engine keeps the one the author already
  // wrote: `fit(oneSpace, newLine)` leaves a one-line construction on one line
  // and a spread one spread.
  template <class... Actions>
    requires(sizeof...(Actions) > 0 &&
             (std::convertible_to<Actions, FormattingAction> && ...))
  [[nodiscard]] static FormattingAction fit(Actions&&... actions) {
    return fit_actions(
        {static_cast<FormattingAction>(std::forward<Actions>(actions))...});
  }

  static void formatBlock(FormattingRegion open, FormattingRegion close,
                          FormattingRegion content,
                          const BlockFormatOptions& options) {
    do_format_block(std::move(open), std::move(close), std::move(content),
                    options);
  }
  static void formatBlock(FormattingRegion open, FormattingRegion close,
                          FormattingRegion content = {}) {
    formatBlock(std::move(open), std::move(close), std::move(content),
                BlockFormatOptions{});
  }

  static void formatSeparatedList(FormattingRegion separators,
                                  const SeparatedListFormatOptions& options) {
    do_format_separated_list(std::move(separators), options);
  }
  static void formatSeparatedList(FormattingRegion separators) {
    formatSeparatedList(std::move(separators), SeparatedListFormatOptions{});
  }

  [[nodiscard]] static std::string formatLineComment(
      std::string_view text, const LineCommentFormatOptions& options) {
    return do_format_line_comment(text, options);
  }
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
      std::string_view text, const MultilineCommentFormatOptions& options) {
    return do_format_multiline_comment(text, options);
  }
  [[nodiscard]] static std::string formatMultilineComment(
      std::string_view text) {
    return formatMultilineComment(text, MultilineCommentFormatOptions{});
  }
  // The overloads that hold a record fill the style from the document, which
  // is the one thing the comment's own bytes cannot say.
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

  [[nodiscard]] const CoreServices& core() const noexcept { return _core; }

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
          (static_cast<const FormatterT&>(self).*method)(
              builder, static_cast<const Node*>(node));
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

  // A range is formattable while the errors are behind it: an error after the
  // selection cannot have moved what is inside it, so a broken tail does not
  // cost the author the shape of the head they just wrote.
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
  using FormatCallback = std::function<void(
      const AbstractFormatter&, FormattingBuilder&, const AstNode*)>;
  using HiddenCallback =
      std::function<void(const AbstractFormatter&, HiddenNodeFormatter&)>;

  const CoreServices& _core;
  std::unordered_map<const AstTypeTag*, FormatCallback> _formatters;
  std::map<std::string, HiddenCallback, std::less<>> _hiddenFormatters;
};

}  // namespace cursiv
