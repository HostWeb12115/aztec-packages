#include <gtest/gtest.h>

#include "translator_relation_test_helpers.hpp"
#include "translator_relations.hpp"
#include "translator_relations_recorder.hpp"

using namespace bb;
using namespace translator_relation_test_helpers;

// Verify that the non-native field relation uniquely determines accumulator limbs
// given evaluation inputs and batching challenges
TEST(TranslatorNonNativeFieldRelation, accumulator_limbs_are_unique)
{
    smt_solver::Solver s(BN254_MODULUS, smt_solver::debug_solver_config);

    auto recording_trace = smt_translator_relations::record_translator_non_native_field_relation();

    std::vector<smt_terms::STerm> formulas, vars;
    std::vector<std::string> names;
    smt_translator_relations::replay_translator_non_native_field_relation(
        recording_trace, &s, "nnf", true, formulas, vars, names);

    // Generate random parameters
    auto random_previous_accumulator = uint256_t(bb::fq::random_element());
    auto random_evaluation_input = uint256_t(bb::fq::random_element());
    auto random_batching_challenge_v_0 = uint256_t(bb::fq::random_element());
    auto random_batching_challenge_v_1 = uint256_t(bb::fq::random_element());
    auto random_batching_challenge_v_2 = uint256_t(bb::fq::random_element());
    auto random_batching_challenge_v_3 = uint256_t(bb::fq::random_element());

    std::vector<std::pair<std::string, uint256_t>> params = {
        { "accumulators_binary_limbs_0_shift", random_previous_accumulator.slice(0, 68) },
        { "accumulators_binary_limbs_1_shift", random_previous_accumulator.slice(68, 136) },
        { "accumulators_binary_limbs_2_shift", random_previous_accumulator.slice(136, 204) },
        { "accumulators_binary_limbs_3_shift", random_previous_accumulator.slice(204, 272) },
        { "evaluation_input_x_0", random_evaluation_input.slice(0, 68) },
        { "evaluation_input_x_1", random_evaluation_input.slice(68, 136) },
        { "evaluation_input_x_2", random_evaluation_input.slice(136, 204) },
        { "evaluation_input_x_3", random_evaluation_input.slice(204, 272) },
        { "evaluation_input_x_4", random_evaluation_input % bb::fr::modulus },
        { "batching_challenge_v_0_0", random_batching_challenge_v_0.slice(0, 68) },
        { "batching_challenge_v_0_1", random_batching_challenge_v_0.slice(68, 136) },
        { "batching_challenge_v_0_2", random_batching_challenge_v_0.slice(136, 204) },
        { "batching_challenge_v_0_3", random_batching_challenge_v_0.slice(204, 272) },
        { "batching_challenge_v_0_4", random_batching_challenge_v_0 % bb::fr::modulus },
        { "batching_challenge_v_1_0", random_batching_challenge_v_1.slice(0, 68) },
        { "batching_challenge_v_1_1", random_batching_challenge_v_1.slice(68, 136) },
        { "batching_challenge_v_1_2", random_batching_challenge_v_1.slice(136, 204) },
        { "batching_challenge_v_1_3", random_batching_challenge_v_1.slice(204, 272) },
        { "batching_challenge_v_1_4", random_batching_challenge_v_1 % bb::fr::modulus },
        { "batching_challenge_v_2_0", random_batching_challenge_v_2.slice(0, 68) },
        { "batching_challenge_v_2_1", random_batching_challenge_v_2.slice(68, 136) },
        { "batching_challenge_v_2_2", random_batching_challenge_v_2.slice(136, 204) },
        { "batching_challenge_v_2_3", random_batching_challenge_v_2.slice(204, 272) },
        { "batching_challenge_v_2_4", random_batching_challenge_v_2 % bb::fr::modulus },
        { "batching_challenge_v_3_0", random_batching_challenge_v_3.slice(0, 68) },
        { "batching_challenge_v_3_1", random_batching_challenge_v_3.slice(68, 136) },
        { "batching_challenge_v_3_2", random_batching_challenge_v_3.slice(136, 204) },
        { "batching_challenge_v_3_3", random_batching_challenge_v_3.slice(204, 272) },
        { "batching_challenge_v_3_4", random_batching_challenge_v_3 % bb::fr::modulus }
    };

    set_relation_parameters(vars, names, s, "nnf", params);
    smt_translator_relations::assert_formulas_zero(&s, formulas);
    apply_expected_limb_bounds(s, vars, names, "nnf");

    // Verify system is satisfiable with these constraints
    ASSERT_TRUE(s.check());

    // Check uniqueness by creating a second instantiation with same parameters
    s.push();

    std::vector<smt_terms::STerm> formulas_2, vars_2;
    std::vector<std::string> names_2;
    smt_translator_relations::replay_translator_non_native_field_relation(
        recording_trace, &s, "nnf_alt", true, formulas_2, vars_2, names_2);

    set_relation_parameters(vars_2, names_2, s, "nnf_alt", params);
    smt_translator_relations::assert_formulas_zero(&s, formulas_2);
    apply_expected_limb_bounds(s, vars_2, names_2, "nnf_alt");

    auto find_var = [&](const std::vector<std::string>& list,
                        const std::vector<smt_terms::STerm>& values,
                        const std::string& target) -> smt_terms::STerm {
        auto it = std::find(list.begin(), list.end(), target);
        if (it == list.end()) {
            throw std::runtime_error("Failed to find variable: " + target);
        }
        return values[static_cast<size_t>(std::distance(list.begin(), it))];
    };

    // Assert that at least one accumulator limb differs
    smt_terms::STerm zero = smt_terms::FFIConst("0", &s, 10);
    std::vector<cvc5::Term> differs;

    for (size_t i = 0; i < 4; ++i) {
        std::string limb_name = "accumulators_binary_limbs_" + std::to_string(i);
        smt_terms::STerm lhs = find_var(names, vars, "nnf_" + limb_name);
        smt_terms::STerm rhs = find_var(names_2, vars_2, "nnf_alt_" + limb_name);
        differs.push_back(s.term_manager.mkTerm(
            cvc5::Kind::NOT,
            { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                    { static_cast<cvc5::Term>(lhs - rhs), static_cast<cvc5::Term>(zero) }) }));
    }

    cvc5::Term at_least_one_differs = differs[0];
    for (size_t i = 1; i < differs.size(); ++i) {
        at_least_one_differs = s.term_manager.mkTerm(cvc5::Kind::OR, { at_least_one_differs, differs[i] });
    }
    s.assertFormula(at_least_one_differs);

    // System should be UNSAT, proving uniqueness
    ASSERT_FALSE(s.check());

    s.pop();
}
