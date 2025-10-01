#include "relation_operation_recorder.hpp"
#include "barretenberg/ecc/curves/bn254/fr.hpp"
#include "barretenberg/numeric/uint256/uint256.hpp"
#include "barretenberg/smt_verification/solver/solver.hpp"
#include "barretenberg/smt_verification/terms/term.hpp"

namespace smt_relation_recorder {

std::unordered_map<size_t, smt_terms::STerm> OperationReplayer::replay(const OperationTrace& trace,
                                                                       smt_solver::Solver* solver,
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
                result = FFIVar(var_name, solver);
            } else {
                result = FFVar(var_name, solver);
            }
            break;
        }

        case OpKind::CONST_U64: {
            uint64_t val = std::get<uint64_t>(op.value);
            if (is_ffi) {
                result = FFIConst(std::to_string(val), solver, 10);
            } else {
                result = FFConst(std::to_string(val), solver, 10);
            }
            break;
        }

        case OpKind::CONST_I64: {
            int64_t val = std::get<int64_t>(op.value);
            if (is_ffi) {
                result = FFIConst(std::to_string(val), solver, 10);
            } else {
                result = FFConst(std::to_string(val), solver, 10);
            }
            break;
        }

        case OpKind::CONST_INT: {
            int val = std::get<int>(op.value);
            if (is_ffi) {
                result = FFIConst(std::to_string(val), solver, 10);
            } else {
                result = FFConst(std::to_string(val), solver, 10);
            }
            break;
        }

        case OpKind::CONST_U256: {
            const auto& val = std::get<uint256_t>(op.value);
            if (is_ffi) {
                result = STerm(bb::fr(val), solver, TermType::FFITerm);
            } else {
                result = FFConst(bb::fr(val), solver);
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

    return results;
}

} // namespace smt_relation_recorder
