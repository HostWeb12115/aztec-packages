#pragma once
#include "barretenberg/ecc/curves/bn254/fr.hpp"
#include "barretenberg/numeric/uint256/uint256.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// Forward declarations to avoid circular dependencies
namespace smt_solver {
class Solver;
}

namespace smt_terms {
class STerm;
enum class TermType;
} // namespace smt_terms

namespace smt_relation_recorder {

/**
 * @brief Enum representing the type of field operation
 */
enum class OpKind {
    VAR,        // Variable/input
    CONST_U64,  // Constant from uint64_t
    CONST_I64,  // Constant from int64_t
    CONST_INT,  // Constant from int
    CONST_U256, // Constant from uint256_t
    CONST_FR,   // Constant from bb::fr
    ADD,        // Addition
    SUB,        // Subtraction
    MUL,        // Multiplication
    NEG         // Negation
};

/**
 * @brief Represents a single operation in the computation graph
 * Each operation has a unique ID and refers to inputs by their IDs
 */
struct Operation {
    OpKind kind;
    size_t result_id; // Unique ID for this operation's result

    // For binary operations (ADD, SUB, MUL): operand IDs
    size_t lhs_id = 0;
    size_t rhs_id = 0;

    // For unary operations (NEG): operand ID stored in lhs_id

    // For constants and variables: store the value/name
    std::variant<std::monostate, uint64_t, int64_t, int, uint256_t, bb::fr, std::string> value;

    Operation(OpKind k, size_t id)
        : kind(k)
        , result_id(id)
    {}
};

/**
 * @brief Records operations performed during relation execution
 * This acts as a "VM trace" of the computation
 */
class OperationTrace {
  public:
    std::vector<Operation> operations;
    size_t next_id = 0;

    // Stores accumulator results in index order
    std::vector<size_t> accumulator_results;

    /**
     * @brief Record a variable
     */
    size_t record_var(const std::string& name)
    {
        Operation op(OpKind::VAR, next_id++);
        op.value = name;
        operations.push_back(op);
        return op.result_id;
    }

    /**
     * @brief Record a constant
     */
    size_t record_const_u64(uint64_t val)
    {
        Operation op(OpKind::CONST_U64, next_id++);
        op.value = val;
        operations.push_back(op);
        return op.result_id;
    }

    size_t record_const_i64(int64_t val)
    {
        Operation op(OpKind::CONST_I64, next_id++);
        op.value = val;
        operations.push_back(op);
        return op.result_id;
    }

    size_t record_const_int(int val)
    {
        Operation op(OpKind::CONST_INT, next_id++);
        op.value = val;
        operations.push_back(op);
        return op.result_id;
    }

    size_t record_const_u256(const uint256_t& val)
    {
        Operation op(OpKind::CONST_U256, next_id++);
        op.value = val;
        operations.push_back(op);
        return op.result_id;
    }

    size_t record_const_fr(const bb::fr& val)
    {
        Operation op(OpKind::CONST_FR, next_id++);
        op.value = val;
        operations.push_back(op);
        return op.result_id;
    }

    /**
     * @brief Record a binary operation
     */
    size_t record_binary_op(OpKind kind, size_t lhs, size_t rhs)
    {
        Operation op(kind, next_id++);
        op.lhs_id = lhs;
        op.rhs_id = rhs;
        operations.push_back(op);
        return op.result_id;
    }

    /**
     * @brief Record a unary operation
     */
    size_t record_unary_op(OpKind kind, size_t operand)
    {
        Operation op(kind, next_id++);
        op.lhs_id = operand;
        operations.push_back(op);
        return op.result_id;
    }

    /**
     * @brief Record the final value of an accumulator
     */
    void set_accumulator_result(size_t accumulator_idx, size_t operation_id)
    {
        if (accumulator_idx >= accumulator_results.size()) {
            accumulator_results.resize(accumulator_idx + 1, std::numeric_limits<size_t>::max());
        }
        accumulator_results[accumulator_idx] = operation_id;
    }
};

/**
 * @brief A field element type that records operations instead of executing them
 * This is used in place of SymFF during relation execution to record the computation
 */
class RecordingFF {
  public:
    std::shared_ptr<OperationTrace> trace;
    size_t operation_id; // ID of the operation that produced this value

    // Thread-local trace used for default construction
    static inline thread_local std::shared_ptr<OperationTrace> default_trace;

    RecordingFF()
        : trace(default_trace ? default_trace : std::make_shared<OperationTrace>())
        , operation_id(trace->record_const_u64(0))
    {}

    // Single-argument constructors for integers (to avoid ambiguity)
    explicit RecordingFF(int val)
        : trace(default_trace ? default_trace : std::make_shared<OperationTrace>())
        , operation_id(trace->record_const_int(val))
    {}

    explicit RecordingFF(uint64_t val)
        : trace(default_trace ? default_trace : std::make_shared<OperationTrace>())
        , operation_id(trace->record_const_u64(val))
    {}

    // Single-argument constructor from uint256_t (for relation constants)
    explicit RecordingFF(const uint256_t& val)
        : trace(default_trace ? default_trace : std::make_shared<OperationTrace>())
        , operation_id(trace->record_const_u256(val))
    {}

