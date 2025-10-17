#include <gtest/gtest.h>

#include "barretenberg/smt_verification/solver/solver.hpp"
#include "barretenberg/smt_verification/terms/term.hpp"
#include "eccvm_relations.hpp"

using namespace bb;
using namespace smt_solver;
using namespace smt_terms;

/**
 * Test that the ECC MSM relation addition operation has unique outputs
 *
 * We prove output uniqueness by creating two instances of the MSM relation and showing:
 * If all inputs are equal AND msm_add = 1 (we're in an ADD state),
 * then the outputs (msm_accumulator_x_shift, msm_accumulator_y_shift) must be equal
 *
 * This uses the actual recorded MSM relation formulas from ecc_msm_relation_impl.hpp
 * The msm_add = 1 constraint is critical because the accumulator output constraints
 * are only active during ADD operations (see ecc_msm_relation_impl.hpp lines 344-346)
 *
 * Runtime: ~312ms
 */
TEST(ECCVMMSMRelation, AdditionOutputsAreUnique)
{
    const char* modulus = "30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001";

    // Record the MSM relation once
    auto trace = smt_eccvm_relations::record_eccvm_msm_relation();

    Solver s(modulus, default_solver_config);

    // Create TWO instances of the relation
    std::vector<STerm> formulas_A;
    std::vector<STerm> vars_A;
    std::vector<std::string> names_A;
    smt_eccvm_relations::replay_eccvm_msm_relation(trace, &s, "A", false, formulas_A, vars_A, names_A);

    std::vector<STerm> formulas_B;
    std::vector<STerm> vars_B;
    std::vector<std::string> names_B;
    smt_eccvm_relations::replay_eccvm_msm_relation(trace, &s, "B", false, formulas_B, vars_B, names_B);

    // Helper to find variables by name
    auto find_var =
        [](const std::vector<STerm>& vars, const std::vector<std::string>& names, const std::string& name) -> STerm {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return vars[i];
            }
        }
        throw std::runtime_error("Variable not found: " + name);
    };

    // Find the output variables (shifted accumulator coordinates)
    STerm acc_x_shift_A = find_var(vars_A, names_A, "A_msm_accumulator_x_shift");
    STerm acc_y_shift_A = find_var(vars_A, names_A, "A_msm_accumulator_y_shift");
    STerm acc_x_shift_B = find_var(vars_B, names_B, "B_msm_accumulator_x_shift");
    STerm acc_y_shift_B = find_var(vars_B, names_B, "B_msm_accumulator_y_shift");

    // Assert all the MSM relation formulas for both instances (zero = 0)
    STerm zero = FFConst("0", &s, 10);
    for (const auto& formula : formulas_A) {
        formula == zero;
    }
    for (const auto& formula : formulas_B) {
        formula == zero;
    }

    // Assert that all INPUT variables are equal between instances
    // We need to constrain all non-shift variables to be equal
    for (size_t i = 0; i < names_A.size(); ++i) {
        const std::string& name_A = names_A[i];

        // Remove the prefix to get the base name
        std::string base_name_A = name_A.substr(2); // Remove "A_"

        // Only constrain non-shift variables (inputs)
        // Shift variables are outputs and should not be constrained to be equal
        if (base_name_A.find("_shift") == std::string::npos) {
            vars_A[i] == vars_B[i];
        }
    }

    // CRITICAL: Ensure we're in an ADD state by constraining msm_add = 1
    // Otherwise the accumulator outputs are not constrained by the addition formulas
    STerm msm_add_A = find_var(vars_A, names_A, "A_msm_add");
    STerm one = FFConst("1", &s, 10);
    msm_add_A == one;

    // Now assert that at least one OUTPUT is different
    cvc5::Term x_different = s.term_manager.mkTerm(
        cvc5::Kind::DISTINCT, { static_cast<cvc5::Term>(acc_x_shift_A), static_cast<cvc5::Term>(acc_x_shift_B) });
    cvc5::Term y_different = s.term_manager.mkTerm(
        cvc5::Kind::DISTINCT, { static_cast<cvc5::Term>(acc_y_shift_A), static_cast<cvc5::Term>(acc_y_shift_B) });

    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::OR, { x_different, y_different }));

    // Should be UNSAT: if all inputs are equal and relation holds, outputs must be equal
    ASSERT_FALSE(s.check()) << "If all inputs are equal and MSM ADD relation holds, outputs must be equal";
}

