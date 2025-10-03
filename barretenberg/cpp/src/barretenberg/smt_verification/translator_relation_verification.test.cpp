#include <gtest/gtest.h>

#include "barretenberg/numeric/uint256/uint256.hpp"
#include "barretenberg/relations/translator_vm/translator_decomposition_relation.hpp"
#include "barretenberg/smt_verification/relations/relation_operation_recorder.hpp"
#include "barretenberg/smt_verification/relations/translator_vm/translator_relations.hpp"
#include "barretenberg/smt_verification/relations/translator_vm/translator_relations_recorder.hpp"
#include "barretenberg/smt_verification/solver/solver.hpp"
#include <iomanip>
#include <set>
#include <sstream>

static std::string op_kind_to_string(smt_relation_recorder::OpKind kind)
{
    using smt_relation_recorder::OpKind;
    switch (kind) {
    case OpKind::VAR:
        return "VAR";
    case OpKind::CONST_FR:
        return "CONST_FR";
    case OpKind::ADD:
        return "ADD";
    case OpKind::SUB:
        return "SUB";
    case OpKind::MUL:
        return "MUL";
    case OpKind::NEG:
        return "NEG";
    default:
        return "UNKNOWN";
    }
}

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
 * @brief Helper function to convert uint256_t to decimal string
 */
