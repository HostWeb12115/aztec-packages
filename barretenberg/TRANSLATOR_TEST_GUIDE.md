# Translator Relation Testing Guide

## Tests Available

### 1. `test_relation_12_uniqueness_and_maximum`
**Status**: Known issue with term manager when run after other tests

This comprehensive test validates both:
- The uniqueness of the p_x_low_limbs decomposition
- The maximum possible value that satisfies the constraints

**Run individually**:
```bash
cd cpp/build-smt
./bin/smt_verification_tests --gtest_filter="*test_relation_12_uniqueness_and_maximum"
```

### 2. `find_maximum_p_x_low_limbs`  
Finds and verifies the maximum value of p_x_low_limbs.

**Run individually**:
```bash
./bin/smt_verification_tests --gtest_filter="*find_maximum_p_x_low_limbs"
```

## Known Issue

Due to cvc5's term manager architecture, tests using multiple Solver instances may interfere with each other when run sequentially in the same process. Each test works correctly when run individually.

## Results

Both tests validate that:
- ✅ Decomposition is **unique** (UNSAT for different decompositions with same p_x_low_limbs)
- ✅ Maximum value is **295147905179352825855** (0xfffffffffffffffff)
- ✅ Matches theoretical maximum based on range constraints

## New API Functions

- `instantiate_translator_decomposition_with_iterm_return_formulas()` - Returns formulas without asserting
- `create_range_constraint_formulas()` - Creates range constraints: 0 ≤ var < upper_bound
- `assert_formulas_zero()` - Asserts formulas equal zero
- `reset_solver_state()` - Resets internal g_solver state
- `uint256_from_decimal_string()` - Converts decimal strings to uint256_t
- Solver methods: `push()` and `pop()` for incremental solving
