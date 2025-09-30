#pragma once

#include "barretenberg/smt_verification/terms/term.hpp"

namespace smt_translator_relations {

// Instantiate TranslatorDecompositionRelationImpl over symbolic FFTerm and assert subrelations == 0.
void instantiate_translator_decomposition_with_ffterm_and_assert(smt_terms::Solver* solver);

// Instantiate TranslatorDecompositionRelationImpl over symbolic ITerm (integers) and assert subrelations == 0.
void instantiate_translator_decomposition_with_iterm_and_assert(smt_terms::Solver* solver);

// Same as above, but prefix all variable names with the provided prefix to allow multiple independent sets.
void instantiate_translator_decomposition_with_iterm_and_assert_prefix(smt_terms::Solver* solver,
                                                                       const std::string& prefix);

// Instantiate with integer terms, assert relations, and collect the created variables (in-order) and their labels.
void instantiate_translator_decomposition_with_iterm_and_assert_prefix_collect(smt_terms::Solver* solver,
                                                                               const std::string& prefix,
                                                                               std::vector<smt_terms::STerm>& out_vars,
                                                                               std::vector<std::string>& out_names);

// Add uniqueness-style constraints for two prefixed instantiations of the translator relation.
// - Enforce equality of all non-CONSTRAINT variables between prefix1 and prefix2
// - Enforce that at least one CONSTRAINT variable differs between prefix1 and prefix2
// - Enforce scaling == 1 and LAGRANGE_EVEN_IN_MINICIRCUIT == 1 for both copies
void add_translator_uniqueness_constraints_for_prefix_pair(smt_terms::Solver* solver,
                                                           const std::string& prefix1,
                                                           const std::string& prefix2,
                                                           const std::vector<smt_terms::STerm>& vars1,
                                                           const std::vector<std::string>& names1,
                                                           const std::vector<smt_terms::STerm>& vars2,
                                                           const std::vector<std::string>& names2);

} // namespace smt_translator_relations