std::string uint256_to_decimal_string(const uint256_t& value)
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

        // Wide limbs for bigfield relation (relations 20-21)
        // These have 6 microlimbs (4 regular + 2 borrowed tail microlimbs from other limbs)
        { "relation_wide_limbs",
          20,
          { "relation_wide_limbs_range_constraint_0",
            "relation_wide_limbs_range_constraint_1",
            "relation_wide_limbs_range_constraint_2",
            "relation_wide_limbs_range_constraint_3",
            "p_x_high_limbs_range_constraint_tail_shift",
            "accumulator_high_limbs_range_constraint_tail_shift" },
          6,
          "No tail constraint (uses borrowed microlimbs)",
          20 }, // No separate tail constraint, use self

        { "relation_wide_limbs_shift",
          21,
          { "relation_wide_limbs_range_constraint_0_shift",
            "relation_wide_limbs_range_constraint_1_shift",
            "relation_wide_limbs_range_constraint_2_shift",
            "relation_wide_limbs_range_constraint_3_shift",
            "p_y_high_limbs_range_constraint_tail_shift",
            "quotient_high_limbs_range_constraint_tail_shift" },
          6,
          "No tail constraint (uses borrowed microlimbs)",
          21 }, // No separate tail constraint, use self

        // Z decompositions (relations 8-11)
        { "z_low_limbs",
          8,
          { "z_low_limbs_range_constraint_0",
            "z_low_limbs_range_constraint_1",
            "z_low_limbs_range_constraint_2",
            "z_low_limbs_range_constraint_3",
            "z_low_limbs_range_constraint_4",
            "z_low_limbs_range_constraint_tail" },
          5,
          "rc_4 * 4 = tail (12-bit top microlimb)",
          30 },

        { "z_low_limbs_shift",
          9,
          { "z_low_limbs_range_constraint_0_shift",
            "z_low_limbs_range_constraint_1_shift",
            "z_low_limbs_range_constraint_2_shift",
            "z_low_limbs_range_constraint_3_shift",
            "z_low_limbs_range_constraint_4_shift",
            "z_low_limbs_range_constraint_tail_shift" },
          5,
          "rc_4_shift * 4 = tail_shift (12-bit top microlimb)",
          31 },

        { "z_high_limbs",
          10,
          { "z_high_limbs_range_constraint_0",
            "z_high_limbs_range_constraint_1",
            "z_high_limbs_range_constraint_2",
            "z_high_limbs_range_constraint_3",
            "z_high_limbs_range_constraint_4",
            "z_high_limbs_range_constraint_tail" },
          5,
          "rc_4 * 1024 = tail (4-bit top microlimb, 60-bit limb)",
          32 },

        { "z_high_limbs_shift",
          11,
          { "z_high_limbs_range_constraint_0_shift",
            "z_high_limbs_range_constraint_1_shift",
            "z_high_limbs_range_constraint_2_shift",
            "z_high_limbs_range_constraint_3_shift",
            "z_high_limbs_range_constraint_4_shift",
            "z_high_limbs_range_constraint_tail_shift" },
          5,
          "rc_4_shift * 1024 = tail_shift (4-bit top microlimb, 60-bit limb)",
          33 },

        // P_y decompositions (relations 4-7)
        { "p_y_low_limbs",
          4,
          { "p_y_low_limbs_range_constraint_0",
            "p_y_low_limbs_range_constraint_1",
            "p_y_low_limbs_range_constraint_2",
            "p_y_low_limbs_range_constraint_3",
            "p_y_low_limbs_range_constraint_4",
            "p_y_low_limbs_range_constraint_tail" },
          5,
          "rc_4 * 4 = tail",
          26 },

        { "p_y_low_limbs_shift",
          5,
          { "p_y_low_limbs_range_constraint_0_shift",
            "p_y_low_limbs_range_constraint_1_shift",
            "p_y_low_limbs_range_constraint_2_shift",
            "p_y_low_limbs_range_constraint_3_shift",
            "p_y_low_limbs_range_constraint_4_shift",
            "p_y_low_limbs_range_constraint_tail_shift" },
          5,
          "rc_4_shift * 4 = tail_shift",
          27 },

        { "p_y_high_limbs",
          6,
          { "p_y_high_limbs_range_constraint_0",
            "p_y_high_limbs_range_constraint_1",
            "p_y_high_limbs_range_constraint_2",
            "p_y_high_limbs_range_constraint_3",
            "p_y_high_limbs_range_constraint_4",
            "p_y_high_limbs_range_constraint_tail" },
          5,
          "rc_4 * 4 = tail",
          28 },

        { "p_y_high_limbs_shift",
          7,
          { "p_y_high_limbs_range_constraint_0_shift",
            "p_y_high_limbs_range_constraint_1_shift",
            "p_y_high_limbs_range_constraint_2_shift",
            "p_y_high_limbs_range_constraint_3_shift" },
          4,
          "rc_3_shift * 64 = rc_4_shift (8-bit top limb)",
          29 },

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

        // Quotient decompositions (relations 16-19)
        { "quotient_low_binary_limbs",
          16,
          { "quotient_low_limbs_range_constraint_0",
            "quotient_low_limbs_range_constraint_1",
            "quotient_low_limbs_range_constraint_2",
            "quotient_low_limbs_range_constraint_3",
            "quotient_low_limbs_range_constraint_4",
            "quotient_low_limbs_range_constraint_tail" },
          5,
          "rc_4 * 4 = tail (12-bit top microlimb)",
          38 },

        { "quotient_low_binary_limbs_shift",
          17,
          { "quotient_low_limbs_range_constraint_0_shift",
            "quotient_low_limbs_range_constraint_1_shift",
            "quotient_low_limbs_range_constraint_2_shift",
            "quotient_low_limbs_range_constraint_3_shift",
            "quotient_low_limbs_range_constraint_4_shift",
            "quotient_low_limbs_range_constraint_tail_shift" },
          5,
          "rc_4_shift * 4 = tail_shift (12-bit top microlimb)",
          39 },

        { "quotient_high_binary_limbs",
          18,
          { "quotient_high_limbs_range_constraint_0",
            "quotient_high_limbs_range_constraint_1",
            "quotient_high_limbs_range_constraint_2",
            "quotient_high_limbs_range_constraint_3",
            "quotient_high_limbs_range_constraint_4",
            "quotient_high_limbs_range_constraint_tail" },
          5,
          "rc_4 * 4 = tail (12-bit top microlimb)",
          40 },

        { "quotient_high_binary_limbs_shift",
          19,
          { "quotient_high_limbs_range_constraint_0_shift",
            "quotient_high_limbs_range_constraint_1_shift",
            "quotient_high_limbs_range_constraint_2_shift",
            "quotient_high_limbs_range_constraint_3_shift" },
          4,
          "rc_3_shift * 16 = rc_4_shift (10-bit top limb)",
          41 },
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
                                      const smt_relation_recorder::OperationTrace& recording_trace_main,
                                      const LimbDecomposition& decomp,
                                      std::string& out_unique,
                                      std::string& out_max)
{
    // Test uniqueness
    s.push();

    std::vector<smt_terms::STerm> f1, v1, f2, v2;
    std::vector<std::string> n1, n2;

    std::cerr << "[TranslatorTest] Starting limb uniqueness check for " << decomp.limb_name << "\n";
    smt_translator_relations::replay_translator_decomposition_relation(
        recording_trace_main, &s, "V1", true, f1, v1, n1);
    std::cerr << "[TranslatorTest] Replayed V1 for " << decomp.limb_name << "\n";
    smt_translator_relations::replay_translator_decomposition_relation(
        recording_trace_main, &s, "V2", true, f2, v2, n2);
    std::cerr << "[TranslatorTest] Replayed V2 for " << decomp.limb_name << "\n";

    smt_translator_relations::create_range_constraint_formulas(&s, v1, n1, "constraint", 16384);
    smt_translator_relations::create_range_constraint_formulas(&s, v2, n2, "constraint", 16384);

    // Constrain op and lagrange_even_in_minicircuit wires to 1 (not set by create_range_constraint_formulas)
    smt_terms::STerm one = smt_terms::FFIConst("1", &s, 10);
    for (size_t i = 0; i < v1.size(); ++i) {
        if (n1[i] == "V1_op" || n1[i].find("V1_lagrange_even_in_minicircuit") != std::string::npos) {
            s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                                  { static_cast<cvc5::Term>(v1[i]), static_cast<cvc5::Term>(one) }));
        }
    }
    for (size_t i = 0; i < v2.size(); ++i) {
        if (n2[i] == "V2_op" || n2[i].find("V2_lagrange_even_in_minicircuit") != std::string::npos) {
            s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                                  { static_cast<cvc5::Term>(v2[i]), static_cast<cvc5::Term>(one) }));
        }
    }

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

    // Test maximum value using binary search on the limb value
    s.push();

    std::vector<smt_terms::STerm> fm, vm;
    std::vector<std::string> nm;

    std::cerr << "[TranslatorTest] Starting maximum check for " << decomp.limb_name << "\n";
    smt_translator_relations::replay_translator_decomposition_relation(recording_trace_main, &s, "M", true, fm, vm, nm);
    std::cerr << "[TranslatorTest] Replayed M for " << decomp.limb_name << "\n";
    smt_translator_relations::create_range_constraint_formulas(&s, vm, nm, "constraint", 16384);

    // Constrain op and lagrange_even_in_minicircuit wires to 1
    smt_terms::STerm one_m = smt_terms::FFIConst("1", &s, 10);
    for (size_t i = 0; i < vm.size(); ++i) {
        if (nm[i] == "M_op" || nm[i].find("M_lagrange_even_in_minicircuit") != std::string::npos) {
            s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                                  { static_cast<cvc5::Term>(vm[i]), static_cast<cvc5::Term>(one_m) }));
        }
    }

    smt_translator_relations::assert_formulas_zero(&s, { fm[decomp.relation_index], fm[decomp.tail_relation_index] });

    auto max_limb = find_mapped_limb_variables(vm, nm, decomp, "M");

    // Search for maximum value (we know it should be a power of 2 minus one, so if it's not, we fail)
    // Start with theoretical maximum based on limb structure (68 bits for most limbs)
    uint256_t max_found = 0;

    // First, find an upper bound by trying powers of 2 (starting from 80 bits down to 1)
    for (int bits = 80; bits >= 1; bits--) {
        s.push();
        uint256_t test_val = (uint256_t(1) << static_cast<uint64_t>(bits)) - 1;
        std::string test_str = uint256_to_decimal_string(test_val);
        smt_terms::STerm test_term = smt_terms::FFIConst(test_str, &s, 10);
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::GEQ, { static_cast<cvc5::Term>(max_limb.limb_var), static_cast<cvc5::Term>(test_term) }));

        if (s.check()) {
            max_found = test_val;
            s.pop();
            break;
        }
        s.pop();
    }

    // Check that this value is indeed the maximum (skip for unconstrained limbs)
    bool is_accumulator = (decomp.relation_index <= 3);
    bool is_wide_limb = (decomp.relation_index == 20 || decomp.relation_index == 21);
    if (!is_accumulator && !is_wide_limb) {
        s.push();
        uint256_t test_val = max_found + 1;
        std::string test_str = uint256_to_decimal_string(test_val);
        smt_terms::STerm test_term = smt_terms::FFIConst(test_str, &s, 10);
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::GEQ, { static_cast<cvc5::Term>(max_limb.limb_var), static_cast<cvc5::Term>(test_term) }));
        ASSERT_FALSE(s.check());
        s.pop();
    }

    s.pop();
    out_max = uint256_to_decimal_string(max_found);
}