/**
 * Test that the ECC MSM relation DOUBLE operation has unique outputs
 *
 * We prove output uniqueness by creating two instances of the MSM relation and showing:
 * If all inputs are equal AND msm_double = 1 (we're in a DOUBLE state),
 * then the outputs (msm_accumulator_x_shift, msm_accumulator_y_shift) must be equal
 *
 * This tests subrelations 10-12 which constrain the DOUBLE operation
 * (see ecc_msm_relation_impl.hpp lines 383-385)
 *
 * Runtime: ~46ms
 */
TEST(ECCVMMSMRelation, DoubleOutputsAreUnique)
{
    const char* modulus = "30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001";

    auto trace = smt_eccvm_relations::record_eccvm_msm_relation();
    Solver s(modulus, default_solver_config);

    std::vector<STerm> formulas_A;
    std::vector<STerm> vars_A;
    std::vector<std::string> names_A;
    smt_eccvm_relations::replay_eccvm_msm_relation(trace, &s, "A", false, formulas_A, vars_A, names_A);

    std::vector<STerm> formulas_B;
    std::vector<STerm> vars_B;
    std::vector<std::string> names_B;
    smt_eccvm_relations::replay_eccvm_msm_relation(trace, &s, "B", false, formulas_B, vars_B, names_B);

    auto find_var =
        [](const std::vector<STerm>& vars, const std::vector<std::string>& names, const std::string& name) -> STerm {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return vars[i];
            }
        }
        throw std::runtime_error("Variable not found: " + name);
    };

    STerm acc_x_shift_A = find_var(vars_A, names_A, "A_msm_accumulator_x_shift");
    STerm acc_y_shift_A = find_var(vars_A, names_A, "A_msm_accumulator_y_shift");
    STerm acc_x_shift_B = find_var(vars_B, names_B, "B_msm_accumulator_x_shift");
    STerm acc_y_shift_B = find_var(vars_B, names_B, "B_msm_accumulator_y_shift");

    STerm zero = FFConst("0", &s, 10);
    for (const auto& formula : formulas_A) {
        formula == zero;
    }
    for (const auto& formula : formulas_B) {
        formula == zero;
    }

    for (size_t i = 0; i < names_A.size(); ++i) {
        const std::string& name_A = names_A[i];
        std::string base_name_A = name_A.substr(2);
        if (base_name_A.find("_shift") == std::string::npos) {
            vars_A[i] == vars_B[i];
        }
    }

    // Constrain msm_double = 1 to ensure we're testing a DOUBLE operation
    STerm msm_double_A = find_var(vars_A, names_A, "A_msm_double");
    STerm one = FFConst("1", &s, 10);
    msm_double_A == one;

    cvc5::Term x_different = s.term_manager.mkTerm(
        cvc5::Kind::DISTINCT, { static_cast<cvc5::Term>(acc_x_shift_A), static_cast<cvc5::Term>(acc_x_shift_B) });
    cvc5::Term y_different = s.term_manager.mkTerm(
        cvc5::Kind::DISTINCT, { static_cast<cvc5::Term>(acc_y_shift_A), static_cast<cvc5::Term>(acc_y_shift_B) });

    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::OR, { x_different, y_different }));

    ASSERT_FALSE(s.check()) << "If all inputs are equal and MSM DOUBLE relation holds, outputs must be equal";
}

/**
 * Test that the ECC MSM relation SKEW operation has unique outputs
 *
 * We prove output uniqueness by creating two instances of the MSM relation and showing:
 * If all inputs are equal AND msm_skew = 1 (we're in a SKEW state),
 * then the outputs (msm_accumulator_x_shift, msm_accumulator_y_shift) must be equal
 *
 * This tests subrelations 3-5 which constrain the SKEW operation
 * (see ecc_msm_relation_impl.hpp lines 421-423)
 *
 * Runtime: ~88ms
 */
