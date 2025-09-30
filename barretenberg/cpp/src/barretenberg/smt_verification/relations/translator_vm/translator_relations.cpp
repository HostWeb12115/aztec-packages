#include "barretenberg/numeric/uint256/uint256.hpp"
#include "barretenberg/relations/translator_vm/translator_decomposition_relation_impl.hpp"
#include "barretenberg/smt_verification/terms/term.hpp"
#include "barretenberg/translator_vm/translator_flavor.hpp"
#include <string>
#include <unordered_set>

using namespace smt_terms;

namespace smt_translator_relations {

namespace detail {

inline thread_local Solver* g_solver = nullptr;

struct SymFF {
    STerm t;
    SymFF() { t = FFConst("0", g_solver, 10); }
    explicit SymFF(const STerm& v)
        : t(v)
    {}
    explicit SymFF(const uint64_t v) { t = FFConst(std::to_string(v), g_solver, 10); }
    explicit SymFF(const uint256_t& v) { t = FFConst(bb::fr(v), g_solver); }

    SymFF operator+(const SymFF& o) const { return SymFF(t + o.t); }
    SymFF operator-(const SymFF& o) const { return SymFF(t - o.t); }
    SymFF operator*(const SymFF& o) const { return SymFF(t * o.t); }
    SymFF& operator*=(const SymFF& o)
    {
        t *= o.t;
        return *this;
    }
    SymFF operator-() const { return SymFF(-t); }
    friend SymFF operator*(const uint256_t& c, const SymFF& x) { return SymFF(c) * x; }
    friend SymFF operator*(const bb::fr& c, const SymFF& x)
    {
        return SymFF(STerm(c, x.t.solver, TermType::FFTerm) * x.t);
    }
    friend SymFF operator+(const bb::fr& c, const SymFF& x)
    {
        return SymFF(STerm(c, x.t.solver, TermType::FFTerm) + x.t);
    }
    friend SymFF operator-(const bb::fr& c, const SymFF& x)
    {
        return SymFF(STerm(c, x.t.solver, TermType::FFTerm) - x.t);
    }
    operator STerm() const { return t; }
};

template <size_t LEN> struct UniAcc {
    using ValueType = SymFF;
    using View = SymFF;
    SymFF val;
    UniAcc& operator+=(const SymFF& x)
    {
        val = val + x;
        return *this;
    }
};

struct SymAllEntities : public bb::TranslatorFlavor::AllEntities<SymFF> {};

struct SymFFI {
    STerm t;
    SymFFI() { t = FFIConst("0", g_solver, 10); }
    explicit SymFFI(const STerm& v)
        : t(v)
    {}
    explicit SymFFI(const uint64_t v) { t = FFIConst(std::to_string(v), g_solver, 10); }
    explicit SymFFI(const uint256_t& v) { t = STerm(bb::fr(v), g_solver, TermType::FFITerm); }

