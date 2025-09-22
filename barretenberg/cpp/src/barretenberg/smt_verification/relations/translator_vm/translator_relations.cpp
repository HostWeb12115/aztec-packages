#include "barretenberg/numeric/uint256/uint256.hpp"
#include "barretenberg/relations/translator_vm/translator_decomposition_relation_impl.hpp"
#include "barretenberg/smt_verification/terms/term.hpp"
#include "barretenberg/translator_vm/translator_flavor.hpp"

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

} // namespace detail

void instantiate_translator_decomposition_with_ffterm_and_assert(Solver* solver)
{
    using namespace detail;
    g_solver = solver;

    SymAllEntities in;
    auto refs = in.get_all();
    for (size_t i = 0; i < refs.size(); ++i) {
        refs[i] = SymFF(FFVar("e" + std::to_string(i), solver));
    }

    bb::RelationParameters<SymFF> params;
    SymFF scaling(FFVar("scaling", solver));

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

    auto assert_zero = [&](const SymFF& a) { static_cast<STerm>(a) == bb::fr(0); };
    std::apply([&](auto&... a) { (assert_zero(a.val), ...); }, accs);
}

} // namespace smt_translator_relations
