// `AstReducer::build` for the default stamp layout, compiled once for every
// language.
#include "cursiv/shared/runtime/AstReducer.hpp"

namespace cursiv::engine {

template ParseResult AstReducer::build<StampLayout{}>(const AstActions&, Cst&&,
                                                      const CancellationToken&,
                                                      const Grammar*);

}  // namespace cursiv::engine