    SymFFI operator+(const SymFFI& o) const { return SymFFI(t + o.t); }
    SymFFI operator-(const SymFFI& o) const { return SymFFI(t - o.t); }
    SymFFI operator*(const SymFFI& o) const { return SymFFI(t * o.t); }
    SymFFI& operator*=(const SymFFI& o)
    {
        t *= o.t;
        return *this;
    }
    SymFFI operator-() const { return SymFFI(-t); }
    friend SymFFI operator*(const uint256_t& c, const SymFFI& x) { return SymFFI(c) * x; }
    friend SymFFI operator*(const bb::fr& c, const SymFFI& x)
    {
        return SymFFI(STerm(c, x.t.solver, TermType::FFITerm) * x.t);
    }
    friend SymFFI operator+(const bb::fr& c, const SymFFI& x)
    {
        return SymFFI(STerm(c, x.t.solver, TermType::FFITerm) + x.t);
    }
    friend SymFFI operator-(const bb::fr& c, const SymFFI& x)
    {
        return SymFFI(STerm(c, x.t.solver, TermType::FFITerm) - x.t);
    }
    operator STerm() const { return t; }
};

template <size_t LEN> struct UniAccInt {
    using ValueType = SymFFI;
    using View = SymFFI;
    SymFFI val;
    UniAccInt& operator+=(const SymFFI& x)
    {
        val = val + x;
        return *this;
    }
};

struct SymAllEntitiesInt : public bb::TranslatorFlavor::AllEntities<SymFFI> {};

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

} // namespace detail

void instantiate_translator_decomposition_with_ffterm_and_assert(Solver* solver)
{
    using namespace detail;
    g_solver = solver;

    SymAllEntities in;
    auto refs = in.get_all();
    auto names = build_all_entity_member_names();

    // 1) Fill empty names with placeholders so suffixing works reliably
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i].empty()) {
            names[i] = std::string("E_") + std::to_string(i);
        }
    }

    // // 2) For shifted witnesses, derive labels from their corresponding to-be-shifted base with a _SHIFT suffix
    // {
    //     using Flavor = bb::TranslatorFlavor;
    //     const size_t base_start = Flavor::TO_BE_SHIFTED_WITNESSES_START;
    //     const size_t shift_start = Flavor::SHIFTED_WITNESSES_START;
    //     const size_t count = Flavor::NUM_SHIFTED_WITNESSES;
    //     for (size_t i = 0; i < count; ++i) {
    //         const auto& base_name = names[base_start + i];
    //         names[shift_start + i] = base_name + std::string("_SHIFT");
    //     }
    // }

    // 3) For interleaved constraints at the tail, derive labels if missing
    {
        using Flavor = bb::TranslatorFlavor;
        const size_t interleaved_start = Flavor::INTERLEAVED_START;
        for (size_t i = interleaved_start; i < names.size(); ++i) {
            if (names[i].empty() || names[i].rfind("E_", 0) == 0) {
                names[i] = std::string("INTERLEAVED_RANGE_CONSTRAINTS_") + std::to_string(i - interleaved_start);
            }
        }
    }
    // Ensure uniqueness to avoid collisions in the SMT dump
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < names.size(); ++i) {
        if (!seen.insert(names[i]).second) {
            size_t k = 1;
            const std::string base = names[i];
            std::string candidate = base + std::string("_") + std::to_string(k);
            while (!seen.insert(candidate).second) {
                ++k;
                candidate = base + std::string("_") + std::to_string(k);
            }
            names[i] = candidate;
        }
    }

    for (size_t i = 0; i < refs.size(); ++i) {
        if (names[i] == std::string("lagrange_even_in_minicircuit")) {
            // Set as constant 1 from the start
            refs[i] = SymFF(STerm(bb::fr(1), solver, TermType::FFTerm));
        } else if (names[i] == std::string("op")) {
            // Set as constant 1 from the start
            refs[i] = SymFF(STerm(bb::fr(1), solver, TermType::FFTerm));
        } else {
            refs[i] = SymFF(FFVar(names[i], solver));
        }
    }

    bb::RelationParameters<SymFF> params;
    // Set scaling to constant 1 from the start (FFTerm)
    SymFF scaling(1);

    using RelImpl = bb::TranslatorDecompositionRelationImpl<SymFF>;
    std::tuple<UniAcc<4>,
               UniAcc<4>,
               UniAcc<4>,
               UniAcc<4>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>>
        accs;

    RelImpl::accumulate(accs, in, params, scaling);

    // Explicitly assert each accumulated component equals 0
    {
        STerm zero = FFConst("0", solver, 10);
        auto assert_zero = [&](const SymFF& a) {
            cvc5::Term eq = solver->term_manager.mkTerm(
                cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(static_cast<STerm>(a)), static_cast<cvc5::Term>(zero) });
            solver->assertFormula(eq);
        };
        std::apply([&](auto&... a) { (assert_zero(a.val), ...); }, accs);
    }
}

