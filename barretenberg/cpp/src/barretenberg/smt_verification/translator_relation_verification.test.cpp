#include <gtest/gtest.h>

#include "barretenberg/numeric/uint256/uint256.hpp"
#include "barretenberg/relations/translator_vm/translator_decomposition_relation.hpp"
#include "barretenberg/smt_verification/relations/translator_vm/translator_relations.hpp"
#include "barretenberg/smt_verification/solver/solver.hpp"
#include <iomanip>
#include <sstream>

using namespace bb;

/**
 * @brief Helper function to convert decimal string to uint256_t
 */
uint256_t uint256_from_decimal_string(const std::string& dec_str)
{
    uint256_t result = 0;
    uint256_t base = 10;
    for (char c : dec_str) {
        if (c < '0' || c > '9') {
            throw std::runtime_error("Invalid decimal string");
        }
        result = result * base + uint256_t(static_cast<uint64_t>(c - '0'));
    }
    return result;
}

/**
 * @brief Decomposition mapping for translator VM limbs
 *
 * This structure documents which relation index corresponds to which limb decomposition,
 * and which range constraints are used.
 */
struct LimbDecomposition {
    std::string limb_name;             // The limb being decomposed (e.g., "p_x_low_limbs")
    size_t relation_index;             // The relation index (0-47)
    std::vector<std::string> rc_names; // Range constraint names (in order: rc_0, rc_1, ..., rc_tail)
    size_t num_microlimbs;             // Number of 14-bit microlimbs (usually 5, but 4 for top 50-bit limbs)
    std::string tail_relation_desc;    // Description of tail constraint relation
    size_t tail_relation_index;        // The relation index for tail constraint
};

/**
 * @brief Get decomposition mapping for all translator VM limbs
 */
std::vector<LimbDecomposition> get_translator_decomposition_map()
{
    return {
        // Accumulator decompositions (relations 0-3)
        { "accumulators_binary_limbs_0",
          0,
          { "accumulator_low_limbs_range_constraint_0",
            "accumulator_low_limbs_range_constraint_1",
            "accumulator_low_limbs_range_constraint_2",
            "accumulator_low_limbs_range_constraint_3",
            "accumulator_low_limbs_range_constraint_4",
            "accumulator_low_limbs_range_constraint_tail" },
          5,
          "rc_4 * 4 = tail",
          34 },

        { "accumulators_binary_limbs_1",
          1,
          { "accumulator_low_limbs_range_constraint_0_shift",
            "accumulator_low_limbs_range_constraint_1_shift",
            "accumulator_low_limbs_range_constraint_2_shift",
            "accumulator_low_limbs_range_constraint_3_shift",
            "accumulator_low_limbs_range_constraint_4_shift",
            "accumulator_low_limbs_range_constraint_tail_shift" },
          5,
          "rc_4_shift * 4 = tail_shift",
          35 },

        { "accumulators_binary_limbs_2",
          2,
          { "accumulator_high_limbs_range_constraint_0",
            "accumulator_high_limbs_range_constraint_1",
            "accumulator_high_limbs_range_constraint_2",
            "accumulator_high_limbs_range_constraint_3",
            "accumulator_high_limbs_range_constraint_4",
            "accumulator_high_limbs_range_constraint_tail" },
          5,
          "rc_4 * 4 = tail",
          36 },

        { "accumulators_binary_limbs_3",
          3,
          { "accumulator_high_limbs_range_constraint_0_shift",
            "accumulator_high_limbs_range_constraint_1_shift",
            "accumulator_high_limbs_range_constraint_2_shift",
            "accumulator_high_limbs_range_constraint_3_shift" },
          4,
          "rc_3_shift * 64 = rc_4_shift (8-bit top limb)",
          37 },

        // P_x decompositions (relations 12-15)
        { "p_x_low_limbs",
          12,
          { "p_x_low_limbs_range_constraint_0",
            "p_x_low_limbs_range_constraint_1",
            "p_x_low_limbs_range_constraint_2",
            "p_x_low_limbs_range_constraint_3",
            "p_x_low_limbs_range_constraint_4",
            "p_x_low_limbs_range_constraint_tail" },
          5,
          "rc_4 * 4 = tail",
          22 },

        { "p_x_low_limbs_shift",
          13,
          { "p_x_low_limbs_range_constraint_0_shift",
            "p_x_low_limbs_range_constraint_1_shift",
            "p_x_low_limbs_range_constraint_2_shift",
            "p_x_low_limbs_range_constraint_3_shift",
            "p_x_low_limbs_range_constraint_4_shift",
            "p_x_low_limbs_range_constraint_tail_shift" },
          5,
          "rc_4_shift * 4 = tail_shift",
          23 },

        { "p_x_high_limbs",
          14,
          { "p_x_high_limbs_range_constraint_0",
            "p_x_high_limbs_range_constraint_1",
            "p_x_high_limbs_range_constraint_2",
            "p_x_high_limbs_range_constraint_3",
            "p_x_high_limbs_range_constraint_4",
            "p_x_high_limbs_range_constraint_tail" },
          5,
          "rc_4 * 4 = tail",
          24 },

        { "p_x_high_limbs_shift",
          15,
          { "p_x_high_limbs_range_constraint_0_shift",
            "p_x_high_limbs_range_constraint_1_shift",
            "p_x_high_limbs_range_constraint_2_shift",
            "p_x_high_limbs_range_constraint_3_shift" },
          4,
          "rc_3_shift * 64 = rc_4_shift (8-bit top limb)",
          25 },
    };
}

