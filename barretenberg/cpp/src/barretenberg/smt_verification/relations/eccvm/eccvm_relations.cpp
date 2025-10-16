#include "eccvm_relations.hpp"
#include "barretenberg/common/zip_view.hpp"
#include "barretenberg/eccvm/eccvm_flavor.hpp"
#include "barretenberg/relations/ecc_vm/ecc_bools_relation_impl.hpp"
#include "barretenberg/relations/ecc_vm/ecc_wnaf_relation_impl.hpp"
#include "barretenberg/smt_verification/relations/relation_operation_recorder.hpp"

using namespace bb;
using namespace smt_relation_recorder;
using namespace smt_terms;

// Explicitly instantiate the templates for RecordingFF
namespace bb {
template class ECCVMBoolsRelationImpl<RecordingFF>;
template class ECCVMWnafRelationImpl<RecordingFF>;
} // namespace bb

namespace smt_eccvm_relations {

// Build list of all ECCVM entity names
static std::vector<std::string> build_all_entity_member_names()
{
    using Flavor = ECCVMFlavor;
    using AllEntities = typename Flavor::AllEntities<RecordingFF>;

    AllEntities symbolic_all_entities;
    std::vector<std::string> names;

    for (auto [name, _] : zip_view(symbolic_all_entities.get_labels(), symbolic_all_entities.get_all())) {
        names.push_back(name);
    }

    return names;
}

OperationTrace record_eccvm_bools_relation()
{
    auto trace = std::make_shared<OperationTrace>();

    // Set the default trace for this thread so single-argument constructors work
    RecordingFF::default_trace = trace;

    // Create a structure to hold RecordingFF entities
    using Flavor = ECCVMFlavor;
    using AllEntities = typename Flavor::AllEntities<RecordingFF>;
    using Relation = ECCVMBoolsRelation<RecordingFF>;

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
        refs[i].get() = RecordingFF(trace, names[i]);
    }

    // Create accumulator tuple - ECCVMBoolsRelation has 19 boolean constraints (all degree 2)
    std::tuple<RecordingAccumulator<2>,
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
               RecordingAccumulator<2>>
        acc;

    // Initialize all accumulators to zero
    std::apply([&](auto&... a) { ((a.val = RecordingFF(trace, static_cast<uint64_t>(0))), ...); }, acc);

    // Create relation parameters (empty for bools relation)
    RelationParameters<RecordingFF> params;

    // Create scaling factor as 1
    RecordingFF scaling_factor(trace, static_cast<uint64_t>(1));

    // Record the relation execution
    Relation::accumulate(acc, symbolic_all_entities, params, scaling_factor);

    // Record which operation IDs correspond to each accumulator's final value
    size_t acc_idx = 0;
    std::apply([&](auto&... a) { ((trace->set_accumulator_result(acc_idx++, a.val.operation_id.value())), ...); }, acc);

    // Clear the default trace
    RecordingFF::default_trace.reset();

    return *trace;
}

void replay_eccvm_bools_relation(const OperationTrace& trace,
                                 smt_solver::Solver* solver,
                                 const std::string& prefix,
                                 bool use_ffi,
                                 std::vector<STerm>& out_formulas,
                                 std::vector<STerm>& out_vars,
                                 std::vector<std::string>& out_names)
{
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

    // Replay operations
    out_formulas = OperationReplayer::replay(trace, solver, initial_variables, use_ffi);
}

OperationTrace record_eccvm_wnaf_relation()
{
    auto trace = std::make_shared<OperationTrace>();

    // Set the default trace for this thread so single-argument constructors work
    RecordingFF::default_trace = trace;

    // Create a structure to hold RecordingFF entities
    using Flavor = ECCVMFlavor;
    using AllEntities = typename Flavor::AllEntities<RecordingFF>;
    using Relation = ECCVMWnafRelation<RecordingFF>;

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
        refs[i].get() = RecordingFF(trace, names[i]);
    }

    // Create accumulator tuple - ECCVMWnafRelation has 21 subrelations (8 for 2-bit range checks + 13 others)
    // All are degree 4 or less based on the implementation
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

    // Initialize all accumulators to zero
    std::apply([&](auto&... a) { ((a.val = RecordingFF(trace, bb::fr::zero())), ...); }, acc);

    // Create relation parameters (empty for wnaf relation)
    RelationParameters<RecordingFF> params;

    // Create scaling factor as 1
    RecordingFF scaling_factor(trace, bb::fr::one());

    // Record the relation execution
    Relation::accumulate(acc, symbolic_all_entities, params, scaling_factor);

    // Record which operation IDs correspond to each accumulator's final value
    size_t acc_idx = 0;
    std::apply([&](auto&... a) { ((trace->set_accumulator_result(acc_idx++, a.val.operation_id.value())), ...); }, acc);

    // Clear the default trace
    RecordingFF::default_trace.reset();

    return *trace;
}

void replay_eccvm_wnaf_relation(const OperationTrace& trace,
                                smt_solver::Solver* solver,
                                const std::string& prefix,
                                bool use_ffi,
                                std::vector<STerm>& out_formulas,
                                std::vector<STerm>& out_vars,
                                std::vector<std::string>& out_names)
{
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

    // Replay operations
    out_formulas = OperationReplayer::replay(trace, solver, initial_variables, use_ffi);
}

} // namespace smt_eccvm_relations
