#include "barretenberg/smt_verification/relations/relation_operation_recorder.hpp"
#include "barretenberg/common/serialize.hpp"
#include "barretenberg/ecc/curves/bn254/fr.hpp"
#include "barretenberg/numeric/uint256/uint256.hpp"
#include "barretenberg/smt_verification/relations/translator_vm/translator_relations_recorder.hpp"
#include "barretenberg/smt_verification/solver/solver.hpp"
#include "barretenberg/smt_verification/terms/term.hpp"

namespace smt_relation_recorder {

std::vector<smt_terms::STerm> OperationReplayer::replay(
    const OperationTrace& trace,
    smt_solver::Solver* solver,
    std::unordered_map<std::string, smt_terms::STerm>& initial_variables,
    bool is_ffi)
{
    using namespace smt_terms;

    std::unordered_map<size_t, STerm> results;

    // Process each operation in order
    for (const auto& op : trace.operations) {
        STerm result;

        switch (op.kind) {
        case OpKind::VAR: {
            const auto& var_name = std::get<std::string>(op.value);
            if (is_ffi) {
                result = initial_variables.at(var_name);
            } else {
                result = initial_variables.at(var_name);
            }
            break;
        }

        case OpKind::CONST_FR: {
            const auto& val = std::get<bb::fr>(op.value);
            if (is_ffi) {
                result = STerm(val, solver, TermType::FFITerm);
            } else {
                result = STerm(val, solver, TermType::FFTerm);
            }
            break;
        }

        case OpKind::ADD: {
            const auto& lhs = results.at(op.lhs_id);
            const auto& rhs = results.at(op.rhs_id);
            result = lhs + rhs;
            break;
        }

        case OpKind::SUB: {
            const auto& lhs = results.at(op.lhs_id);
            const auto& rhs = results.at(op.rhs_id);
            result = lhs - rhs;
            break;
        }

        case OpKind::MUL: {
            const auto& lhs = results.at(op.lhs_id);
            const auto& rhs = results.at(op.rhs_id);
            result = lhs * rhs;
            break;
        }

        case OpKind::NEG: {
            const auto& operand = results.at(op.lhs_id);
            result = -operand;
            break;
        }

        default:
            throw std::runtime_error("Unknown operation kind in replay");
        }

        results[op.result_id] = result;
    }

    std::vector<smt_terms::STerm> accumulator_results;
    for (const auto& id : trace.accumulator_results) {
        accumulator_results.push_back(results.at(id));
    }
    return accumulator_results;
}

} // namespace smt_relation_recorder
