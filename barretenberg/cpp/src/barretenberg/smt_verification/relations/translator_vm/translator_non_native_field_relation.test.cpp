#include <gtest/gtest.h>

#include "barretenberg/numeric/uint256/uint256.hpp"
#include "barretenberg/relations/translator_vm/translator_decomposition_relation.hpp"
#include "barretenberg/smt_verification/relations/relation_operation_recorder.hpp"
#include "barretenberg/smt_verification/relations/translator_vm/translator_relations.hpp"
#include "barretenberg/smt_verification/relations/translator_vm/translator_relations_recorder.hpp"
#include "barretenberg/smt_verification/solver/solver.hpp"
#include "barretenberg/smt_verification/terms/term.hpp"
#include "barretenberg/translator_vm/translator_flavor.hpp"
#include <array>
#include <sstream>

namespace {

static std::string to_dec_string(const uint256_t& value)
{
    if (value == 0) {
        return "0";
    }

    std::string result;
    uint256_t temp = value;
    uint256_t base = 10;

    while (temp > 0) {
        uint256_t digit = temp % base;
        result = char('0' + static_cast<uint64_t>(digit.data[0])) + result;
        temp = temp / base;
    }

    return result;
}

void set_relation_parameter(std::vector<smt_terms::STerm>& vars,
                            const std::vector<std::string>& names,
                            smt_solver::Solver& s,
                            const std::string& prefix,
                            const std::string& target_name,
                            const uint256_t& value)
{
    const std::string full_name = prefix.empty() ? target_name : prefix + "_" + target_name;
    auto it = std::find(names.begin(), names.end(), full_name);
    if (it == names.end()) {
        throw std::runtime_error("Parameter not found: " + full_name);
    }
    size_t index = static_cast<size_t>(std::distance(names.begin(), it));
    smt_terms::STerm param = smt_terms::FFIConst(to_dec_string(value), &s, 10);
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                          { static_cast<cvc5::Term>(vars[index]), static_cast<cvc5::Term>(param) }));
}

void set_relation_parameters(std::vector<smt_terms::STerm>& vars,
                             const std::vector<std::string>& names,
                             smt_solver::Solver& s,
                             const std::string& prefix,
                             const std::vector<std::pair<std::string, uint256_t>>& assignments)
{
    for (const auto& assignment : assignments) {
        set_relation_parameter(vars, names, s, prefix, assignment.first, assignment.second);
    }
}

// Documented maximum bit lengths per limb used for regression checks.
static const std::unordered_map<std::string, size_t> kExpectedLimbBitLengths = {
    { "accumulators_binary_limbs_0", 68 },
    { "accumulators_binary_limbs_1", 68 },
    { "accumulators_binary_limbs_2", 68 },
    { "accumulators_binary_limbs_3", 50 },
    { "relation_wide_limbs", 80 },
    { "relation_wide_limbs_shift", 80 },
    { "z_low_limbs", 68 },
    { "z_low_limbs_shift", 68 },
    { "z_high_limbs", 60 },
    { "z_high_limbs_shift", 60 },
    { "p_y_low_limbs", 68 },
    { "p_y_low_limbs_shift", 68 },
    { "p_y_high_limbs", 68 },
    { "p_y_high_limbs_shift", 50 },
    { "p_x_low_limbs", 68 },
    { "p_x_low_limbs_shift", 68 },
    { "p_x_high_limbs", 68 },
    { "p_x_high_limbs_shift", 50 },
    { "quotient_low_binary_limbs", 68 },
    { "quotient_low_binary_limbs_shift", 68 },
    { "quotient_high_binary_limbs", 68 },
    { "quotient_high_binary_limbs_shift", 52 }
};