/**
 * @brief Find limb and its range constraints by decomposition mapping
 */
struct MappedLimbVariables {
    smt_terms::STerm limb_var;
    std::vector<smt_terms::STerm> rc_vars; // rc_0, rc_1, ..., rc_n
    smt_terms::STerm tail_var;             // tail or rc_4_shift for top limbs
    bool found_all;
    LimbDecomposition decomp_info;
};

MappedLimbVariables find_mapped_limb_variables(const std::vector<smt_terms::STerm>& vars,
                                               const std::vector<std::string>& names,
                                               const LimbDecomposition& decomp,
                                               const std::string& prefix = "")
{
    MappedLimbVariables result;
    result.decomp_info = decomp;
    result.found_all = false;

    std::string limb_full_name = prefix.empty() ? decomp.limb_name : prefix + "_" + decomp.limb_name;

    // Find limb
    bool found_limb = false;
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == limb_full_name) {
            result.limb_var = vars[i];
            found_limb = true;
            break;
        }
    }

    if (!found_limb) {
        return result;
    }

    // Find range constraints
    result.rc_vars.resize(decomp.rc_names.size());
    std::vector<bool> found_rc(decomp.rc_names.size(), false);

    for (size_t i = 0; i < names.size(); ++i) {
        for (size_t j = 0; j < decomp.rc_names.size(); ++j) {
            std::string rc_full_name = prefix.empty() ? decomp.rc_names[j] : prefix + "_" + decomp.rc_names[j];
            if (names[i] == rc_full_name) {
                result.rc_vars[j] = vars[i];
                found_rc[j] = true;
                break;
            }
        }
    }

    // Tail is the last element in rc_names
    if (found_rc.back()) {
        result.tail_var = result.rc_vars.back();
    }

    result.found_all = found_limb && std::all_of(found_rc.begin(), found_rc.end(), [](bool b) { return b; });
    return result;
}

/**
 * @brief Test uniqueness and maximum value for a limb decomposition
 */