static void instantiate_translator_decomposition_with_iterm_and_assert_impl(Solver* solver,
                                                                            const std::string& prefix,
                                                                            std::vector<STerm>* collected_vars,
                                                                            std::vector<std::string>* collected_names)
{
    using namespace detail;
    g_solver = solver;

    SymAllEntitiesInt in;
    auto refs = in.get_all();
    auto names = build_all_entity_member_names();

    // 1) Fill empty names with placeholders so suffixing works reliably
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i].empty()) {
            assert(false);
            names[i] = std::string("E_") + std::to_string(i);
        }
    }

    // // 2) For shifted witnesses, derive labels from their corresponding to-be-shifted base with a _SHIFT suffix
    // {
    //     using Flavor = bb::TranslatorFlavor;
    //     const size_t base_start = Flavor::TO_BE_SHIFTED_WITNESSES_START;
    //     const size_t shift_start = Flavor::SHIFTED_WITNESSES_START;
    //     const size_t count = Flavor::NUM_SHIFTED_WITNESSES;
    //     for (size_t i = 0; i < count; ++i) {
    //         const auto& base_name = names[base_start + i];
    //         names[shift_start + i] = base_name + std::string("_SHIFT");
    //     }
    // }

    // 3) Apply optional prefix to allow multiple independent instantiations
    if (!prefix.empty()) {
        for (auto& n : names) {
            n = prefix + std::string("_") + n;
        }
    }

    // 4) For interleaved constraints at the tail, derive labels if missing
    {
        using Flavor = bb::TranslatorFlavor;
        const size_t interleaved_start = Flavor::INTERLEAVED_START;
        for (size_t i = interleaved_start; i < names.size(); ++i) {
            if (names[i].empty() || names[i].rfind("E_", 0) == 0) {
                assert(false);
                names[i] = std::string("INTERLEAVED_RANGE_CONSTRAINTS_") + std::to_string(i - interleaved_start);
            }
        }
    }
    // Ensure uniqueness to avoid collisions in the SMT dump
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < names.size(); ++i) {
        if (!seen.insert(names[i]).second) {
            size_t k = 1;
            const std::string base = names[i];
            std::string candidate = base + std::string("_") + std::to_string(k);
            while (!seen.insert(candidate).second) {
                ++k;
                candidate = base + std::string("_") + std::to_string(k);
            }
            names[i] = candidate;
        }
    }

    for (size_t i = 0; i < refs.size(); ++i) {
        bool is_lagr_even = false;
        {
            static const std::string target = "lagrange_even_in_minicircuit";
            if (names[i].size() >= target.size() && names[i].substr(names[i].size() - target.size()) == target) {
                is_lagr_even = true;
            }
        }
        if (is_lagr_even) {
            STerm one = FFIConst("1", solver, 10);
            refs[i] = SymFFI(one);
            if (collected_vars) {
                collected_vars->push_back(one);
            }
        } else {
            STerm var = FFIVar(names[i], solver);
            refs[i] = SymFFI(var);
            if (collected_vars) {
                collected_vars->push_back(var);
            }
        }
    }
    if (collected_names) {
        collected_names->reserve(collected_names->size() + names.size());
        for (const auto& nm : names) {
            collected_names->push_back(nm);
        }
    }

    bb::RelationParameters<SymFFI> params;
    // Use scaling as constant 1 from the start (FFITerm)
    const std::string scaling_name = prefix.empty() ? std::string("scaling") : (prefix + std::string("_") + "scaling");
    SymFFI scaling(1);

    using RelImpl = bb::TranslatorDecompositionRelationImpl<SymFFI>;
    std::tuple<UniAccInt<4>,
               UniAccInt<4>,
               UniAccInt<4>,
               UniAccInt<4>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>>
        accs;

    RelImpl::accumulate(accs, in, params, scaling);

    // Additional bounds for all variables whose label contains "CONSTRAINT": 0 <= v < 16384
    {
        STerm lower = FFIConst("0", solver, 10);
        STerm upper = FFIConst("16384", solver, 10);
        for (size_t i = 0; i < refs.size(); ++i) {
            if (names[i].find("constraint") != std::string::npos) {
                STerm v = static_cast<STerm>(refs[i]);
                lower <= v;
                v < upper;
            }
        }
    }

    // Assert each accumulated relation equals 0 modulo the circuit modulus (explicit assert)
    {
        STerm zero = FFIConst("0", solver, 10);
        auto assert_zero = [&](const SymFFI& a) {
            cvc5::Term eq = solver->term_manager.mkTerm(
                cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(static_cast<STerm>(a)), static_cast<cvc5::Term>(zero) });
            solver->assertFormula(eq);
        };
        std::apply([&](auto&... a) { (assert_zero(a.val), ...); }, accs);
    }

    // Enforce scaling == 1 and __LAGRANGE_EVEN_IN_MINICIRCUIT == 1 for this instantiation (explicit assert)
    {
        STerm one = FFIConst("1", solver, 10);
        // scaling: assert on the exact term used in accumulate
        {
            STerm s_var = static_cast<STerm>(scaling);
            cvc5::Term eq = solver->term_manager.mkTerm(
                cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(s_var), static_cast<cvc5::Term>(one) });
            solver->assertFormula(eq);
        }
        // lagrange even: assert on the actual ref if present (avoids naming mismatch), otherwise by name
        {
            const std::string lagr_label = std::string("__LAGRANGE_EVEN_IN_MINICIRCUIT");
            const std::string lagr_full = prefix.empty() ? lagr_label : prefix + std::string("_") + lagr_label;
            bool asserted = false;
            for (size_t i = 0; i < names.size(); ++i) {
                if (names[i] == lagr_full) {
                    cvc5::Term eq = solver->term_manager.mkTerm(
                        cvc5::Kind::EQUAL,
                        { static_cast<cvc5::Term>(static_cast<STerm>(refs[i])), static_cast<cvc5::Term>(one) });
                    solver->assertFormula(eq);
                    asserted = true;
                    break;
                }
            }
            if (!asserted) {
                STerm l_var = FFIVar(lagr_full, solver);
                cvc5::Term eq = solver->term_manager.mkTerm(
                    cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(l_var), static_cast<cvc5::Term>(one) });
                solver->assertFormula(eq);
            }
        }
    }
}