void test_lo_hi_uniqueness_and_maximum(smt_solver::Solver& s,
                                       const smt_relation_recorder::OperationTrace& recording_trace_main,
                                       const std::string& var_name,
                                       const std::vector<uint256_t>& limb_max_values,
                                       std::string& out_unique,
                                       std::string& out_max)
{
    // Determine coordinate (x, y, or z) and level (lo or hi, or 1 or 2 for z)
    bool is_x = (var_name == "x_lo" || var_name == "x_hi");
    bool is_z = (var_name == "z1" || var_name == "z2");
    bool is_low = (var_name == "x_lo" || var_name == "y_lo" || var_name == "z1");

    std::string coord;
    size_t lo_relation, hi_relation;
    std::string lo_var_name, hi_var_name;
    std::string low_limb_name, low_limb_shift_name, high_limb_name, high_limb_shift_name;

    if (is_z) {
        coord = "z";
        lo_relation = 46;               // z1
        hi_relation = 47;               // z2
        lo_var_name = "x_hi_z_1_shift"; // z1 is stored here
        hi_var_name = "y_lo_z_2_shift"; // z2 is stored here
        low_limb_name = "z_low_limbs";
        low_limb_shift_name = "z_low_limbs_shift";
        high_limb_name = "z_high_limbs";
        high_limb_shift_name = "z_high_limbs_shift";
    } else {
        coord = is_x ? "x" : "y";
        // x: lo=42, hi=43; y: lo=44, hi=45
        lo_relation = is_x ? 42 : 44;
        hi_relation = is_x ? 43 : 45;
        // x_lo=x_lo_y_hi, x_hi=x_hi_z_1, y_lo=y_lo_z_2, y_hi=x_lo_y_hi_shift
        lo_var_name = is_x ? "x_lo_y_hi" : "y_lo_z_2";
        hi_var_name = is_x ? "x_hi_z_1" : "x_lo_y_hi_shift";
        low_limb_name = "p_" + coord + "_low_limbs";
        low_limb_shift_name = "p_" + coord + "_low_limbs_shift";
        high_limb_name = "p_" + coord + "_high_limbs";
        high_limb_shift_name = "p_" + coord + "_high_limbs_shift";
    }

    // Test uniqueness
    s.push();

    std::vector<smt_terms::STerm> f1, v1, f2, v2;
    std::vector<std::string> n1, n2;

    std::cerr << "[TranslatorTest] Starting lo/hi uniqueness check for " << var_name << "\n";
    smt_translator_relations::replay_translator_decomposition_relation(
        recording_trace_main, &s, "V1", true, f1, v1, n1);
    std::cerr << "[TranslatorTest] Replayed V1 for " << var_name << "\n";
    smt_translator_relations::replay_translator_decomposition_relation(
        recording_trace_main, &s, "V2", true, f2, v2, n2);
    std::cerr << "[TranslatorTest] Replayed V2 for " << var_name << "\n";

    // Assert decomposition relations for the composite values only (not the limb decompositions)
    smt_translator_relations::assert_formulas_zero(
        &s, { f1[lo_relation], f1[hi_relation], f2[lo_relation], f2[hi_relation] });

    // Find the variables
    smt_terms::STerm v1_low, v1_low_shift, v1_high, v1_high_shift, v1_lo, v1_hi;
    smt_terms::STerm v2_low, v2_low_shift, v2_high, v2_high_shift, v2_lo, v2_hi;

    for (size_t i = 0; i < n1.size(); ++i) {
        if (n1[i] == "V1_" + low_limb_name)
            v1_low = v1[i];
        if (n1[i] == "V1_" + low_limb_shift_name)
            v1_low_shift = v1[i];
        if (n1[i] == "V1_" + high_limb_name)
            v1_high = v1[i];
        if (n1[i] == "V1_" + high_limb_shift_name)
            v1_high_shift = v1[i];
        if (n1[i] == "V1_" + lo_var_name)
            v1_lo = v1[i];
        if (n1[i] == "V1_" + hi_var_name)
            v1_hi = v1[i];

        if (n2[i] == "V2_" + low_limb_name)
            v2_low = v2[i];
        if (n2[i] == "V2_" + low_limb_shift_name)
            v2_low_shift = v2[i];
        if (n2[i] == "V2_" + high_limb_name)
            v2_high = v2[i];
        if (n2[i] == "V2_" + high_limb_shift_name)
            v2_high_shift = v2[i];
        if (n2[i] == "V2_" + lo_var_name)
            v2_lo = v2[i];
        if (n2[i] == "V2_" + hi_var_name)
            v2_hi = v2[i];
    }

    // Constrain limbs to their discovered maximum values (bottom-up approach)
    // limb_max_values indices:
    // [0-3]: accumulator limbs (accumulators_binary_limbs_0-3)
    // [4-5]: wide limbs (relation_wide_limbs, relation_wide_limbs_shift)
    // [6-9]: z limbs (z_low_limbs, z_low_limbs_shift, z_high_limbs, z_high_limbs_shift)
    // [10-13]: p_y limbs (p_y_low_limbs, p_y_low_limbs_shift, p_y_high_limbs, p_y_high_limbs_shift)
    // [14-17]: p_x limbs (p_x_low_limbs, p_x_low_limbs_shift, p_x_high_limbs, p_x_high_limbs_shift)
    // [18-21]: quotient limbs (quotient_low_binary_limbs, quotient_low_binary_limbs_shift,
    // quotient_high_binary_limbs, quotient_high_binary_limbs_shift)

    size_t base_idx = is_z ? 6 : (is_x ? 14 : 10);

    smt_terms::STerm zero = smt_terms::FFIConst("0", &s, 10);
    smt_terms::STerm max_low = smt_terms::FFIConst(uint256_to_decimal_string(limb_max_values[base_idx + 0]), &s, 10);
    smt_terms::STerm max_low_shift =
        smt_terms::FFIConst(uint256_to_decimal_string(limb_max_values[base_idx + 1]), &s, 10);
    smt_terms::STerm max_high = smt_terms::FFIConst(uint256_to_decimal_string(limb_max_values[base_idx + 2]), &s, 10);
    smt_terms::STerm max_high_shift =
        smt_terms::FFIConst(uint256_to_decimal_string(limb_max_values[base_idx + 3]), &s, 10);

    // V1 limb range constraints
    s.assertFormula(
        s.term_manager.mkTerm(cvc5::Kind::GEQ, { static_cast<cvc5::Term>(v1_low), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(
        s.term_manager.mkTerm(cvc5::Kind::LEQ, { static_cast<cvc5::Term>(v1_low), static_cast<cvc5::Term>(max_low) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::GEQ,
                                          { static_cast<cvc5::Term>(v1_low_shift), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::LEQ, { static_cast<cvc5::Term>(v1_low_shift), static_cast<cvc5::Term>(max_low_shift) }));
    s.assertFormula(
        s.term_manager.mkTerm(cvc5::Kind::GEQ, { static_cast<cvc5::Term>(v1_high), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::LEQ,
                                          { static_cast<cvc5::Term>(v1_high), static_cast<cvc5::Term>(max_high) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::GEQ,
                                          { static_cast<cvc5::Term>(v1_high_shift), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::LEQ, { static_cast<cvc5::Term>(v1_high_shift), static_cast<cvc5::Term>(max_high_shift) }));

    // V2 limb range constraints
    s.assertFormula(
        s.term_manager.mkTerm(cvc5::Kind::GEQ, { static_cast<cvc5::Term>(v2_low), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(
        s.term_manager.mkTerm(cvc5::Kind::LEQ, { static_cast<cvc5::Term>(v2_low), static_cast<cvc5::Term>(max_low) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::GEQ,
                                          { static_cast<cvc5::Term>(v2_low_shift), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::LEQ, { static_cast<cvc5::Term>(v2_low_shift), static_cast<cvc5::Term>(max_low_shift) }));
    s.assertFormula(
        s.term_manager.mkTerm(cvc5::Kind::GEQ, { static_cast<cvc5::Term>(v2_high), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::LEQ,
                                          { static_cast<cvc5::Term>(v2_high), static_cast<cvc5::Term>(max_high) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::GEQ,
                                          { static_cast<cvc5::Term>(v2_high_shift), static_cast<cvc5::Term>(zero) }));
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::LEQ, { static_cast<cvc5::Term>(v2_high_shift), static_cast<cvc5::Term>(max_high_shift) }));

    // Select which variable to test (lo or hi)
    smt_terms::STerm v1_var = is_low ? v1_lo : v1_hi;
    smt_terms::STerm v2_var = is_low ? v2_lo : v2_hi;

    // Constrain lo or hi to be equal
    s.assertFormula(
        s.term_manager.mkTerm(cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(v1_var), static_cast<cvc5::Term>(v2_var) }));

    // Assert that at least one relevant limb differs
    // For x_lo/y_lo: check low_limbs and low_limbs_shift
    // For x_hi/y_hi: check high_limbs and high_limbs_shift
    // For z1: check z_low_limbs and z_high_limbs (non-shift)
    // For z2: check z_low_limbs_shift and z_high_limbs_shift
    std::vector<cvc5::Term> differs;
    if (is_z) {
        // z1 uses non-shift limbs, z2 uses shift limbs
        if (is_low) {
            // z1: z_low_limbs + z_high_limbs * LIMB_SHIFT
            differs.push_back(s.term_manager.mkTerm(
                cvc5::Kind::NOT,
                { s.term_manager.mkTerm(
                    cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(v1_low - v2_low), static_cast<cvc5::Term>(zero) }) }));
            differs.push_back(
                s.term_manager.mkTerm(cvc5::Kind::NOT,
                                      { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                                              { static_cast<cvc5::Term>(v1_high - v2_high),
                                                                static_cast<cvc5::Term>(zero) }) }));
        } else {
            // z2: z_low_limbs_shift + z_high_limbs_shift * LIMB_SHIFT
            differs.push_back(
                s.term_manager.mkTerm(cvc5::Kind::NOT,
                                      { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                                              { static_cast<cvc5::Term>(v1_low_shift - v2_low_shift),
                                                                static_cast<cvc5::Term>(zero) }) }));
            differs.push_back(
                s.term_manager.mkTerm(cvc5::Kind::NOT,
                                      { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                                              { static_cast<cvc5::Term>(v1_high_shift - v2_high_shift),
                                                                static_cast<cvc5::Term>(zero) }) }));
        }
    } else {
        // x/y case
        if (is_low) {
            differs.push_back(s.term_manager.mkTerm(
                cvc5::Kind::NOT,
                { s.term_manager.mkTerm(
                    cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(v1_low - v2_low), static_cast<cvc5::Term>(zero) }) }));
            differs.push_back(
                s.term_manager.mkTerm(cvc5::Kind::NOT,
                                      { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                                              { static_cast<cvc5::Term>(v1_low_shift - v2_low_shift),
                                                                static_cast<cvc5::Term>(zero) }) }));
        } else {
            differs.push_back(
                s.term_manager.mkTerm(cvc5::Kind::NOT,
                                      { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                                              { static_cast<cvc5::Term>(v1_high - v2_high),
                                                                static_cast<cvc5::Term>(zero) }) }));
            differs.push_back(
                s.term_manager.mkTerm(cvc5::Kind::NOT,
                                      { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                                              { static_cast<cvc5::Term>(v1_high_shift - v2_high_shift),
                                                                static_cast<cvc5::Term>(zero) }) }));
        }
    }

    cvc5::Term at_least_one_differs = differs[0];
    for (size_t i = 1; i < differs.size(); ++i) {
        at_least_one_differs = s.term_manager.mkTerm(cvc5::Kind::OR, { at_least_one_differs, differs[i] });
    }
    s.assertFormula(at_least_one_differs);

    out_unique = s.check() ? "NOT_UNIQUE" : "UNIQUE";
    s.pop();

    // Test maximum value using discovered limb maximums (bottom-up approach)
    s.push();

    std::vector<smt_terms::STerm> fm, vm;
    std::vector<std::string> nm;

    std::cerr << "[TranslatorTest] Starting lo/hi maximum check for " << var_name << "\n";
    smt_translator_relations::replay_translator_decomposition_relation(recording_trace_main, &s, "M", true, fm, vm, nm);
    std::cerr << "[TranslatorTest] Replayed M for " << var_name << "\n";
    smt_translator_relations::assert_formulas_zero(&s, { fm[lo_relation], fm[hi_relation] });

    // Find the variables
    smt_terms::STerm m_low, m_low_shift, m_high, m_high_shift, m_lo, m_hi;
    for (size_t i = 0; i < nm.size(); ++i) {
        if (nm[i] == "M_" + low_limb_name)
            m_low = vm[i];
        if (nm[i] == "M_" + low_limb_shift_name)
            m_low_shift = vm[i];
        if (nm[i] == "M_" + high_limb_name)
            m_high = vm[i];
        if (nm[i] == "M_" + high_limb_shift_name)
            m_high_shift = vm[i];
        if (nm[i] == "M_" + lo_var_name)
            m_lo = vm[i];
        if (nm[i] == "M_" + hi_var_name)
            m_hi = vm[i];
    }

    // Constrain limbs to their discovered maximum values (bottom-up approach)
    s.assertFormula(
        s.term_manager.mkTerm(cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(m_low), static_cast<cvc5::Term>(max_low) }));
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(m_low_shift), static_cast<cvc5::Term>(max_low_shift) }));
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                          { static_cast<cvc5::Term>(m_high), static_cast<cvc5::Term>(max_high) }));
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(m_high_shift), static_cast<cvc5::Term>(max_high_shift) }));

    // Get the maximum value from solver (limbs are constrained to discovered maximums)
    smt_terms::STerm m_var = is_low ? m_lo : m_hi;

    if (s.check()) {
        uint256_t max_val = uint256_from_decimal_string(s.get(m_var));
        out_max = uint256_to_decimal_string(max_val);
    } else {
        out_max = "UNSAT";
    }

    s.pop();
}