void test_limb_uniqueness_and_maximum(smt_solver::Solver& s,
                                      const LimbDecomposition& decomp,
                                      std::string& out_unique,
                                      std::string& out_max)
{
    // Test uniqueness
    s.push();

    std::vector<smt_terms::STerm> f1, v1, f2, v2;
    std::vector<std::string> n1, n2;

    smt_translator_relations::instantiate_translator_decomposition_with_iterm_return_formulas(&s, "V1", f1, v1, n1);
    smt_translator_relations::instantiate_translator_decomposition_with_iterm_return_formulas(&s, "V2", f2, v2, n2);

    smt_translator_relations::create_range_constraint_formulas(&s, v1, n1, "constraint", 16384);
    smt_translator_relations::create_range_constraint_formulas(&s, v2, n2, "constraint", 16384);

    smt_translator_relations::assert_formulas_zero(&s,
                                                   { f1[decomp.relation_index],
                                                     f1[decomp.tail_relation_index],
                                                     f2[decomp.relation_index],
                                                     f2[decomp.tail_relation_index] });

    auto limb1 = find_mapped_limb_variables(v1, n1, decomp, "V1");
    auto limb2 = find_mapped_limb_variables(v2, n2, decomp, "V2");

    // Constrain limbs equal
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(limb1.limb_var), static_cast<cvc5::Term>(limb2.limb_var) }));

    // Assert at least one RC differs
    smt_terms::STerm zero = smt_terms::FFIConst("0", &s, 10);
    std::vector<cvc5::Term> diffs;
    for (size_t i = 0; i < limb1.rc_vars.size(); ++i) {
        smt_terms::STerm diff = limb1.rc_vars[i] - limb2.rc_vars[i];
        diffs.push_back(s.term_manager.mkTerm(
            cvc5::Kind::NOT,
            { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                    { static_cast<cvc5::Term>(diff), static_cast<cvc5::Term>(zero) }) }));
    }
    cvc5::Term disj = diffs[0];
    for (size_t i = 1; i < diffs.size(); ++i) {
        disj = s.term_manager.mkTerm(cvc5::Kind::OR, { disj, diffs[i] });
    }
    s.assertFormula(disj);

    out_unique = s.check() ? "NOT_UNIQUE" : "UNIQUE";
    s.pop();

    // Test maximum value
    s.push();

    std::vector<smt_terms::STerm> fm, vm;
    std::vector<std::string> nm;

    smt_translator_relations::instantiate_translator_decomposition_with_iterm_return_formulas(&s, "M", fm, vm, nm);
    smt_translator_relations::create_range_constraint_formulas(&s, vm, nm, "constraint", 16384);
    smt_translator_relations::assert_formulas_zero(&s, { fm[decomp.relation_index], fm[decomp.tail_relation_index] });

    auto max_limb = find_mapped_limb_variables(vm, nm, decomp, "M");

    // Set all RCs to maximum (except tail which is constrained by relation)
    smt_terms::STerm max_14 = smt_terms::FFIConst("16383", &s, 10);
    // For limbs with 5 microlimbs, set first 4 to max (rc_4 and tail are constrained)
    // For limbs with 4 microlimbs (top 50-bit limbs), set first 3 to max (rc_3 and rc_4 are constrained)
    size_t rcs_to_max = decomp.num_microlimbs == 5 ? 4 : 3;
    for (size_t i = 0; i < rcs_to_max && i < max_limb.rc_vars.size(); ++i) {
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(max_limb.rc_vars[i]), static_cast<cvc5::Term>(max_14) }));
    }

    if (s.check()) {
        uint256_t max_val = uint256_from_decimal_string(s.get(max_limb.limb_var));
        std::ostringstream oss;
        oss << max_val; // uint256_t already outputs with 0x prefix
        out_max = oss.str();
    } else {
        out_max = "UNSAT";
    }

    s.pop();
}