void instantiate_translator_decomposition_with_iterm_and_assert(Solver* solver)
{
    instantiate_translator_decomposition_with_iterm_and_assert_impl(solver, "", nullptr, nullptr);
}

void instantiate_translator_decomposition_with_iterm_and_assert_prefix(Solver* solver, const std::string& prefix)
{
    instantiate_translator_decomposition_with_iterm_and_assert_impl(solver, prefix, nullptr, nullptr);
}

void instantiate_translator_decomposition_with_iterm_and_assert_prefix_collect(Solver* solver,
                                                                               const std::string& prefix,
                                                                               std::vector<STerm>& out_vars,
                                                                               std::vector<std::string>& out_names)
{
    instantiate_translator_decomposition_with_iterm_and_assert_impl(solver, prefix, &out_vars, &out_names);
}

// Build uniqueness-style constraints similarly to UltraCircuit::unique_witness:
// - All non-CONSTRAINT variables are equal across the two copies
// - At least one CONSTRAINT variable differs across the two copies
// - scaling == 1 and __LAGRANGE_EVEN_IN_MINICIRCUIT == 1 in both copies
void add_translator_uniqueness_constraints_for_prefix_pair(Solver* solver,
                                                           const std::string& prefix1,
                                                           const std::string& prefix2,
                                                           const std::vector<STerm>& vars1,
                                                           const std::vector<std::string>& names1,
                                                           const std::vector<STerm>& vars2,
                                                           const std::vector<std::string>& names2)
{
    using namespace detail;
    g_solver = solver;

    assert(vars1.size() == names1.size());
    assert(vars2.size() == names2.size());
    assert(vars1.size() == vars2.size());

    // Build equality set and inequality set
    std::vector<STerm> neq_terms;
    for (size_t i = 0; i < vars1.size(); ++i) {
        const auto& first_name = names1[i];
        // const auto& second_name = names2[i];
        const auto& v1 = vars1[i];
        const auto& v2 = vars2[i];
        // Skip uniqueness checks for interleaved and ordered range constraints (not used in this relation)
        if (first_name.find("ordered_range_constraints_") != std::string::npos ||
            first_name.find("interleaved_range_constraints_") != std::string::npos) {
            continue;
        }

        bool is_constraint = (first_name.find("constraint") != std::string::npos) ||
                             (first_name.find("range_constraint") != std::string::npos);
        if (is_constraint) {
            // Collect disjunction of some inequality among CONSTRAINT variables
            // Accumulate inequality via (v1 - v2) != 0; we'll batch-OR Bool for each later
            STerm diff = vars1[i] - vars2[i];
            // Encode inequality as a Bool term by comparing to 0
            // We cannot store Bool directly here; just store diff and generate Bool at the end.
            neq_terms.push_back(diff);
        } else {
            // Skip equating Z_PERM and its shift as requested
            if (first_name.find("Z_PERM") != std::string::npos) {
                continue;
            }
            // Force equality for non-CONSTRAINT variables (explicit assert)
            cvc5::Term eq = solver->term_manager.mkTerm(cvc5::Kind::EQUAL,
                                                        { static_cast<cvc5::Term>(v1), static_cast<cvc5::Term>(v2) });
            solver->assertFormula(eq);
        }
    }

    // At least one CONSTRAINT variable differs
    if (!neq_terms.empty()) {
        // Since Bool wrapper isn't directly available here, assert pairwise inequality OR manually.
        // Build a chain: (t0 != 0) OR (t1 != 0) OR ...
        STerm zero = FFIConst("0", solver, 10);
        // Start with first disjunct
        cvc5::Term disj = solver->term_manager.mkTerm(
            cvc5::Kind::NOT,
            { solver->term_manager.mkTerm(cvc5::Kind::EQUAL,
                                          { static_cast<cvc5::Term>(neq_terms[0]), static_cast<cvc5::Term>(zero) }) });
        for (size_t i = 1; i < neq_terms.size(); ++i) {
            cvc5::Term lit = solver->term_manager.mkTerm(
                cvc5::Kind::NOT,
                { solver->term_manager.mkTerm(
                    cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(neq_terms[i]), static_cast<cvc5::Term>(zero) }) });
            disj = solver->term_manager.mkTerm(cvc5::Kind::OR, { disj, lit });
        }
        solver->assertFormula(disj);
    }

    // Enforce scaling == 1 and __LAGRANGE_EVEN_IN_MINICIRCUIT == 1 for both copies
    STerm one = FFIConst("1", solver, 10);
    FFIVar(prefix1 + std::string("_") + std::string("scaling"), solver) == one;
    FFIVar(prefix2 + std::string("_") + std::string("scaling"), solver) == one;

    // The Lagrange even flag is labeled by flavor; use the same labeling as instantiation
    const std::string lagr_name = std::string("__LAGRANGE_EVEN_IN_MINICIRCUIT");
    FFIVar(prefix1 + std::string("_") + lagr_name, solver) == one;
    FFIVar(prefix2 + std::string("_") + lagr_name, solver) == one;
}

