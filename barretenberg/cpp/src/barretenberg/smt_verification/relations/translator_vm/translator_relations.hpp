#pragma once

#include "barretenberg/smt_verification/terms/term.hpp"

namespace smt_translator_relations {

// Instantiate TranslatorDecompositionRelationImpl over symbolic FFTerm and assert subrelations == 0.
void instantiate_translator_decomposition_with_ffterm_and_assert(smt_terms::Solver* solver);

} // namespace smt_translator_relations