void test_x_lo_x_hi_uniqueness_and_maximum(smt_solver::Solver& s,
                                           const std::string& var_name,
                                           std::string& out_unique,
                                           std::string& out_max)
{
    // Test uniqueness
    s.push();

    std::vector<smt_terms::STerm> f1, v1, f2, v2;
    std::vector<std::string> n1, n2;

    smt_translator_relations::instantiate_translator_decomposition_with_iterm_return_formulas(&s, "V1", f1, v1, n1);
    smt_translator_relations::instantiate_translator_decomposition_with_iterm_return_formulas(&s, "V2", f2, v2, n2);

    // Assert decomposition relations (Relation 42 & 43):
    // x_lo_y_hi = p_x_low_limbs + p_x_low_limbs_shift * 2^68
    // x_hi_z_1 = p_x_high_limbs + p_x_high_limbs_shift * 2^68
    smt_translator_relations::assert_formulas_zero(&s, { f1[42], f1[43], f2[42], f2[43] });

    // Find the variables
    smt_terms::STerm v1_low, v1_low_shift, v1_high, v1_high_shift, v1_x_lo, v1_x_hi;
    smt_terms::STerm v2_low, v2_low_shift, v2_high, v2_high_shift, v2_x_lo, v2_x_hi;

    for (size_t i = 0; i < n1.size(); ++i) {
        if (n1[i] == "V1_p_x_low_limbs")
            v1_low = v1[i];
        if (n1[i] == "V1_p_x_low_limbs_shift")
            v1_low_shift = v1[i];
        if (n1[i] == "V1_p_x_high_limbs")
            v1_high = v1[i];
        if (n1[i] == "V1_p_x_high_limbs_shift")
            v1_high_shift = v1[i];
        if (n1[i] == "V1_x_lo_y_hi")
            v1_x_lo = v1[i];
        if (n1[i] == "V1_x_hi_z_1")
            v1_x_hi = v1[i];
        if (n2[i] == "V2_p_x_low_limbs")
            v2_low = v2[i];
        if (n2[i] == "V2_p_x_low_limbs_shift")
            v2_low_shift = v2[i];
        if (n2[i] == "V2_p_x_high_limbs")
            v2_high = v2[i];
        if (n2[i] == "V2_p_x_high_limbs_shift")
            v2_high_shift = v2[i];
        if (n2[i] == "V2_x_lo_y_hi")
            v2_x_lo = v2[i];
        if (n2[i] == "V2_x_hi_z_1")
            v2_x_hi = v2[i];
    }

    // Constrain limbs to discovered maximum ranges
    smt_terms::STerm zero = smt_terms::FFIConst("0", &s, 10);
    smt_terms::STerm max_68bit = smt_terms::FFIConst("295147905179352825855", &s, 10); // 2^68 - 1
    smt_terms::STerm max_50bit = smt_terms::FFIConst("1125899906842623", &s, 10);      // 2^50 - 1

    // V1 limb range constraints
    s.assertFormula(
        s.term_manager.mkTerm(cvc5::Kind::GEQ, { static_cast<cvc5::Term>(v1_low), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::LEQ,
                                          { static_cast<cvc5::Term>(v1_low), static_cast<cvc5::Term>(max_68bit) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::GEQ,
                                          { static_cast<cvc5::Term>(v1_low_shift), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::LEQ, { static_cast<cvc5::Term>(v1_low_shift), static_cast<cvc5::Term>(max_68bit) }));
    s.assertFormula(
        s.term_manager.mkTerm(cvc5::Kind::GEQ, { static_cast<cvc5::Term>(v1_high), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::LEQ,
                                          { static_cast<cvc5::Term>(v1_high), static_cast<cvc5::Term>(max_68bit) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::GEQ,
                                          { static_cast<cvc5::Term>(v1_high_shift), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::LEQ, { static_cast<cvc5::Term>(v1_high_shift), static_cast<cvc5::Term>(max_50bit) }));

    // V2 limb range constraints
    s.assertFormula(
        s.term_manager.mkTerm(cvc5::Kind::GEQ, { static_cast<cvc5::Term>(v2_low), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::LEQ,
                                          { static_cast<cvc5::Term>(v2_low), static_cast<cvc5::Term>(max_68bit) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::GEQ,
                                          { static_cast<cvc5::Term>(v2_low_shift), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::LEQ, { static_cast<cvc5::Term>(v2_low_shift), static_cast<cvc5::Term>(max_68bit) }));
    s.assertFormula(
        s.term_manager.mkTerm(cvc5::Kind::GEQ, { static_cast<cvc5::Term>(v2_high), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::LEQ,
                                          { static_cast<cvc5::Term>(v2_high), static_cast<cvc5::Term>(max_68bit) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::GEQ,
                                          { static_cast<cvc5::Term>(v2_high_shift), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::LEQ, { static_cast<cvc5::Term>(v2_high_shift), static_cast<cvc5::Term>(max_50bit) }));

    // Select which variable to test (x_lo or x_hi)
    smt_terms::STerm v1_var = (var_name == "x_lo") ? v1_x_lo : v1_x_hi;
    smt_terms::STerm v2_var = (var_name == "x_lo") ? v2_x_lo : v2_x_hi;

    // Constrain x_lo or x_hi to be equal
    s.assertFormula(
        s.term_manager.mkTerm(cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(v1_var), static_cast<cvc5::Term>(v2_var) }));

    // Assert that at least one relevant limb differs
    // For x_lo: check p_x_low_limbs and p_x_low_limbs_shift
    // For x_hi: check p_x_high_limbs and p_x_high_limbs_shift
    std::vector<cvc5::Term> differs;
    if (var_name == "x_lo") {
        differs.push_back(s.term_manager.mkTerm(
            cvc5::Kind::NOT,
            { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                    { static_cast<cvc5::Term>(v1_low - v2_low), static_cast<cvc5::Term>(zero) }) }));
        differs.push_back(
            s.term_manager.mkTerm(cvc5::Kind::NOT,
                                  { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                                          { static_cast<cvc5::Term>(v1_low_shift - v2_low_shift),
                                                            static_cast<cvc5::Term>(zero) }) }));
    } else {
        differs.push_back(s.term_manager.mkTerm(
            cvc5::Kind::NOT,
            { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                    { static_cast<cvc5::Term>(v1_high - v2_high), static_cast<cvc5::Term>(zero) }) }));
        differs.push_back(
            s.term_manager.mkTerm(cvc5::Kind::NOT,
                                  { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                                          { static_cast<cvc5::Term>(v1_high_shift - v2_high_shift),
                                                            static_cast<cvc5::Term>(zero) }) }));
    }

    cvc5::Term at_least_one_differs = differs[0];
    for (size_t i = 1; i < differs.size(); ++i) {
        at_least_one_differs = s.term_manager.mkTerm(cvc5::Kind::OR, { at_least_one_differs, differs[i] });
    }
    s.assertFormula(at_least_one_differs);

    out_unique = s.check() ? "NOT_UNIQUE" : "UNIQUE";
    s.pop();

    // Test maximum value
    s.push();

    std::vector<smt_terms::STerm> fm, vm;
    std::vector<std::string> nm;

    smt_translator_relations::instantiate_translator_decomposition_with_iterm_return_formulas(&s, "M", fm, vm, nm);
    smt_translator_relations::assert_formulas_zero(&s, { fm[42], fm[43] });

    // Find the variables
    smt_terms::STerm m_low, m_low_shift, m_high, m_high_shift, m_x_lo, m_x_hi;
    for (size_t i = 0; i < nm.size(); ++i) {
        if (nm[i] == "M_p_x_low_limbs")
            m_low = vm[i];
        if (nm[i] == "M_p_x_low_limbs_shift")
            m_low_shift = vm[i];
        if (nm[i] == "M_p_x_high_limbs")
            m_high = vm[i];
        if (nm[i] == "M_p_x_high_limbs_shift")
            m_high_shift = vm[i];
        if (nm[i] == "M_x_lo_y_hi")
            m_x_lo = vm[i];
        if (nm[i] == "M_x_hi_z_1")
            m_x_hi = vm[i];
    }

    // Constrain limbs to their maximum values
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                          { static_cast<cvc5::Term>(m_low), static_cast<cvc5::Term>(max_68bit) }));
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(m_low_shift), static_cast<cvc5::Term>(max_68bit) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                          { static_cast<cvc5::Term>(m_high), static_cast<cvc5::Term>(max_68bit) }));
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(m_high_shift), static_cast<cvc5::Term>(max_50bit) }));

    smt_terms::STerm m_var = (var_name == "x_lo") ? m_x_lo : m_x_hi;

    if (s.check()) {
        uint256_t max_val = uint256_from_decimal_string(s.get(m_var));
        std::ostringstream oss;
        oss << max_val;
        out_max = oss.str();
    } else {
        out_max = "UNSAT";
    }

    s.pop();
}

