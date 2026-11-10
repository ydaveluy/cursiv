#pragma once

// When nothing is typed at the caret, also offers keywords without a letter,
// which the default provider leaves out.

#include <memory>
#include <string_view>

#include "cursiv/completion/DefaultCompletionProvider.hpp"
#include "cursiv/services/LspServices.hpp"

namespace support {

struct OffersEveryKeyword final : cursiv::DefaultCompletionProvider {
  using DefaultCompletionProvider::DefaultCompletionProvider;
  bool filterKeyword(const cursiv::CompletionContext& context,
                     std::string_view keyword) const override {
    return context.tokenOffset == context.offset ||
           DefaultCompletionProvider::filterKeyword(context, keyword);
  }
};

inline void offer_every_keyword(cursiv::LspServices& language) {
  language.completionProvider = std::make_unique<OffersEveryKeyword>(language);
}

}  // namespace support
