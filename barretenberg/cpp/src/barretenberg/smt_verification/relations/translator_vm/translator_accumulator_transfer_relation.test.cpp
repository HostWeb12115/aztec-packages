#include <gtest/gtest.h>

#include "barretenberg/smt_verification/relations/translator_vm/translator_relations.hpp"
#include "barretenberg/smt_verification/relations/translator_vm/translator_relations_recorder.hpp"
#include "barretenberg/smt_verification/solver/solver.hpp"
#include "barretenberg/smt_verification/terms/term.hpp"
#include <array>

using namespace bb;

TEST(TranslatorAccumulatorTransferRelation, test_accumulator_transfer_relation)
{
    smt_solver::Solver s("30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001",
                         smt_solver::default_solver_config);

    auto transfer_trace = smt_translator_relations::record_translator_accumulator_transfer_relation();

    std::vector<smt_terms::STerm> transfer_formulas;
    std::vector<smt_terms::STerm> transfer_vars;
    std::vector<std::string> transfer_names;

    smt_translator_relations::replay_translator_accumulator_transfer_relation(
        transfer_trace, &s, "acc_transfer", true, transfer_formulas, transfer_vars, transfer_names);

    smt_translator_relations::create_range_constraint_formulas(
        &s, transfer_vars, transfer_names, "acc_transfer", 16384);
    smt_translator_relations::assert_formulas_zero(&s, transfer_formulas);

    auto get_var = [&](const std::string& name) -> smt_terms::STerm {
        for (size_t i = 0; i < transfer_names.size(); ++i) {
            if (transfer_names[i] == name) {
                return transfer_vars[i];
            }
        }
        ADD_FAILURE() << "Variable not found: " << name;
        return transfer_vars.front();
    };

    smt_terms::STerm lagrange_last = get_var("acc_transfer_lagrange_last_in_minicircuit");
    smt_terms::STerm lagrange_odd = get_var("acc_transfer_lagrange_odd_in_minicircuit");
    smt_terms::STerm lagrange_result_row = get_var("acc_transfer_lagrange_result_row");

    smt_terms::STerm acc0 = get_var("acc_transfer_accumulators_binary_limbs_0");
    smt_terms::STerm acc1 = get_var("acc_transfer_accumulators_binary_limbs_1");
    smt_terms::STerm acc2 = get_var("acc_transfer_accumulators_binary_limbs_2");
    smt_terms::STerm acc3 = get_var("acc_transfer_accumulators_binary_limbs_3");

    smt_terms::STerm acc0_shift = get_var("acc_transfer_accumulators_binary_limbs_0_shift");
    smt_terms::STerm acc1_shift = get_var("acc_transfer_accumulators_binary_limbs_1_shift");
    smt_terms::STerm acc2_shift = get_var("acc_transfer_accumulators_binary_limbs_2_shift");
    smt_terms::STerm acc3_shift = get_var("acc_transfer_accumulators_binary_limbs_3_shift");

    smt_terms::STerm lagrange_mini_masking = get_var("acc_transfer_lagrange_mini_masking");

    std::array<smt_terms::STerm, 4> accumulated_result_params = { get_var("acc_transfer_accumulated_result_param_0"),
                                                                  get_var("acc_transfer_accumulated_result_param_1"),
                                                                  get_var("acc_transfer_accumulated_result_param_2"),
                                                                  get_var("acc_transfer_accumulated_result_param_3") };

    smt_terms::STerm zero = smt_terms::FFIConst("0", &s, 10);
    smt_terms::STerm one = smt_terms::FFIConst("1", &s, 10);

    auto assert_pair_must_match = [&](const smt_terms::STerm& a, const smt_terms::STerm& b) {
        // Base case: should be satisfiable when the limbs are allowed to match
        s.push();
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(lagrange_last), static_cast<cvc5::Term>(zero) }));
        s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                              { static_cast<cvc5::Term>(lagrange_odd), static_cast<cvc5::Term>(one) }));
        ASSERT_TRUE(s.check());
        s.pop();

        // Contradiction: forcing inequality must be UNSAT
        s.push();
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(lagrange_last), static_cast<cvc5::Term>(zero) }));
        s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                              { static_cast<cvc5::Term>(lagrange_odd), static_cast<cvc5::Term>(one) }));

        smt_terms::STerm diff = a - b;
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::NOT,
            { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                    { static_cast<cvc5::Term>(diff), static_cast<cvc5::Term>(zero) }) }));
        ASSERT_FALSE(s.check());
        s.pop();
    };

    // Ensuring that the accumulator limbs are equal to their shifted counterparts, when lagrange_odd_in_minicircuit is
    // 1 and lagrange_last_in_minicircuit is 0.
    assert_pair_must_match(acc0, acc0_shift);
    assert_pair_must_match(acc1, acc1_shift);
    assert_pair_must_match(acc2, acc2_shift);
    assert_pair_must_match(acc3, acc3_shift);

    auto assert_limb_zero_when_last = [&](const smt_terms::STerm& limb) {
        // Base case: satisfiable when limb is allowed to be zero
        s.push();
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(lagrange_last), static_cast<cvc5::Term>(one) }));
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(lagrange_mini_masking), static_cast<cvc5::Term>(zero) }));
        ASSERT_TRUE(s.check());
        s.pop();

        // Contradiction: limb must equal zero in this branch
        s.push();
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(lagrange_last), static_cast<cvc5::Term>(one) }));
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(lagrange_mini_masking), static_cast<cvc5::Term>(zero) }));
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::NOT,
            { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                    { static_cast<cvc5::Term>(limb), static_cast<cvc5::Term>(zero) }) }));
        ASSERT_FALSE(s.check());
        s.pop();
    };

    // Ensuring that when lagrange_last_in_minicircuit is 1 and the row is unmasked, the accumulator limbs must be 0.
    assert_limb_zero_when_last(acc0);
    assert_limb_zero_when_last(acc1);
    assert_limb_zero_when_last(acc2);
    assert_limb_zero_when_last(acc3);

    auto assert_limb_matches_result_row = [&](const smt_terms::STerm& limb, const smt_terms::STerm& param) {
        // Base case: satisfiable when limb equals parameter
        s.push();
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(lagrange_result_row), static_cast<cvc5::Term>(one) }));
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(lagrange_mini_masking), static_cast<cvc5::Term>(zero) }));
        ASSERT_TRUE(s.check());
        s.pop();

        // Contradiction: limb differing from parameter must be UNSAT
        s.push();
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(lagrange_result_row), static_cast<cvc5::Term>(one) }));
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(lagrange_mini_masking), static_cast<cvc5::Term>(zero) }));
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::NOT,
            { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                    { static_cast<cvc5::Term>(limb), static_cast<cvc5::Term>(param) }) }));
        ASSERT_FALSE(s.check());
        s.pop();
    };

    // Ensuring that when lagrange_result_row is 1 (and not masked), limbs must match the accumulated result parameter.
    assert_limb_matches_result_row(acc0, accumulated_result_params[0]);
    assert_limb_matches_result_row(acc1, accumulated_result_params[1]);
    assert_limb_matches_result_row(acc2, accumulated_result_params[2]);
    assert_limb_matches_result_row(acc3, accumulated_result_params[3]);
}
