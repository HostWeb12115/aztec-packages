#include <gtest/gtest.h>

#include "barretenberg/smt_verification/relations/translator_vm/translator_relations.hpp"
#include "barretenberg/smt_verification/relations/translator_vm/translator_relations_recorder.hpp"
#include "barretenberg/smt_verification/solver/solver.hpp"
#include "barretenberg/smt_verification/terms/term.hpp"

using namespace bb;

TEST(TranslatorOpcodeConstraintRelation, test_opcode_constraint_relation)
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
