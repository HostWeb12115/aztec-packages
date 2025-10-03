#include "translator_relations_recorder.hpp"
#include "barretenberg/common/zip_view.hpp"
#include "barretenberg/ecc/curves/bn254/fr.hpp"
#include "barretenberg/numeric/uint256/uint256.hpp"
#include "barretenberg/relations/translator_vm/translator_decomposition_relation_impl.hpp"
#include "barretenberg/relations/translator_vm/translator_extra_relations_impl.hpp"
#include "barretenberg/smt_verification/relations/relation_operation_recorder.hpp"
#include "barretenberg/smt_verification/solver/solver.hpp"
#include "barretenberg/smt_verification/terms/term.hpp"
#include "barretenberg/translator_vm/translator_flavor.hpp"
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace smt_translator_relations {

using namespace smt_relation_recorder;
using namespace smt_terms;

namespace {

// Build labels equal to the exact AllEntities member names, in the same order as get_all()
static std::vector<std::string> build_all_entity_member_names()
{
    std::vector<std::string> names;
    names.reserve(bb::TranslatorFlavor::NUM_ALL_ENTITIES);

    // PrecomputedEntities (use exact member names)
    names.push_back("ordered_extra_range_constraints_numerator");
    names.push_back("lagrange_first");
    names.push_back("lagrange_last");
    names.push_back("lagrange_odd_in_minicircuit");
    names.push_back("lagrange_even_in_minicircuit");
    names.push_back("lagrange_result_row");
    names.push_back("lagrange_last_in_minicircuit");
    names.push_back("lagrange_masking");
    names.push_back("lagrange_mini_masking");
    names.push_back("lagrange_real_last");

    // WireNonshiftedEntities
    names.push_back("op");

    // WireToBeShiftedEntities (base and range constraints)
    names.push_back("x_lo_y_hi");
    names.push_back("x_hi_z_1");
    names.push_back("y_lo_z_2");
    names.push_back("p_x_low_limbs");
    names.push_back("p_x_high_limbs");
    names.push_back("p_y_low_limbs");
    names.push_back("p_y_high_limbs");
    names.push_back("z_low_limbs");
    names.push_back("z_high_limbs");
    names.push_back("accumulators_binary_limbs_0");
    names.push_back("accumulators_binary_limbs_1");
    names.push_back("accumulators_binary_limbs_2");
    names.push_back("accumulators_binary_limbs_3");
    names.push_back("quotient_low_binary_limbs");
    names.push_back("quotient_high_binary_limbs");
    names.push_back("relation_wide_limbs");

    auto push_range = [&](const std::string& base, int start_idx, int end_idx, bool include_tail) {
        for (int i = start_idx; i <= end_idx; ++i) {
            names.push_back(base + std::string("_") + std::to_string(i));
        }
        if (include_tail) {
            names.push_back(base + std::string("_tail"));
        }
    };

    push_range("p_x_low_limbs_range_constraint", 0, 4, true);
    push_range("p_x_high_limbs_range_constraint", 0, 4, true);
    push_range("p_y_low_limbs_range_constraint", 0, 4, true);
    push_range("p_y_high_limbs_range_constraint", 0, 4, true);
    push_range("z_low_limbs_range_constraint", 0, 4, true);
    push_range("z_high_limbs_range_constraint", 0, 4, true);
    push_range("accumulator_low_limbs_range_constraint", 0, 4, true);
    push_range("accumulator_high_limbs_range_constraint", 0, 4, true);
    push_range("quotient_low_limbs_range_constraint", 0, 4, true);
    push_range("quotient_high_limbs_range_constraint", 0, 4, true);
    push_range("relation_wide_limbs_range_constraint", 0, 3, false);

    // OrderedRangeConstraints
    push_range("ordered_range_constraints", 0, 4, false);

    // DerivedWitnessEntities
    names.push_back("z_perm");

    // InterleavedRangeConstraints
    push_range("interleaved_range_constraints", 0, 3, false);

    // ShiftedEntities
    auto push_shift = [&](const std::string& n) { names.push_back(n + std::string("_shift")); };
    push_shift("x_lo_y_hi");
    push_shift("x_hi_z_1");
    push_shift("y_lo_z_2");
    push_shift("p_x_low_limbs");
    push_shift("p_x_high_limbs");
    push_shift("p_y_low_limbs");
    push_shift("p_y_high_limbs");
    push_shift("z_low_limbs");
    push_shift("z_high_limbs");
    push_shift("accumulators_binary_limbs_0");
    push_shift("accumulators_binary_limbs_1");
    push_shift("accumulators_binary_limbs_2");
    push_shift("accumulators_binary_limbs_3");
    push_shift("quotient_low_binary_limbs");
    push_shift("quotient_high_binary_limbs");
    push_shift("relation_wide_limbs");

    auto push_range_shift = [&](const std::string& base, int start_idx, int end_idx, bool include_tail) {
        for (int i = start_idx; i <= end_idx; ++i) {
            names.push_back(base + std::string("_") + std::to_string(i) + std::string("_shift"));
        }
        if (include_tail) {
            names.push_back(base + std::string("_tail_shift"));
        }
    };

    push_range_shift("p_x_low_limbs_range_constraint", 0, 4, true);
    push_range_shift("p_x_high_limbs_range_constraint", 0, 4, true);
    push_range_shift("p_y_low_limbs_range_constraint", 0, 4, true);
    push_range_shift("p_y_high_limbs_range_constraint", 0, 4, true);
    push_range_shift("z_low_limbs_range_constraint", 0, 4, true);
    push_range_shift("z_high_limbs_range_constraint", 0, 4, true);
    push_range_shift("accumulator_low_limbs_range_constraint", 0, 4, true);
    push_range_shift("accumulator_high_limbs_range_constraint", 0, 4, true);
    push_range_shift("quotient_low_limbs_range_constraint", 0, 4, true);
    push_range_shift("quotient_high_limbs_range_constraint", 0, 4, true);
    push_range_shift("relation_wide_limbs_range_constraint", 0, 3, false);
    push_range_shift("ordered_range_constraints", 0, 4, false);
    names.push_back("z_perm_shift");

    return names;
}

} // anonymous namespace

OperationTrace record_translator_decomposition_relation()
{
    auto trace = std::make_shared<OperationTrace>();

    // Set the default trace for this thread so single-argument constructors work
    RecordingFF::default_trace = trace;

    // Create a structure to hold RecordingFF entities
    struct RecordingAllEntities : public bb::TranslatorFlavor::AllEntities<RecordingFF> {};

    RecordingAllEntities in;
    auto refs = in.get_all();
    auto names = build_all_entity_member_names();

    // Create RecordingFF for each entity
    for (size_t i = 0; i < refs.size(); ++i) {
        if (names[i] == "lagrange_even_in_minicircuit") {
            // Set as constant 1
            refs[i] = RecordingFF(trace, static_cast<uint64_t>(1));
        } else if (names[i] == "op") {
            // Set as constant 1
            refs[i] = RecordingFF(trace, static_cast<uint64_t>(1));
        } else {
            // Create as variable
            refs[i] = RecordingFF(trace, names[i]);
        }
    }

    // Create relation parameters
    bb::RelationParameters<RecordingFF> params;
    RecordingFF scaling(trace, static_cast<uint64_t>(1));

    // Create accumulators
    using RelImpl = bb::TranslatorDecompositionRelationImpl<RecordingFF>;
    std::tuple<RecordingAccumulator<4>,
               RecordingAccumulator<4>,
               RecordingAccumulator<4>,
               RecordingAccumulator<4>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<2>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>,
               RecordingAccumulator<3>>
        accs;

    // Initialize all accumulators to zero
    std::apply([&](auto&... a) { ((a.val = RecordingFF(trace, static_cast<uint64_t>(0))), ...); }, accs);

    // Execute the relation to record operations
    RelImpl::accumulate(accs, in, params, scaling);

    // Record which operation IDs correspond to each accumulator's final value
    size_t acc_idx = 0;
    std::apply([&](auto&... a) { ((trace->set_accumulator_result(acc_idx++, a.val.operation_id.value())), ...); },
               accs);

    // Clear the default trace to avoid state leaking between tests
    RecordingFF::default_trace.reset();

    return *trace;
}

OperationTrace record_translator_opcode_constraint_relation()
{
    auto trace = std::make_shared<OperationTrace>();

    // Set the default trace for this thread so single-argument constructors work
    RecordingFF::default_trace = trace;

    // Create a structure to hold RecordingFF entities
    using Flavor = bb::TranslatorFlavor;
    using AllEntities = typename Flavor::AllEntities<RecordingFF>;

    AllEntities symbolic_all_entities;
    std::vector<std::reference_wrapper<RecordingFF>> refs;
    std::vector<std::string> names;

    // Collect all entity names and references
    for (auto [name, entity] : zip_view(symbolic_all_entities.get_labels(), symbolic_all_entities.get_all())) {
        names.push_back(name);
        refs.push_back(std::ref(entity));
    }

    // Create RecordingFF for each entity
    for (size_t i = 0; i < refs.size(); ++i) {
        bool is_lagr_mini = (names[i] == "lagrange_mini_masking");

        if (is_lagr_mini) {
            // Will be set to 0 by the test - create as variable for now
            refs[i].get() = RecordingFF(trace, names[i]);
        } else {
            refs[i].get() = RecordingFF(trace, names[i]);
        }
    }

    // Create accumulators for 5 subrelations
    std::tuple<RecordingAccumulator<6>,
               RecordingAccumulator<6>,
               RecordingAccumulator<6>,
               RecordingAccumulator<6>,
               RecordingAccumulator<6>>
        acc;

    // Initialize all accumulators to zero
    std::apply([&](auto&... a) { ((a.val = RecordingFF(trace, static_cast<uint64_t>(0))), ...); }, acc);

    // Create scaling factor = 1
    RecordingFF scaling_factor(trace, static_cast<uint64_t>(1));

    // Accumulate the opcode constraint relation
    using OpcodeRelation = bb::TranslatorOpcodeConstraintRelationImpl<RecordingFF>;
    using RelationParams = bb::RelationParameters<RecordingFF>;
    RelationParams params;
    OpcodeRelation::template accumulate<decltype(acc), AllEntities, RelationParams>(
        acc, symbolic_all_entities, params, scaling_factor);

    // Record which operation IDs correspond to each accumulator's final value
    size_t acc_idx = 0;
    std::apply([&](auto&... a) { ((trace->set_accumulator_result(acc_idx++, a.val.operation_id.value())), ...); }, acc);

    // Clear the default trace to avoid state leaking between tests
    RecordingFF::default_trace.reset();

    return *trace;
}

OperationTrace record_translator_accumulator_transfer_relation()
{
    auto trace = std::make_shared<OperationTrace>();

    RecordingFF::default_trace = trace;

    using Flavor = bb::TranslatorFlavor;
    using AllEntities = typename Flavor::AllEntities<RecordingFF>;

    AllEntities symbolic_all_entities;
    std::vector<std::reference_wrapper<RecordingFF>> refs;
    std::vector<std::string> names;

    for (auto [name, entity] : zip_view(symbolic_all_entities.get_labels(), symbolic_all_entities.get_all())) {
        names.push_back(name);
        refs.push_back(std::ref(entity));
    }

    for (size_t i = 0; i < refs.size(); ++i) {
        refs[i].get() = RecordingFF(trace, names[i]);
    }

    std::tuple<RecordingAccumulator<4>,
               RecordingAccumulator<4>,
               RecordingAccumulator<4>,
               RecordingAccumulator<4>,
               RecordingAccumulator<4>,
               RecordingAccumulator<4>,
               RecordingAccumulator<4>,
               RecordingAccumulator<4>,
               RecordingAccumulator<4>,
               RecordingAccumulator<4>,
               RecordingAccumulator<4>,
               RecordingAccumulator<4>>
        acc;

    std::apply([&](auto&... a) { ((a.val = RecordingFF(trace, static_cast<uint64_t>(0))), ...); }, acc);

    using AccRelation = bb::TranslatorAccumulatorTransferRelationImpl<RecordingFF>;
    using RelationParams = bb::RelationParameters<RecordingFF>;

    RelationParams params;
    RecordingFF scaling_factor(trace, static_cast<uint64_t>(1));

    AccRelation::template accumulate<decltype(acc), AllEntities, RelationParams>(
        acc, symbolic_all_entities, params, scaling_factor);

    size_t acc_idx = 0;
    std::apply([&](auto&... a) { ((trace->set_accumulator_result(acc_idx++, a.val.operation_id.value())), ...); }, acc);

    RecordingFF::default_trace.reset();

    return *trace;
}

void replay_translator_decomposition_relation(const OperationTrace& trace,
                                              smt_solver::Solver* solver,
                                              const std::string& prefix,
                                              bool use_ffi,
                                              std::vector<STerm>& out_formulas,
                                              std::vector<STerm>& out_vars,
                                              std::vector<std::string>& out_names)
{
    using namespace smt_terms;

    // Build the name mapping
    auto original_names = build_all_entity_member_names();
    std::unordered_map<std::string, std::string> name_map;

    for (const auto& name : original_names) {
        if (prefix.empty()) {
            name_map[name] = name;
        } else {
            name_map[name] = prefix + "_" + name;
        }
    }

    // Replay operations with custom variable naming
    std::unordered_map<size_t, STerm> results;

    // Generate initial variables
    std::unordered_map<std::string, STerm> initial_variables;
    out_vars.clear();
    out_names.clear();
    for (const auto& name : original_names) {
        if (name_map.count(name)) {
            initial_variables[name] = use_ffi ? FFIVar(name_map[name], solver) : FFVar(name_map[name], solver);
            out_vars.push_back(initial_variables[name]);
            out_names.push_back(name_map[name]);
        } else {
            throw std::runtime_error("Variable not found in name map");
        }
    }

    // Replay operations with custom variable naming
    out_formulas = OperationReplayer::replay(trace, solver, initial_variables, use_ffi);
}

void replay_translator_opcode_constraint_relation(const OperationTrace& trace,
                                                  smt_solver::Solver* solver,
                                                  const std::string& prefix,
                                                  std::vector<STerm>& out_formulas,
                                                  std::vector<STerm>& out_vars,
                                                  std::vector<std::string>& out_names)
{
    // For now, use the same implementation with use_ffi = false
    replay_translator_decomposition_relation(trace, solver, prefix, false, out_formulas, out_vars, out_names);
}

void replay_translator_accumulator_transfer_relation(const OperationTrace& trace,
                                                     smt_solver::Solver* solver,
                                                     const std::string& prefix,
                                                     bool use_ffi,
                                                     std::vector<STerm>& out_formulas,
                                                     std::vector<STerm>& out_vars,
                                                     std::vector<std::string>& out_names)
{
    replay_translator_decomposition_relation(trace, solver, prefix, use_ffi, out_formulas, out_vars, out_names);
}

void instantiate_translator_decomposition_relation_recorded(smt_solver::Solver* solver,
                                                            const std::string& prefix,
                                                            bool use_ffi,
                                                            std::vector<STerm>& out_formulas,
                                                            std::vector<STerm>& out_vars,
                                                            std::vector<std::string>& out_names)
{
    auto trace = record_translator_decomposition_relation();
    replay_translator_decomposition_relation(trace, solver, prefix, use_ffi, out_formulas, out_vars, out_names);
}

void instantiate_translator_opcode_constraint_relation_recorded(smt_solver::Solver* solver,
                                                                const std::string& prefix,
                                                                std::vector<STerm>& out_formulas,
                                                                std::vector<STerm>& out_vars,
                                                                std::vector<std::string>& out_names)
{
    auto trace = record_translator_opcode_constraint_relation();
    replay_translator_opcode_constraint_relation(trace, solver, prefix, out_formulas, out_vars, out_names);
}

} // namespace smt_translator_relations