TEST(ECCVMMSMRelation, SkewOutputsAreUnique)
{
    const char* modulus = "30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001";

    auto trace = smt_eccvm_relations::record_eccvm_msm_relation();
    Solver s(modulus, default_solver_config);

    std::vector<STerm> formulas_A;
    std::vector<STerm> vars_A;
    std::vector<std::string> names_A;
    smt_eccvm_relations::replay_eccvm_msm_relation(trace, &s, "A", false, formulas_A, vars_A, names_A);

    std::vector<STerm> formulas_B;
    std::vector<STerm> vars_B;
    std::vector<std::string> names_B;
    smt_eccvm_relations::replay_eccvm_msm_relation(trace, &s, "B", false, formulas_B, vars_B, names_B);

    auto find_var =
        [](const std::vector<STerm>& vars, const std::vector<std::string>& names, const std::string& name) -> STerm {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return vars[i];
            }
        }
        throw std::runtime_error("Variable not found: " + name);
    };

    STerm acc_x_shift_A = find_var(vars_A, names_A, "A_msm_accumulator_x_shift");
    STerm acc_y_shift_A = find_var(vars_A, names_A, "A_msm_accumulator_y_shift");
    STerm acc_x_shift_B = find_var(vars_B, names_B, "B_msm_accumulator_x_shift");
    STerm acc_y_shift_B = find_var(vars_B, names_B, "B_msm_accumulator_y_shift");

    STerm zero = FFConst("0", &s, 10);
    for (const auto& formula : formulas_A) {
        formula == zero;
    }
    for (const auto& formula : formulas_B) {
        formula == zero;
    }

    for (size_t i = 0; i < names_A.size(); ++i) {
        const std::string& name_A = names_A[i];
        std::string base_name_A = name_A.substr(2);
        if (base_name_A.find("_shift") == std::string::npos) {
            vars_A[i] == vars_B[i];
        }
    }

    // Constrain msm_skew = 1 to ensure we're testing a SKEW operation
    STerm msm_skew_A = find_var(vars_A, names_A, "A_msm_skew");
    STerm one = FFConst("1", &s, 10);
    msm_skew_A == one;

    cvc5::Term x_different = s.term_manager.mkTerm(
        cvc5::Kind::DISTINCT, { static_cast<cvc5::Term>(acc_x_shift_A), static_cast<cvc5::Term>(acc_x_shift_B) });
    cvc5::Term y_different = s.term_manager.mkTerm(
        cvc5::Kind::DISTINCT, { static_cast<cvc5::Term>(acc_y_shift_A), static_cast<cvc5::Term>(acc_y_shift_B) });

    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::OR, { x_different, y_different }));

    ASSERT_FALSE(s.check()) << "If all inputs are equal and MSM SKEW relation holds, outputs must be equal";
}

/**
 * Sanity check: when msm_add = 0, outputs should NOT be uniquely constrained
 *
 * This verifies that the msm_add selector is actually controlling the ADD operation constraints.
 * When msm_add = 0, the relation should allow different outputs for the same inputs (SAT).
 *
 * Runtime: ~5347ms (SAT is slower than UNSAT as it requires finding a witness)
 */