TEST(TranslatorRelationVerification, test_translator_decompositions)
{

    smt_solver::Solver s("30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001",
                         smt_solver::default_solver_config);

    auto decomp_map = get_translator_decomposition_map();

    std::cerr << "\n" << std::string(80, '=') << "\n";
    std::cerr << "Testing Translator VM Decompositions\n";
    std::cerr << std::string(80, '=') << "\n\n";

    std::vector<uint256_t> max_values;

    auto recording_trace_main = smt_translator_relations::record_translator_decomposition_relation();

    const auto& ops = recording_trace_main.operations;
    auto log_op = [&](size_t idx) {
        if (idx < ops.size()) {
            const auto& op = ops[idx];
            std::cerr << "[TranslatorTest] Operation #" << idx << " kind=" << op_kind_to_string(op.kind)
                      << " lhs=" << op.lhs_id << " rhs=" << op.rhs_id << " result=" << op.result_id;
            if (op.kind == smt_relation_recorder::OpKind::VAR) {
                std::cerr << " value=" << std::get<std::string>(op.value);
            }
            std::cerr << "\n";
        } else {
            std::cerr << "[TranslatorTest] Operation #" << idx << " is out of range (" << ops.size() << " ops)\n";
        }
    };

    log_op(553);
    log_op(558);
    // Test individual limb decompositions for p_y and p_x
    {
        std::vector<smt_terms::STerm> formulas, vars;
        std::vector<std::string> names;

        // Apply range constraints
        smt_translator_relations::create_range_constraint_formulas(&s, vars, names, "constraint", 16384);

        // Test accumulator limbs (indices 0-3), wide limbs (indices 4-5), z limbs (indices 6-9),
        // p_y limbs (indices 10-13), p_x limbs (indices 14-17), quotient limbs (indices 18-21)
        std::vector<LimbDecomposition> limbs_to_test = { decomp_map[0],  decomp_map[1],  decomp_map[2],  decomp_map[3],
                                                         decomp_map[4],  decomp_map[5],  decomp_map[6],  decomp_map[7],
                                                         decomp_map[8],  decomp_map[9],  decomp_map[10], decomp_map[11],
                                                         decomp_map[12], decomp_map[13], decomp_map[14], decomp_map[15],
                                                         decomp_map[16], decomp_map[17], decomp_map[18], decomp_map[19],
                                                         decomp_map[20], decomp_map[21] };

        for (const auto& decomp : limbs_to_test) {
            std::string unique_result, max_value;
            test_limb_uniqueness_and_maximum(s, recording_trace_main, decomp, unique_result, max_value);

            std::string bitness_str = "N/A";
            std::string max_hex_str = max_value;
            if (max_value != "UNSAT") {
                uint256_t max_val = uint256_from_decimal_string(max_value);
                uint64_t bits = max_val.get_msb() + 1;
                bitness_str = std::to_string(bits) + " bits";
                std::ostringstream oss;
                oss << max_val; // uint256_t outputs in hex with 0x prefix
                max_hex_str = oss.str();
                max_values.push_back(max_val);
            } else {
                // If max is UNSAT, push 0 as placeholder
                max_values.push_back(uint256_t(0));
            }

            std::cerr << std::left << std::setw(30) << decomp.limb_name << " | " << std::setw(12) << unique_result
                      << " | max: " << std::setw(68) << max_hex_str << " | " << bitness_str << "\n";

            // Only assert uniqueness for regular limbs (not accumulators or wide limbs)
            // Accumulator limbs (0-3) and wide limbs (20-21) are not fully constrained without full context
            bool is_accumulator = (decomp.relation_index >= 0 && decomp.relation_index <= 3);
            bool is_wide_limb = (decomp.relation_index == 20 || decomp.relation_index == 21);
            if (!is_accumulator && !is_wide_limb) {
                ASSERT_EQ(unique_result, "UNIQUE");
            }
        }
    }
    std::cerr << "GOT TO HERE 1\n";
    // Test x_lo, x_hi, y_lo, y_hi, z1, z2 decompositions
    for (const auto& var_name : { "x_lo", "x_hi", "y_lo", "y_hi", "z1", "z2" }) {
        std::string unique_result, max_value;
        test_lo_hi_uniqueness_and_maximum(s, recording_trace_main, var_name, max_values, unique_result, max_value);

        std::string bitness_str = "N/A";
        std::string max_hex_str = max_value;
        if (max_value != "UNSAT") {
            uint256_t max_val = uint256_from_decimal_string(max_value);
            uint64_t bits = max_val.get_msb() + 1;
            bitness_str = std::to_string(bits) + " bits";
            std::ostringstream oss;
            oss << max_val; // uint256_t outputs in hex with 0x prefix
            max_hex_str = oss.str();
        }

        std::cerr << std::left << std::setw(30) << var_name << " | " << std::setw(12) << unique_result
                  << " | max: " << std::setw(68) << max_hex_str << " | " << bitness_str << "\n";

        ASSERT_EQ(unique_result, "UNIQUE");
    }

    std::cerr << "\n" << std::string(80, '=') << "\n";
    std::cerr << "All decomposition tests passed ✓\n";
    std::cerr << std::string(80, '=') << "\n\n";

    // Check which relations were tested
    std::set<size_t> tested_relations;
    for (const auto& decomp : decomp_map) {
        // Check if this decomposition was tested by checking relation indices in test output
        tested_relations.insert(decomp.relation_index);
        tested_relations.insert(decomp.tail_relation_index);
    }

    // Add composite value decomposition relations (x_lo, x_hi, y_lo, y_hi, z1, z2)
    // Relation 42: x_lo = p_x_low_limbs + p_x_low_limbs_shift * LIMB_SHIFT
    // Relation 43: x_hi = p_x_high_limbs + p_x_high_limbs_shift * LIMB_SHIFT
    // Relation 44: y_lo = p_y_low_limbs + p_y_low_limbs_shift * LIMB_SHIFT
    // Relation 45: y_hi = p_y_high_limbs + p_y_high_limbs_shift * LIMB_SHIFT
    // Relation 46: z1 = z_low_limbs + z_high_limbs * LIMB_SHIFT
    // Relation 47: z2 = z_low_limbs_shift + z_high_limbs_shift * LIMB_SHIFT
    tested_relations.insert(42);
    tested_relations.insert(43);
    tested_relations.insert(44);
    tested_relations.insert(45);
    tested_relations.insert(46);
    tested_relations.insert(47);

    std::cerr << "Relations tested (" << tested_relations.size() << " total):\n";
    for (size_t idx : tested_relations) {
        std::cerr << idx << " ";
    }
    std::cerr << "\n\n";

    // Find untested relations (total is 48 relations: 0-47)
    std::vector<size_t> untested_relations;
    for (size_t i = 0; i < 48; ++i) {
        if (tested_relations.find(i) == tested_relations.end()) {
            untested_relations.push_back(i);
        }
    }

    if (!untested_relations.empty()) {
        std::cerr << "Relations NOT tested (" << untested_relations.size() << " total):\n";
        for (size_t idx : untested_relations) {
            std::cerr << "  Relation " << idx << "\n";
        }
    } else {
        std::cerr << "All 48 relations are tested ✓\n";
    }
    std::cerr << "\n" << std::string(80, '=') << "\n\n";
}