    explicit RecordingFF(std::shared_ptr<OperationTrace> t)
        : trace(t)
        , operation_id(trace->record_const_u64(0))
    {}

    explicit RecordingFF(std::shared_ptr<OperationTrace> t, uint64_t val)
        : trace(t)
        , operation_id(trace->record_const_u64(val))
    {}

    explicit RecordingFF(std::shared_ptr<OperationTrace> t, int64_t val)
        : trace(t)
        , operation_id(trace->record_const_i64(val))
    {}

    explicit RecordingFF(std::shared_ptr<OperationTrace> t, int val)
        : trace(t)
        , operation_id(trace->record_const_int(val))
    {}

    explicit RecordingFF(std::shared_ptr<OperationTrace> t, const uint256_t& val)
        : trace(t)
        , operation_id(trace->record_const_u256(val))
    {}

    explicit RecordingFF(std::shared_ptr<OperationTrace> t, const bb::fr& val)
        : trace(t)
        , operation_id(trace->record_const_fr(val))
    {}

    explicit RecordingFF(std::shared_ptr<OperationTrace> t, const std::string& var_name)
        : trace(t)
        , operation_id(trace->record_var(var_name))
    {}

  private:
    // Private tag type to distinguish operation ID constructor
    struct OperationIdTag {};

    // Private constructor for operation results (used by operator overloads)
    RecordingFF(std::shared_ptr<OperationTrace> t, size_t op_id, OperationIdTag)
        : trace(t)
        , operation_id(op_id)
    {}

  public:
    // Arithmetic operations
    RecordingFF operator+(const RecordingFF& other) const
    {
        size_t result_id_local = trace->record_binary_op(OpKind::ADD, operation_id, other.operation_id);
        return RecordingFF(trace, result_id_local, OperationIdTag{});
    }

    RecordingFF operator-(const RecordingFF& other) const
    {
        size_t result_id_local = trace->record_binary_op(OpKind::SUB, operation_id, other.operation_id);
        return RecordingFF(trace, result_id_local, OperationIdTag{});
    }

    RecordingFF operator*(const RecordingFF& other) const
    {
        size_t result_id_local = trace->record_binary_op(OpKind::MUL, operation_id, other.operation_id);
        return RecordingFF(trace, result_id_local, OperationIdTag{});
    }

    RecordingFF& operator*=(const RecordingFF& other)
    {
        operation_id = trace->record_binary_op(OpKind::MUL, operation_id, other.operation_id);
        return *this;
    }

    RecordingFF operator-() const
    {
        size_t result_id_local = trace->record_unary_op(OpKind::NEG, operation_id);
        return RecordingFF(trace, result_id_local, OperationIdTag{});
    }

    // Friend operations for scalar * RecordingFF
    friend RecordingFF operator*(const uint256_t& c, const RecordingFF& x)
    {
        size_t const_id = x.trace->record_const_u256(c);
        size_t result_id_local = x.trace->record_binary_op(OpKind::MUL, const_id, x.operation_id);
        return RecordingFF(x.trace, result_id_local, OperationIdTag{});
    }

    friend RecordingFF operator*(const bb::fr& c, const RecordingFF& x)
    {
        size_t const_id = x.trace->record_const_fr(c);
        size_t result_id_local = x.trace->record_binary_op(OpKind::MUL, const_id, x.operation_id);
        return RecordingFF(x.trace, result_id_local, OperationIdTag{});
    }

    friend RecordingFF operator+(const bb::fr& c, const RecordingFF& x)
    {
        size_t const_id = x.trace->record_const_fr(c);
        size_t result_id_local = x.trace->record_binary_op(OpKind::ADD, const_id, x.operation_id);
        return RecordingFF(x.trace, result_id_local, OperationIdTag{});
    }

    friend RecordingFF operator-(const bb::fr& c, const RecordingFF& x)
    {
        size_t const_id = x.trace->record_const_fr(c);
        size_t result_id_local = x.trace->record_binary_op(OpKind::SUB, const_id, x.operation_id);
        return RecordingFF(x.trace, result_id_local, OperationIdTag{});
    }
};

/**
 * @brief Accumulator that records additions
 */
template <size_t LEN> struct RecordingAccumulator {
    using ValueType = RecordingFF;
    using View = RecordingFF;
    RecordingFF val;

    RecordingAccumulator& operator+=(const RecordingFF& x)
    {
        val = val + x;
        return *this;
    }
};

/**
 * @brief Replay recorded operations on a specific solver to produce SMT terms
 */
class OperationReplayer {
  public:
    /**
     * @brief Replay operations to produce actual SMT terms for a given solver
     * @param trace The recorded operation trace
     * @param solver The SMT solver to create terms with
     * @param initial_variables The initial variables to use for the replay
     * @param is_ffi Whether to use FFI terms (true) or FF terms (false)
     * @return Vector of SMT terms
     */
    static std::vector<smt_terms::STerm> replay(const OperationTrace& trace,
                                                smt_solver::Solver* solver,
                                                std::unordered_map<std::string, smt_terms::STerm>& initial_variables,
                                                bool is_ffi = false);
};
} // namespace smt_relation_recorder
