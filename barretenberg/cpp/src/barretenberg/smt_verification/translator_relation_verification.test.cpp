#include <gtest/gtest.h>

#include "barretenberg/smt_verification/relations/translator_vm/translator_relations.hpp"
#include "barretenberg/smt_verification/solver/solver.hpp"
#include "barretenberg/translator_vm/translator_flavor.hpp"
#include <fstream>

using namespace smt_solver;

// BN254 scalar field modulus (bb::fr)
static constexpr const char* kBn254ScalarModulusHex =
    "30644E72E131A029B85045B68181585D2833E84879B9709143E1F593F0000001";

TEST(TranslatorRelationVerification, translator_decomposition_relation_ff_formulas)
{
    Solver s(kBn254ScalarModulusHex, debug_solver_config);
    smt_translator_relations::instantiate_translator_decomposition_with_ffterm_and_assert(&s);
    bool res = s.check();
    ASSERT_TRUE(res);
}

TEST(TranslatorRelationVerification, translator_decomposition_relation_ffi_formulas)
{
    Solver s(kBn254ScalarModulusHex, debug_solver_config);
    smt_translator_relations::instantiate_translator_decomposition_with_iterm_and_assert(&s);
    bool res = s.check();
    ASSERT_TRUE(res);
}

TEST(TranslatorRelationVerification, translator_decomposition_relation_ffi_formulas_two_copies)
{
    Solver s(kBn254ScalarModulusHex, debug_solver_config);
    std::vector<smt_terms::STerm> c1_vars;
    std::vector<std::string> c1_names;
    std::vector<smt_terms::STerm> c2_vars;
    std::vector<std::string> c2_names;
    smt_translator_relations::instantiate_translator_decomposition_with_iterm_and_assert_prefix_collect(
        &s, "C1", c1_vars, c1_names);
    smt_translator_relations::instantiate_translator_decomposition_with_iterm_and_assert_prefix_collect(
        &s, "C2", c2_vars, c2_names);
    // Add uniqueness-style cross-copy constraints (non-CONSTRAINT equal, at least one CONSTRAINT differs),
    // and force scaling and Lagrange-even flag to 1 for both copies.
    smt_translator_relations::add_translator_uniqueness_constraints_for_prefix_pair(
        &s, "C1", "C2", c1_vars, c1_names, c2_vars, c2_names);
    bool res = s.check();
    ASSERT_TRUE(res);

    // Compare by using the exact collected variables/names from instantiation
    std::vector<std::pair<std::string, std::string>> differing;
    size_t n = std::min(c1_vars.size(), c2_vars.size());
    for (size_t i = 0; i < n; ++i) {
        const std::string& name1 = c1_names[i];
        (void)c2_names; // Names should correspond index-wise due to identical instantiation
        std::string v1 = s.get(c1_vars[i]);
        std::string v2 = s.get(c2_vars[i]);
        if (v1 != v2) {
            // Strip prefixes for compactness if both start with C1_/C2_
            std::string base = name1;
            if (base.rfind("C1_", 0) == 0) {
                base = base.substr(3);
            }
            differing.emplace_back(base, std::string(v1).append(" != ").append(v2));
        }
    }
    for (const auto& [name, diff] : differing) {
        std::cerr << name << ": " << diff << "\n";
    }
    // Also persist to a temp file to bypass gtest stdout capture
    {
        std::ofstream ofs("/tmp/translator_diffs.txt", std::ios::out | std::ios::trunc);
        if (ofs.is_open()) {
            for (const auto& [name, diff] : differing) {
                ofs << name << ": " << diff << "\n";
            }
            ofs.close();
        }
    }

    // Dump full model values for C1 and C2 to stderr and temp files
    {
        std::cerr << "C1 model:" << "\n";
        for (size_t i = 0; i < c1_vars.size(); ++i) {
            std::cerr << c1_names[i] << " = " << s.get(c1_vars[i]) << "\n";
        }
        // scaling values are included via collected vars/names
        std::cerr << "C2 model:" << "\n";
        for (size_t i = 0; i < c2_vars.size(); ++i) {
            std::cerr << c2_names[i] << " = " << s.get(c2_vars[i]) << "\n";
        }
        std::cerr.flush();

        std::ofstream ofs1("/tmp/translator_model_C1.txt", std::ios::out | std::ios::trunc);
        if (ofs1.is_open()) {
            size_t n1 = c1_vars.size();
            for (size_t i = 0; i < n1; ++i) {
                ofs1 << c1_names[i] << " = " << s.get(c1_vars[i]) << "\n";
            }
            ofs1.close();
        }
        std::ofstream ofs2("/tmp/translator_model_C2.txt", std::ios::out | std::ios::trunc);
        if (ofs2.is_open()) {
            size_t n2 = c2_vars.size();
            for (size_t i = 0; i < n2; ++i) {
                ofs2 << c2_names[i] << " = " << s.get(c2_vars[i]) << "\n";
            }
            ofs2.close();
        }
    }
}