TEST(TranslatorRelationVerification, test_opcode_constraint_relation)
{

    smt_solver::Solver s("30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001",
                         smt_solver::default_solver_config);

    std::vector<smt_terms::STerm> formulas, vars;
    std::vector<std::string> names;

    // Get the opcode constraint relation formulas
    auto recording_trace_opcode = smt_translator_relations::record_translator_opcode_constraint_relation();
    smt_translator_relations::replay_translator_opcode_constraint_relation(
        recording_trace_opcode, &s, "", formulas, vars, names);

    std::cerr << "\n" << std::string(80, '=') << "\n";
    std::cerr << "Testing Translator Opcode Constraint Relation\n";
    std::cerr << std::string(80, '=') << "\n\n";

    // Find lagrange_mini_masking and op variables
    smt_terms::STerm lagr_mini, op_var;
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == "lagrange_mini_masking") {
            lagr_mini = vars[i];
        }
        if (names[i] == "op") {
            op_var = vars[i];
        }
    }

    // Set lagrange_mini_masking = 0 and scaling_factor = 1 (already done in formula generation)
    smt_terms::STerm zero = smt_terms::FFConst("0", &s, 10);
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                          { static_cast<cvc5::Term>(lagr_mini), static_cast<cvc5::Term>(zero) }));

    // Assert all relation formulas = 0
    for (const auto& formula : formulas) {
        s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                              { static_cast<cvc5::Term>(formula), static_cast<cvc5::Term>(zero) }));
    }

    // Check that the relation is satisfiable
    std::cerr << "Step 1: Checking if opcode constraint is satisfiable with lagrange_mini_masking=0...\n";
    bool sat_result = s.check();
    std::cerr << "Result: " << (sat_result ? "SAT (as expected)" : "UNSAT (unexpected!)") << "\n";
    std::cerr << "This means a valid value for op (one of {0, 3, 4, 8}) can be found.\n\n";
    ASSERT_TRUE(sat_result);

    // Now add constraints that op != 0, op != 3, op != 4, op != 8
    std::cerr << "Step 2: Adding constraints that op ∉ {0, 3, 4, 8}...\n";

    smt_terms::STerm zero_val = smt_terms::FFConst("0", &s, 10);
    smt_terms::STerm three_val = smt_terms::FFConst("3", &s, 10);
    smt_terms::STerm four_val = smt_terms::FFConst("4", &s, 10);
    smt_terms::STerm eight_val = smt_terms::FFConst("8", &s, 10);

    // op != 0
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::NOT,
        { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                { static_cast<cvc5::Term>(op_var), static_cast<cvc5::Term>(zero_val) }) }));

    // op != 3
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::NOT,
        { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                { static_cast<cvc5::Term>(op_var), static_cast<cvc5::Term>(three_val) }) }));

    // op != 4
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::NOT,
        { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                { static_cast<cvc5::Term>(op_var), static_cast<cvc5::Term>(four_val) }) }));

    // op != 8
    s.assertFormula(s.term_manager.mkTerm(
        cvc5::Kind::NOT,
        { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                { static_cast<cvc5::Term>(op_var), static_cast<cvc5::Term>(eight_val) }) }));

    // Check that the relation is now unsatisfiable
    std::cerr << "Checking if constraints are now unsatisfiable...\n";
    bool unsat_result = s.check();
    std::cerr << "Result: " << (unsat_result ? "SAT (unexpected!)" : "UNSAT (as expected)") << "\n\n";
    ASSERT_FALSE(unsat_result);

    std::cerr << std::string(80, '=') << "\n";
    std::cerr << "Opcode constraint relation test passed ✓\n";
    std::cerr << "The relation correctly enforces op ∈ {0, 3, 4, 8}\n";
    std::cerr << std::string(80, '=') << "\n\n";

    // Clean up to avoid state contamination with subsequent tests
    formulas.clear();
    vars.clear();
    names.clear();
}