TEST(ECCVMMSMRelation, AdditionOutputsNotConstrainedWhenSelectorZero)
{
    const char* modulus = "30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001";

    auto trace = smt_eccvm_relations::record_eccvm_msm_relation();
    Solver s(modulus, default_solver_config);

    std::vector<STerm> formulas_A;
    std::vector<STerm> vars_A;
    std::vector<std::string> names_A;
    smt_eccvm_relations::replay_eccvm_msm_relation(trace, &s, "A", false, formulas_A, vars_A, names_A);

    std::vector<STerm> formulas_B;
    std::vector<STerm> vars_B;
    std::vector<std::string> names_B;
    smt_eccvm_relations::replay_eccvm_msm_relation(trace, &s, "B", false, formulas_B, vars_B, names_B);

    auto find_var =
        [](const std::vector<STerm>& vars, const std::vector<std::string>& names, const std::string& name) -> STerm {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return vars[i];
            }
        }
        throw std::runtime_error("Variable not found: " + name);
    };

    STerm acc_x_shift_A = find_var(vars_A, names_A, "A_msm_accumulator_x_shift");
    STerm acc_y_shift_A = find_var(vars_A, names_A, "A_msm_accumulator_y_shift");
    STerm acc_x_shift_B = find_var(vars_B, names_B, "B_msm_accumulator_x_shift");
    STerm acc_y_shift_B = find_var(vars_B, names_B, "B_msm_accumulator_y_shift");

    STerm zero = FFConst("0", &s, 10);
    for (const auto& formula : formulas_A) {
        formula == zero;
    }
    for (const auto& formula : formulas_B) {
        formula == zero;
    }

    for (size_t i = 0; i < names_A.size(); ++i) {
        const std::string& name_A = names_A[i];
        std::string base_name_A = name_A.substr(2);
        if (base_name_A.find("_shift") == std::string::npos) {
            vars_A[i] == vars_B[i];
        }
    }

    // SANITY CHECK: Set msm_add = 0, outputs should NOT be uniquely constrained
    STerm msm_add_A = find_var(vars_A, names_A, "A_msm_add");
    msm_add_A == zero;

    cvc5::Term x_different = s.term_manager.mkTerm(
        cvc5::Kind::DISTINCT, { static_cast<cvc5::Term>(acc_x_shift_A), static_cast<cvc5::Term>(acc_x_shift_B) });
    cvc5::Term y_different = s.term_manager.mkTerm(
        cvc5::Kind::DISTINCT, { static_cast<cvc5::Term>(acc_y_shift_A), static_cast<cvc5::Term>(acc_y_shift_B) });

    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::OR, { x_different, y_different }));

    // Should be SAT: when msm_add = 0, outputs are not constrained by ADD formulas
    ASSERT_TRUE(s.check()) << "When msm_add = 0, outputs should not be uniquely determined";
}

/**
 * Sanity check: when msm_double = 0, outputs should NOT be uniquely constrained
 *
 * This verifies that the msm_double selector is actually controlling the DOUBLE operation constraints.
 * When msm_double = 0, the relation should allow different outputs for the same inputs (SAT).
 *
 * Runtime: ~4481ms (SAT is slower than UNSAT as it requires finding a witness)
 */
TEST(ECCVMMSMRelation, DoubleOutputsNotConstrainedWhenSelectorZero)
{
    const char* modulus = "30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001";

    auto trace = smt_eccvm_relations::record_eccvm_msm_relation();
    Solver s(modulus, default_solver_config);

    std::vector<STerm> formulas_A;
    std::vector<STerm> vars_A;
    std::vector<std::string> names_A;
    smt_eccvm_relations::replay_eccvm_msm_relation(trace, &s, "A", false, formulas_A, vars_A, names_A);

    std::vector<STerm> formulas_B;
    std::vector<STerm> vars_B;
    std::vector<std::string> names_B;
    smt_eccvm_relations::replay_eccvm_msm_relation(trace, &s, "B", false, formulas_B, vars_B, names_B);

    auto find_var =
        [](const std::vector<STerm>& vars, const std::vector<std::string>& names, const std::string& name) -> STerm {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return vars[i];
            }
        }
        throw std::runtime_error("Variable not found: " + name);
    };

    STerm acc_x_shift_A = find_var(vars_A, names_A, "A_msm_accumulator_x_shift");
    STerm acc_y_shift_A = find_var(vars_A, names_A, "A_msm_accumulator_y_shift");
    STerm acc_x_shift_B = find_var(vars_B, names_B, "B_msm_accumulator_x_shift");
    STerm acc_y_shift_B = find_var(vars_B, names_B, "B_msm_accumulator_y_shift");

    STerm zero = FFConst("0", &s, 10);
    for (const auto& formula : formulas_A) {
        formula == zero;
    }
    for (const auto& formula : formulas_B) {
        formula == zero;
    }

    for (size_t i = 0; i < names_A.size(); ++i) {
        const std::string& name_A = names_A[i];
        std::string base_name_A = name_A.substr(2);
        if (base_name_A.find("_shift") == std::string::npos) {
            vars_A[i] == vars_B[i];
        }
    }

    // SANITY CHECK: Set msm_double = 0, outputs should NOT be uniquely constrained
    STerm msm_double_A = find_var(vars_A, names_A, "A_msm_double");
    msm_double_A == zero;

    cvc5::Term x_different = s.term_manager.mkTerm(
        cvc5::Kind::DISTINCT, { static_cast<cvc5::Term>(acc_x_shift_A), static_cast<cvc5::Term>(acc_x_shift_B) });
    cvc5::Term y_different = s.term_manager.mkTerm(
        cvc5::Kind::DISTINCT, { static_cast<cvc5::Term>(acc_y_shift_A), static_cast<cvc5::Term>(acc_y_shift_B) });

    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::OR, { x_different, y_different }));

    // Should be SAT: when msm_double = 0, outputs are not constrained by DOUBLE formulas
    ASSERT_TRUE(s.check()) << "When msm_double = 0, outputs should not be uniquely determined";
}