static void apply_expected_limb_bounds(smt_solver::Solver& s,
                                       const std::vector<smt_terms::STerm>& vars,
                                       const std::vector<std::string>& names,
                                       const std::string& prefix)
{
    smt_terms::STerm zero = smt_terms::FFIConst("0", &s, 10);
    const std::string prefix_with_sep = prefix.empty() ? std::string() : prefix + "_";

    auto apply_bound = [&](const std::string& name, const smt_terms::STerm& var) {
        auto it = kExpectedLimbBitLengths.find(name);
        if (it == kExpectedLimbBitLengths.end()) {
            return; // Skip parameters and other non-limb variables
        }

        uint64_t bits = it->second;
        uint256_t upper = (uint256_t(1) << bits) - uint256_t(1);
        smt_terms::STerm upper_term = smt_terms::FFIConst(to_dec_string(upper), &s, 10);

        s.assertFormula(
            s.term_manager.mkTerm(cvc5::Kind::GEQ, { static_cast<cvc5::Term>(var), static_cast<cvc5::Term>(zero) }));
        s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::LEQ,
                                              { static_cast<cvc5::Term>(var), static_cast<cvc5::Term>(upper_term) }));
    };

    for (size_t i = 0; i < names.size(); ++i) {
        std::string base_name = names[i];
        if (!prefix_with_sep.empty()) {
            if (base_name.rfind(prefix_with_sep, 0) != 0) {
                continue;
            }
            base_name = base_name.substr(prefix_with_sep.size());
        }

        apply_bound(base_name, vars[i]);

        if (base_name == "relation_wide_limbs") {
            apply_bound("relation_wide_limbs_shift", vars[i]);
        } else if (base_name == "relation_wide_limbs_shift") {
            apply_bound("relation_wide_limbs", vars[i]);
        }
    }
}

} // namespace

using namespace bb;

