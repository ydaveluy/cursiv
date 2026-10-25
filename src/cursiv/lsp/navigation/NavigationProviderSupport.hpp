// The walks the navigation questions share: the three go-to questions
// differ only in what each makes of a target, and find-references, rename
// and highlight all read the same rows per target.
#pragma once

#include <lsp/types.h>

#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "cursiv/core/references/References.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"

namespace cursiv {

struct Document;

/**
 * `collect` answers for one target. An empty overall result is
 * `nullopt`, which the protocol reads as "nothing here" rather than "an
 * empty set".
 */
[[nodiscard]] std::optional<std::vector<lsp::LocationLink>>
collect_links_per_declaration(
    const References& references, const Document& document,
    lsp::Position position,
    const std::function<std::vector<lsp::LocationLink>(
        const AstNode&, const CancellationToken&)>& collect,
    const CancellationToken& cancelToken);

/**
 * Every target's rows, once each and in (document, offset) order: a multi
 * reference's targets overlap on the reference that reaches them all, and
 * a row answered twice is an edit a client refuses to apply.
 */
[[nodiscard]] std::vector<ReferenceDescription>
collect_references_per_declaration(
    const References& references, std::span<const AstNode* const> targets,
    function_ref<FindReferencesOptions(const AstNode&)> options,
    const CancellationToken& cancelToken);

}  // namespace cursiv
