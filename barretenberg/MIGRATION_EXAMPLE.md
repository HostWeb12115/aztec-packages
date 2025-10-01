# Migration Example: From Global Solver to Recording Mechanism

## Example: Updating a Test to Use Recording

### Before (Using Global Solver)

```cpp
TEST(TranslatorRelationVerification, test_old_way)
{
    // Reset global state from previous tests
    smt_translator_relations::reset_solver_state();
    
    smt_solver::Solver s("30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001",
                         smt_solver::default_solver_config);
    
    // This uses global g_solver pointer internally
    auto formulas = smt_translator_relations::extract_translator_decomposition_relation_formulas(&s);
    
    // Assert formulas
    for (const auto& formula : formulas) {
        // ... test logic ...
    }
}
```

**Problems:**
- Requires `reset_solver_state()` to clean global state
- Cannot use multiple solvers safely
- Hard to parallelize
- Tests interfere with each other

### After (Using Recording)

```cpp
TEST(TranslatorRelationVerification, test_new_way)
{
    // No need to reset global state - there is none!
    
    smt_solver::Solver s("30644e72e131a029b85045b68181585d2833e84879b9709143e1f593f0000001",
                         smt_solver::default_solver_config);
    
    // Record and replay - no global state
    std::vector<smt_terms::STerm> formulas, vars, names;
    smt_translator_relations::instantiate_translator_decomposition_relation_recorded(
        &s, "", true, formulas, vars, names);
    
    // Assert formulas
    for (const auto& formula : formulas) {
        // ... test logic ...
    }
}
```

**Benefits:**
- No global state to manage
- Can use multiple solvers safely
- Tests are independent
- Can parallelize easily

## Example: Reusing Recordings Across Tests

```cpp
class TranslatorRelationTests : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Record ONCE for all tests in the suite
        s_decomposition_trace = smt_translator_relations::record_translator_decomposition_relation();
    }
    
    static std::shared_ptr<smt_relation_recorder::OperationTrace> s_decomposition_trace;
};

std::shared_ptr<smt_relation_recorder::OperationTrace> TranslatorRelationTests::s_decomposition_trace;

TEST_F(TranslatorRelationTests, test_with_solver1)
{
    smt_solver::Solver s(...);
    
    std::vector<smt_terms::STerm> formulas, vars, names;
    smt_translator_relations::replay_translator_decomposition_relation(
        *s_decomposition_trace, &s, "test1", true, formulas, vars, names);
    
    // Test logic...
}

TEST_F(TranslatorRelationTests, test_with_solver2)
{
    smt_solver::Solver s(...);
    
    std::vector<smt_terms::STerm> formulas, vars, names;
    smt_translator_relations::replay_translator_decomposition_relation(
        *s_decomposition_trace, &s, "test2", true, formulas, vars, names);
    
    // Test logic...
}
```

**Benefits:**
- Recording computed only once
- Each test gets its own replay
- Faster test suite

## Example: Multiple Solvers, No Interference

```cpp
TEST(TranslatorRelationVerification, test_uniqueness_with_multiple_copies)
{
    // Record once
    auto trace = smt_translator_relations::record_translator_decomposition_relation();
    
    smt_solver::Solver s("...", smt_solver::default_solver_config);
    
    // Create first copy with prefix "C1"
    std::vector<smt_terms::STerm> formulas1, vars1, names1;
    smt_translator_relations::replay_translator_decomposition_relation(
        *trace, &s, "C1", true, formulas1, vars1, names1);
    
    // Create second copy with prefix "C2"
    std::vector<smt_terms::STerm> formulas2, vars2, names2;
    smt_translator_relations::replay_translator_decomposition_relation(
        *trace, &s, "C2", true, formulas2, vars2, names2);
    
    // Now we have two independent copies on the SAME solver
    // with no interference!
    
    // Assert formulas equal zero
    for (const auto& f : formulas1) {
        // ... assert C1 formulas ...
    }
    for (const auto& f : formulas2) {
        // ... assert C2 formulas ...
    }
    
    // Add uniqueness constraints
    // Force same p_x_low_limbs but different decompositions
    // ...
    
    bool result = s.check();
    EXPECT_FALSE(result); // Should be UNSAT (unique decomposition)
}
```