/**
 * Sanity check: when msm_skew = 0, outputs should NOT be uniquely constrained
 *
 * This verifies that the msm_skew selector is actually controlling the SKEW operation constraints.
 * When msm_skew = 0, the relation should allow different outputs for the same inputs (SAT).
 *
 * Runtime: ~5041ms (SAT is slower than UNSAT as it requires finding a witness)
 */
TEST(ECCVMMSMRelation, SkewOutputsNotConstrainedWhenSelectorZero)
{
    const char* modulus = "30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001";

    auto trace = smt_eccvm_relations::record_eccvm_msm_relation();
    Solver s(modulus, default_solver_config);

    std::vector<STerm> formulas_A;
    std::vector<STerm> vars_A;
    std::vector<std::string> names_A;
    smt_eccvm_relations::replay_eccvm_msm_relation(trace, &s, "A", false, formulas_A, vars_A, names_A);

    std::vector<STerm> formulas_B;
    std::vector<STerm> vars_B;
    std::vector<std::string> names_B;
    smt_eccvm_relations::replay_eccvm_msm_relation(trace, &s, "B", false, formulas_B, vars_B, names_B);

    auto find_var =
        [](const std::vector<STerm>& vars, const std::vector<std::string>& names, const std::string& name) -> STerm {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return vars[i];
            }
        }
        throw std::runtime_error("Variable not found: " + name);
    };

    STerm acc_x_shift_A = find_var(vars_A, names_A, "A_msm_accumulator_x_shift");
    STerm acc_y_shift_A = find_var(vars_A, names_A, "A_msm_accumulator_y_shift");
    STerm acc_x_shift_B = find_var(vars_B, names_B, "B_msm_accumulator_x_shift");
    STerm acc_y_shift_B = find_var(vars_B, names_B, "B_msm_accumulator_y_shift");

    STerm zero = FFConst("0", &s, 10);
    for (const auto& formula : formulas_A) {
        formula == zero;
    }
    for (const auto& formula : formulas_B) {
        formula == zero;
    }

    for (size_t i = 0; i < names_A.size(); ++i) {
        const std::string& name_A = names_A[i];
        std::string base_name_A = name_A.substr(2);
        if (base_name_A.find("_shift") == std::string::npos) {
            vars_A[i] == vars_B[i];
        }
    }

    // SANITY CHECK: Set msm_skew = 0, outputs should NOT be uniquely constrained
    STerm msm_skew_A = find_var(vars_A, names_A, "A_msm_skew");
    msm_skew_A == zero;

    cvc5::Term x_different = s.term_manager.mkTerm(
        cvc5::Kind::DISTINCT, { static_cast<cvc5::Term>(acc_x_shift_A), static_cast<cvc5::Term>(acc_x_shift_B) });
    cvc5::Term y_different = s.term_manager.mkTerm(
        cvc5::Kind::DISTINCT, { static_cast<cvc5::Term>(acc_y_shift_A), static_cast<cvc5::Term>(acc_y_shift_B) });

    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::OR, { x_different, y_different }));

    // Should be SAT: when msm_skew = 0, outputs are not constrained by SKEW formulas
    ASSERT_TRUE(s.check()) << "When msm_skew = 0, outputs should not be uniquely determined";
}