TEST(TranslatorNonNativeFieldRelation, test_translator_non_native_field_relation)
{
    smt_solver::Solver s("30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001",
                         smt_solver::debug_solver_config);

    auto recording_trace_nnf = smt_translator_relations::record_translator_non_native_field_relation();

    std::vector<smt_terms::STerm> nnf_formulas;
    std::vector<smt_terms::STerm> nnf_vars;
    std::vector<std::string> nnf_names;
    smt_translator_relations::replay_translator_non_native_field_relation(
        recording_trace_nnf, &s, "nnf", true, nnf_formulas, nnf_vars, nnf_names);

    auto random_previous_accumulator = uint256_t(bb::fq::random_element());
    auto random_evaluation_input = uint256_t(bb::fq::random_element());
    auto random_batching_challenge_v_0 = uint256_t(bb::fq::random_element());
    auto random_batching_challenge_v_1 = uint256_t(bb::fq::random_element());
    auto random_batching_challenge_v_2 = uint256_t(bb::fq::random_element());
    auto random_batching_challenge_v_3 = uint256_t(bb::fq::random_element());
    auto accumulator_chunk_0 = random_previous_accumulator.slice(0, 68);
    auto accumulator_chunk_1 = random_previous_accumulator.slice(68, 136);
    auto accumulator_chunk_2 = random_previous_accumulator.slice(136, 204);
    auto accumulator_chunk_3 = random_previous_accumulator.slice(204, 272);
    auto evaluation_input_chunk_0 = random_evaluation_input.slice(0, 68);
    auto evaluation_input_chunk_1 = random_evaluation_input.slice(68, 136);
    auto evaluation_input_chunk_2 = random_evaluation_input.slice(136, 204);
    auto evaluation_input_chunk_3 = random_evaluation_input.slice(204, 272);
    auto evaluation_input_chunk_4 = random_evaluation_input % bb::fr::modulus;
    auto batching_challenge_v_0_chunk_0 = random_batching_challenge_v_0.slice(0, 68);
    auto batching_challenge_v_0_chunk_1 = random_batching_challenge_v_0.slice(68, 136);
    auto batching_challenge_v_0_chunk_2 = random_batching_challenge_v_0.slice(136, 204);
    auto batching_challenge_v_0_chunk_3 = random_batching_challenge_v_0.slice(204, 272);
    auto batching_challenge_v_0_chunk_4 = random_batching_challenge_v_0 % bb::fr::modulus;
    auto batching_challenge_v_1_chunk_0 = random_batching_challenge_v_1.slice(0, 68);
    auto batching_challenge_v_1_chunk_1 = random_batching_challenge_v_1.slice(68, 136);
    auto batching_challenge_v_1_chunk_2 = random_batching_challenge_v_1.slice(136, 204);
    auto batching_challenge_v_1_chunk_3 = random_batching_challenge_v_1.slice(204, 272);
    auto batching_challenge_v_1_chunk_4 = random_batching_challenge_v_1 % bb::fr::modulus;
    auto batching_challenge_v_2_chunk_0 = random_batching_challenge_v_2.slice(0, 68);
    auto batching_challenge_v_2_chunk_1 = random_batching_challenge_v_2.slice(68, 136);
    auto batching_challenge_v_2_chunk_2 = random_batching_challenge_v_2.slice(136, 204);
    auto batching_challenge_v_2_chunk_3 = random_batching_challenge_v_2.slice(204, 272);
    auto batching_challenge_v_2_chunk_4 = random_batching_challenge_v_2 % bb::fr::modulus;
    auto batching_challenge_v_3_chunk_0 = random_batching_challenge_v_3.slice(0, 68);
    auto batching_challenge_v_3_chunk_1 = random_batching_challenge_v_3.slice(68, 136);
    auto batching_challenge_v_3_chunk_2 = random_batching_challenge_v_3.slice(136, 204);
    auto batching_challenge_v_3_chunk_3 = random_batching_challenge_v_3.slice(204, 272);
    auto batching_challenge_v_3_chunk_4 = random_batching_challenge_v_3 % bb::fr::modulus;
    // TODO: Specify valid parameter values here
    // You can modify these uint256_t values to test different scenarios
    // The relation checks accumulator limb uniqueness given these parameters
    std::vector<std::pair<std::string, uint256_t>> params = {
        // Shifted accumulator limbs (4 limbs)
        { "accumulators_binary_limbs_0_shift", accumulator_chunk_0 },
        { "accumulators_binary_limbs_1_shift", accumulator_chunk_1 },
        { "accumulators_binary_limbs_2_shift", accumulator_chunk_2 },
        { "accumulators_binary_limbs_3_shift", accumulator_chunk_3 },
        // Evaluation input (5 limbs)
        { "evaluation_input_x_0", evaluation_input_chunk_0 },
        { "evaluation_input_x_1", evaluation_input_chunk_1 },
        { "evaluation_input_x_2", evaluation_input_chunk_2 },
        { "evaluation_input_x_3", evaluation_input_chunk_3 },
        { "evaluation_input_x_4", evaluation_input_chunk_4 },
        // Batching challenge v[0] (5 limbs)
        { "batching_challenge_v_0_0", batching_challenge_v_0_chunk_0 },
        { "batching_challenge_v_0_1", batching_challenge_v_0_chunk_1 },
        { "batching_challenge_v_0_2", batching_challenge_v_0_chunk_2 },
        { "batching_challenge_v_0_3", batching_challenge_v_0_chunk_3 },
        { "batching_challenge_v_0_4", batching_challenge_v_0_chunk_4 },
        // Batching challenge v[1] (5 limbs)
        { "batching_challenge_v_1_0", batching_challenge_v_1_chunk_0 },
        { "batching_challenge_v_1_1", batching_challenge_v_1_chunk_1 },
        { "batching_challenge_v_1_2", batching_challenge_v_1_chunk_2 },
        { "batching_challenge_v_1_3", batching_challenge_v_1_chunk_3 },
        { "batching_challenge_v_1_4", batching_challenge_v_1_chunk_4 },
        // Batching challenge v[2] (5 limbs)
        { "batching_challenge_v_2_0", batching_challenge_v_2_chunk_0 },
        { "batching_challenge_v_2_1", batching_challenge_v_2_chunk_1 },
        { "batching_challenge_v_2_2", batching_challenge_v_2_chunk_2 },
        { "batching_challenge_v_2_3", batching_challenge_v_2_chunk_3 },
        { "batching_challenge_v_2_4", batching_challenge_v_2_chunk_4 },
        // Batching challenge v[3] (5 limbs)
        { "batching_challenge_v_3_0", batching_challenge_v_3_chunk_0 },
        { "batching_challenge_v_3_1", batching_challenge_v_3_chunk_1 },
        { "batching_challenge_v_3_2", batching_challenge_v_3_chunk_2 },
        { "batching_challenge_v_3_3", batching_challenge_v_3_chunk_3 },
        { "batching_challenge_v_3_4", batching_challenge_v_3_chunk_4 }
    };

    set_relation_parameters(nnf_vars, nnf_names, s, "nnf", params);

    // Print the formulas generated by the relation
    std::cerr << "\n" << std::string(80, '=') << "\n";
    std::cerr << "Non-Native Field Relation Formulas (" << nnf_formulas.size() << " total)\n";
    std::cerr << std::string(80, '=') << "\n";
    for (size_t i = 0; i < nnf_formulas.size(); ++i) {
        std::cerr << "Formula " << i << ": " << static_cast<cvc5::Term>(nnf_formulas[i]) << "\n";
    }
    std::cerr << std::string(80, '=') << "\n\n";

    smt_translator_relations::assert_formulas_zero(&s, nnf_formulas);
    apply_expected_limb_bounds(s, nnf_vars, nnf_names, "nnf");

    // First, verify the system is satisfiable with these constraints
    ASSERT_TRUE(s.check());

    // Now check uniqueness: create a second instantiation with the same parameters
    s.push();

    std::vector<smt_terms::STerm> nnf_formulas_2;
    std::vector<smt_terms::STerm> nnf_vars_2;
    std::vector<std::string> nnf_names_2;
    smt_translator_relations::replay_translator_non_native_field_relation(
        recording_trace_nnf, &s, "nnf_alt", true, nnf_formulas_2, nnf_vars_2, nnf_names_2);

    // Apply the same parameter values to the second instantiation
    set_relation_parameters(nnf_vars_2, nnf_names_2, s, "nnf_alt", params);

    smt_translator_relations::assert_formulas_zero(&s, nnf_formulas_2);
    apply_expected_limb_bounds(s, nnf_vars_2, nnf_names_2, "nnf_alt");

    // Helper to find a variable by name
    auto find_var = [&](const std::vector<std::string>& list,
                        const std::vector<smt_terms::STerm>& values,
                        const std::string& target) -> smt_terms::STerm {
        auto it = std::find(list.begin(), list.end(), target);
        if (it == list.end()) {
            throw std::runtime_error("Failed to find variable: " + target);
        }
        size_t index = static_cast<size_t>(std::distance(list.begin(), it));
        return values[index];
    };

    // Assert that at least one accumulator limb differs
    smt_terms::STerm zero = smt_terms::FFIConst("0", &s, 10);
    std::vector<cvc5::Term> differs;

    for (size_t i = 0; i < 4; ++i) {
        std::string limb_name = "accumulators_binary_limbs_" + std::to_string(i);
        smt_terms::STerm lhs = find_var(nnf_names, nnf_vars, "nnf_" + limb_name);
        smt_terms::STerm rhs = find_var(nnf_names_2, nnf_vars_2, "nnf_alt_" + limb_name);
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

    // The system should be UNSAT, proving uniqueness of accumulator_binary_limbs
    // given the shifted limbs, evaluation parameters, and batching challenge parameters.
    ASSERT_FALSE(s.check());

    s.pop();
}
