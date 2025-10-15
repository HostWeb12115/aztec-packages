#include <gtest/gtest.h>

#include "translator_relation_test_helpers.hpp"
#include "translator_relations.hpp"
#include "translator_relations_recorder.hpp"

using namespace bb;
using namespace translator_relation_test_helpers;

// Verify that the opcode constraint relation correctly enforces op ∈ {0, 3, 4, 8}
TEST(TranslatorOpcodeConstraintRelation, opcode_must_be_in_valid_set)
{
    smt_solver::Solver s(BN254_MODULUS, smt_solver::default_solver_config);

    std::vector<smt_terms::STerm> formulas, vars;
    std::vector<std::string> names;

    auto recording_trace = smt_translator_relations::record_translator_opcode_constraint_relation();
    smt_translator_relations::replay_translator_opcode_constraint_relation(
        recording_trace, &s, "", formulas, vars, names);

    // Find selector and opcode variables
    smt_terms::STerm lagr_mini, op_var;
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == "lagrange_mini_masking") {
            lagr_mini = vars[i];
        }
        if (names[i] == "op") {
            op_var = vars[i];
        }
    }

    smt_terms::STerm zero = smt_terms::FFConst("0", &s, 10);
    s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                          { static_cast<cvc5::Term>(lagr_mini), static_cast<cvc5::Term>(zero) }));

    for (const auto& formula : formulas) {
        s.assertFormula(s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                              { static_cast<cvc5::Term>(formula), static_cast<cvc5::Term>(zero) }));
    }

    // Should be satisfiable when op can be in {0, 3, 4, 8}
    ASSERT_TRUE(s.check());

    // Should be unsatisfiable when op is forced outside {0, 3, 4, 8}
    for (const char* val_str : { "0", "3", "4", "8" }) {
        smt_terms::STerm val = smt_terms::FFConst(val_str, &s, 10);
        s.assertFormula(s.term_manager.mkTerm(
            cvc5::Kind::NOT,
            { s.term_manager.mkTerm(cvc5::Kind::EQUAL,
                                    { static_cast<cvc5::Term>(op_var), static_cast<cvc5::Term>(val) }) }));
    }

    ASSERT_FALSE(s.check());
}
