# Translator Decomposition Relation Testing Guide

## Summary of Changes

I've refactored the translator decomposition relation code to allow **selective testing of specific subrelations**. Previously, all relations were immediately asserted to the solver. Now you can:

1. Extract individual relation formulas
2. Selectively assert only the relations you want to test
3. Apply custom constraints (like range constraints) to specific variables
4. Test uniqueness properties of specific decompositions

## New API Functions

### 1. `instantiate_translator_decomposition_with_iterm_return_formulas`

Returns formulas, variables, and names **without** asserting them to the solver.

```cpp
void instantiate_translator_decomposition_with_iterm_return_formulas(
    Solver* solver,
    const std::string& prefix,
    std::vector<STerm>& out_formulas,    // 48 relation formulas
    std::vector<STerm>& out_vars,        // All variables
    std::vector<std::string>& out_names  // Variable names
);
```

**Usage:**
```cpp
Solver s(kBn254ScalarModulusHex, debug_solver_config);
std::vector<STerm> formulas, vars;
std::vector<std::string> names;

instantiate_translator_decomposition_with_iterm_return_formulas(
    &s, "C1", formulas, vars, names);

// Now you have 48 formulas you can selectively assert
// formulas[12] = p_x_low_limbs decomposition
// formulas[22] = p_x_low_limbs tail constraint
```

### 2. `create_range_constraint_formulas`

Creates range constraint formulas (0 ≤ var < upper_bound) for variables matching a pattern.

```cpp
std::vector<STerm> create_range_constraint_formulas(
    Solver* solver,
    const std::vector<STerm>& vars,
    const std::vector<std::string>& var_names,
    const std::string& name_pattern,
    uint64_t upper_bound
);
```

**Usage:**
```cpp
// Constrain all variables with "constraint" in their name to [0, 16384)
create_range_constraint_formulas(&s, vars, names, "constraint", 16384);
```

### 3. `assert_formulas_zero`

Helper to assert that formulas equal zero.

```cpp
void assert_formulas_zero(Solver* solver, const std::vector<STerm>& formulas);
```

**Usage:**
```cpp
// Assert only specific relations
assert_formulas_zero(&s, { formulas[12], formulas[22] });
```

## Relation Index Reference

From the printed output, here are the key relations:

- **Relation 12**: `p_x_low_limbs` decomposition into 5 range constraints
  ```
  p_x_low_limbs = rc_0 + rc_1*16384 + rc_2*16384² + rc_3*16384³ + rc_4*16384⁴
  ```

- **Relation 22**: `p_x_low_limbs_range_constraint_tail` relationship
  ```
  tail = rc_4 * 4
  ```

- **Relations 0-3**: Accumulator limbs decomposition
- **Relations 4-21**: Other limb decompositions (p_y, z, quotient, relation_wide)
- **Relations 22-41**: Tail constraints
- **Relations 42-47**: Cross-limb relationships

## Example Test: Uniqueness of Relation 12

The test `test_relation_12_uniqueness_with_same_p_x_low_limbs` demonstrates:

1. **Create two independent copies** of the relation (C1 and C2)
2. **Apply range constraints** to all constraint variables
3. **Assert relations 12 and 22** for both copies
4. **Force same `p_x_low_limbs`** in both copies
5. **Ask SMT solver**: Can we find different decompositions?

**Expected Result**: UNSAT (decomposition is unique)

### Security Note

⚠️ If the test returns **SAT**, it would indicate a **security vulnerability**: multiple valid decompositions exist for the same value, which could potentially be exploited.

## Running the Tests

```bash
cd cpp/build-smt

# Print all relation formulas
./bin/smt_verification_tests --gtest_filter="*print_individual_decomposition_relation_formulas"

# Test relation 12 uniqueness
./bin/smt_verification_tests --gtest_filter="*test_relation_12_uniqueness_with_same_p_x_low_limbs"
```

## Creating Custom Tests

Here's a template for testing other relations:

```cpp
TEST(TranslatorRelationVerification, test_custom_relation)
{
    Solver s(kBn254ScalarModulusHex, debug_solver_config);
    
    // Get formulas without asserting
    std::vector<STerm> formulas, vars;
    std::vector<std::string> names;
    instantiate_translator_decomposition_with_iterm_return_formulas(
        &s, "", formulas, vars, names);
    
    // Apply range constraints
    create_range_constraint_formulas(&s, vars, names, "constraint", 16384);
    
    // Assert only the relations you care about
    assert_formulas_zero(&s, { 
        formulas[12],  // Your relation of interest
        formulas[22]   // Related constraint
    });
    
    // Add custom constraints here
    // ...
    
    // Check satisfiability
    bool res = s.check();
    ASSERT_FALSE(res);  // or ASSERT_TRUE depending on what you expect
}
```

## Files Modified

1. **translator_relations.hpp**: Added new function declarations
2. **translator_relations.cpp**: Implemented new functions
3. **translator_relation_verification.test.cpp**: Added example test

## Next Steps

You can now:
- Test individual subrelations in isolation
- Add custom equality/inequality constraints
- Test uniqueness of any decomposition
- Verify cross-relation properties