TEST(TranslatorRelationVerification, test_relation_formulas_extraction)
{
    smt_solver::Solver s("30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001",
                         smt_solver::default_solver_config);
    std::vector<smt_terms::STerm> formulas =
        smt_translator_relations::extract_translator_decomposition_relation_formulas(&s);
    ASSERT_EQ(formulas.size(), 48); // 48 subrelations in translator decomposition
}

TEST(TranslatorRelationVerification, test_translator_decompositions)
{
    smt_solver::Solver s("30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001",
                         smt_solver::default_solver_config);

    auto decomp_map = get_translator_decomposition_map();

    std::cerr << "\n" << std::string(80, '=') << "\n";
    std::cerr << "Testing Translator VM Decompositions\n";
    std::cerr << std::string(80, '=') << "\n\n";

    // Test individual limb decompositions
    {
        std::vector<smt_terms::STerm> formulas, vars;
        std::vector<std::string> names;

        smt_translator_relations::instantiate_translator_decomposition_with_iterm_return_formulas(
            &s, "", formulas, vars, names);

        // Apply range constraints
        smt_translator_relations::create_range_constraint_formulas(&s, vars, names, "constraint", 16384);

        std::vector<LimbDecomposition> limbs_to_test = { decomp_map[4], decomp_map[5], decomp_map[6], decomp_map[7] };

        for (const auto& decomp : limbs_to_test) {
            std::string unique_result, max_value;
            test_limb_uniqueness_and_maximum(s, decomp, unique_result, max_value);

            std::cerr << std::left << std::setw(30) << decomp.limb_name << " | " << std::setw(12) << unique_result
                      << " | max: " << max_value << "\n";

            ASSERT_EQ(unique_result, "UNIQUE");
        }
    }

    // Test x_lo and x_hi decompositions
    for (const auto& var_name : { "x_lo", "x_hi" }) {
        std::string unique_result, max_value;
        test_x_lo_x_hi_uniqueness_and_maximum(s, var_name, unique_result, max_value);

        std::cerr << std::left << std::setw(30) << var_name << " | " << std::setw(12) << unique_result
                  << " | max: " << max_value << "\n";

        ASSERT_EQ(unique_result, "UNIQUE");
    }

    std::cerr << "\n" << std::string(80, '=') << "\n";
    std::cerr << "All decomposition tests passed ✓\n";
    std::cerr << std::string(80, '=') << "\n\n";
}