std::vector<STerm> extract_translator_decomposition_relation_formulas(Solver* solver)
{
    using namespace detail;
    g_solver = solver;

    SymAllEntities in;
    auto refs = in.get_all();
    auto names = build_all_entity_member_names();

    // 1) Fill empty names with placeholders so suffixing works reliably
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i].empty()) {
            names[i] = std::string("E_") + std::to_string(i);
        }
    }

    // 2) For interleaved constraints at the tail, derive labels if missing
    {
        using Flavor = bb::TranslatorFlavor;
        const size_t interleaved_start = Flavor::INTERLEAVED_START;
        for (size_t i = interleaved_start; i < names.size(); ++i) {
            if (names[i].empty() || names[i].rfind("E_", 0) == 0) {
                names[i] = std::string("INTERLEAVED_RANGE_CONSTRAINTS_") + std::to_string(i - interleaved_start);
            }
        }
    }

    // Ensure uniqueness to avoid collisions in the SMT dump
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < names.size(); ++i) {
        if (!seen.insert(names[i]).second) {
            size_t k = 1;
            const std::string base = names[i];
            std::string candidate = base + std::string("_") + std::to_string(k);
            while (!seen.insert(candidate).second) {
                ++k;
                candidate = base + std::string("_") + std::to_string(k);
            }
            names[i] = candidate;
        }
    }

    for (size_t i = 0; i < refs.size(); ++i) {
        if (names[i] == std::string("lagrange_even_in_minicircuit")) {
            // Set as constant 1 from the start
            refs[i] = SymFF(STerm(bb::fr(1), solver, TermType::FFTerm));
        } else {
            refs[i] = SymFF(FFVar(names[i], solver));
        }
    }

    bb::RelationParameters<SymFF> params;
    // Set scaling to constant 1 from the start (FFTerm)
    SymFF scaling(1);

    using RelImpl = bb::TranslatorDecompositionRelationImpl<SymFF>;
    std::tuple<UniAcc<4>,
               UniAcc<4>,
               UniAcc<4>,
               UniAcc<4>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<2>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>,
               UniAcc<3>>
        accs;

    RelImpl::accumulate(accs, in, params, scaling);

    // Extract each accumulated component as an STerm
    std::vector<STerm> formulas;
    std::apply([&](auto&... a) { (formulas.push_back(static_cast<STerm>(a.val)), ...); }, accs);

    return formulas;
}