## Performance Comparison

### Old Way
```
Test 1: Reset state → Set g_solver → Run relation → Test
Test 2: Reset state → Set g_solver → Run relation → Test
Test 3: Reset state → Set g_solver → Run relation → Test
...
Total: N × (reset + set + run + test)
```

### New Way with Recording
```
Setup: Record relation once
Test 1: Replay → Test
Test 2: Replay → Test
Test 3: Replay → Test
...
Total: Record once + N × (replay + test)
```

Replay is typically faster than running the relation from scratch.

## API Reference

### Recording Functions

```cpp
// Record decomposition relation
std::shared_ptr<OperationTrace> record_translator_decomposition_relation();

// Record opcode constraint relation
std::shared_ptr<OperationTrace> record_translator_opcode_constraint_relation();
```

### Replay Functions

```cpp
// Replay decomposition relation
void replay_translator_decomposition_relation(
    const OperationTrace& trace,
    Solver* solver,
    const std::string& prefix,
    bool use_ffi,
    std::vector<STerm>& out_formulas,
    std::vector<STerm>& out_vars,
    std::vector<std::string>& out_names);

// Replay opcode constraint relation
void replay_translator_opcode_constraint_relation(
    const OperationTrace& trace,
    Solver* solver,
    const std::string& prefix,
    std::vector<STerm>& out_formulas,
    std::vector<STerm>& out_vars,
    std::vector<std::string>& out_names);
```

### High-Level API (Record + Replay)

```cpp
// Decomposition relation
void instantiate_translator_decomposition_relation_recorded(
    Solver* solver,
    const std::string& prefix,
    bool use_ffi,
    std::vector<STerm>& out_formulas,
    std::vector<STerm>& out_vars,
    std::vector<std::string>& out_names);

// Opcode constraint relation
void instantiate_translator_opcode_constraint_relation_recorded(
    Solver* solver,
    const std::string& prefix,
    std::vector<STerm>& out_formulas,
    std::vector<STerm>& out_vars,
    std::vector<std::string>& out_names);
```

## Common Patterns

### Pattern 1: Drop-in Replacement

Replace the old function call with the new one:

```cpp
// Old
extract_translator_decomposition_relation_formulas(&s);

// New
std::vector<STerm> formulas, vars, names;
instantiate_translator_decomposition_relation_recorded(&s, "", false, formulas, vars, names);
// Note: use_ffi = false for FFTerm, true for FFITerm
```

### Pattern 2: Record Once, Use Many Times

```cpp
// At test suite setup
auto g_trace = record_translator_decomposition_relation();

// In each test
void test_something(Solver& s) {
    std::vector<STerm> formulas, vars, names;
    replay_translator_decomposition_relation(*g_trace, &s, "test", true, 
                                             formulas, vars, names);
    // Use formulas...
}
```

### Pattern 3: Multiple Independent Copies

```cpp
auto trace = record_translator_decomposition_relation();
Solver s(...);

// Copy 1
std::vector<STerm> f1, v1, n1;
replay_translator_decomposition_relation(*trace, &s, "copy1", true, f1, v1, n1);

// Copy 2
std::vector<STerm> f2, v2, n2;
replay_translator_decomposition_relation(*trace, &s, "copy2", true, f2, v2, n2);

// f1 and f2 are independent!
```

## Checklist for Migration

- [ ] Remove `reset_solver_state()` calls
- [ ] Remove global `g_solver` usage
- [ ] Replace `extract_*_formulas` with `instantiate_*_relation_recorded`
- [ ] Update function signatures to include output vectors
- [ ] Consider caching recordings for performance
- [ ] Update test comments to reflect no global state
- [ ] Run tests to verify correctness
- [ ] Celebrate no more global state! 🎉