void instantiate_translator_decomposition_with_iterm_return_formulas(Solver* solver,
                                                                     const std::string& prefix,
                                                                     std::vector<STerm>& out_formulas,
                                                                     std::vector<STerm>& out_vars,
                                                                     std::vector<std::string>& out_names)
{
    using namespace detail;
    g_solver = solver;

    SymAllEntitiesInt in;
    auto refs = in.get_all();
    auto names = build_all_entity_member_names();

    // 1) Fill empty names with placeholders so suffixing works reliably
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i].empty()) {
            assert(false);
            names[i] = std::string("E_") + std::to_string(i);
        }
    }

    // 2) Apply optional prefix to allow multiple independent instantiations
    if (!prefix.empty()) {
        for (auto& n : names) {
            n = prefix + std::string("_") + n;
        }
    }

    // 3) For interleaved constraints at the tail, derive labels if missing
    {
        using Flavor = bb::TranslatorFlavor;
        const size_t interleaved_start = Flavor::INTERLEAVED_START;
        for (size_t i = interleaved_start; i < names.size(); ++i) {
            if (names[i].empty() || names[i].rfind("E_", 0) == 0) {
                assert(false);
                names[i] = std::string("INTERLEAVED_RANGE_CONSTRAINTS_") + std::to_string(i - interleaved_start);
            }
        }
    }

    // Ensure uniqueness to avoid collisions in the SMT dump
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < names.size(); ++i) {
        if (!seen.insert(names[i]).second) {
            size_t k = 1;
            const std::string base = names[i];
            std::string candidate = base + std::string("_") + std::to_string(k);
            while (!seen.insert(candidate).second) {
                ++k;
                candidate = base + std::string("_") + std::to_string(k);
            }
            names[i] = candidate;
        }
    }

    for (size_t i = 0; i < refs.size(); ++i) {
        bool is_lagr_even = false;
        bool is_op = false;
        {
            static const std::string target = "lagrange_even_in_minicircuit";
            if (names[i].size() >= target.size() && names[i].substr(names[i].size() - target.size()) == target) {
                is_lagr_even = true;
            }
        }
        {
            static const std::string target = "op";
            if (names[i] == target) {
                is_op = true;
            }
        }
        if (is_lagr_even || is_op) {
            STerm one = FFIConst("1", solver, 10);
            refs[i] = SymFFI(one);
            out_vars.push_back(one);
        } else {
            STerm var = FFIVar(names[i], solver);
            refs[i] = SymFFI(var);
            out_vars.push_back(var);
        }
    }
    out_names.reserve(out_names.size() + names.size());
    for (const auto& nm : names) {
        out_names.push_back(nm);
    }

    bb::RelationParameters<SymFFI> params;
    // Use scaling as constant 1 from the start (FFITerm)
    SymFFI scaling(1);

    using RelImpl = bb::TranslatorDecompositionRelationImpl<SymFFI>;
    std::tuple<UniAccInt<4>,
               UniAccInt<4>,
               UniAccInt<4>,
               UniAccInt<4>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<2>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>,
               UniAccInt<3>>
        accs;

    RelImpl::accumulate(accs, in, params, scaling);

    // Extract each accumulated component as an STerm (relation formulas)
    std::apply([&](auto&... a) { (out_formulas.push_back(static_cast<STerm>(a.val)), ...); }, accs);
}

std::vector<STerm> create_range_constraint_formulas(Solver* solver,
                                                    const std::vector<STerm>& vars,
                                                    const std::vector<std::string>& var_names,
                                                    const std::string& name_pattern,
                                                    uint64_t upper_bound)
{
    using namespace detail;
    g_solver = solver; // Set global solver for term operations

    std::vector<STerm> constraints;

    STerm lower = FFIConst("0", solver, 10);
    STerm upper = FFIConst(std::to_string(upper_bound), solver, 10);

    for (size_t i = 0; i < vars.size(); ++i) {
        if (var_names[i].find(name_pattern) != std::string::npos) {
            // Create constraints: var >= 0 AND var < upper_bound
            // These will be automatically asserted when evaluated
            lower <= vars[i];
            vars[i] < upper;
        }
    }

    return constraints;
}

void assert_formulas_zero(Solver* solver, const std::vector<STerm>& formulas)
{
    using namespace detail;
    STerm zero = FFIConst("0", solver, 10);
    for (const auto& formula : formulas) {
        cvc5::Term eq = solver->term_manager.mkTerm(
            cvc5::Kind::EQUAL, { static_cast<cvc5::Term>(formula), static_cast<cvc5::Term>(zero) });
        solver->assertFormula(eq);
    }
}

void reset_solver_state()
{
    using namespace detail;
    g_solver = nullptr;
}

} // namespace smt_translator_relations
