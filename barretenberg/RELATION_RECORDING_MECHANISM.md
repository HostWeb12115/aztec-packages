# Relation Operation Recording Mechanism

## Problem

Previously, translator relation verification tests had issues with using a global solver pointer (`g_solver`) when constructing relations. This created several problems:

1. **Global State**: Relations depended on a global `g_solver` pointer, making code hard to reason about
2. **Multiple Solver Issues**: Using multiple `Solver` instances sequentially caused interference and crashes
3. **Thread Safety**: Global state makes concurrent testing difficult
4. **Coupling**: Relations were tightly coupled to a specific solver instance

## Solution: VM Trace / Operation Recording

The new mechanism **records operations** that relations perform on inputs, similar to a VM trace or computation graph. These recorded operations can then be **replayed** on any specific solver.

### How It Works

```
┌─────────────────────────────────────────────────────────────┐
│  Step 1: Record (No Solver Needed!)                         │
│  ─────────────────────────────────                          │
│                                                              │
│  Relation::accumulate(RecordingFF inputs)                   │
│         ↓                                                    │
│  Records all operations:                                     │
│   - VAR("p_x_low_limbs")                                    │
│   - CONST(16384)                                            │
│   - MUL(op_5, op_6)                                         │
│   - ADD(op_7, op_8)                                         │
│   - ...                                                      │
│         ↓                                                    │
│  OperationTrace (computation graph)                         │
└─────────────────────────────────────────────────────────────┘
                         │
                         ↓
┌─────────────────────────────────────────────────────────────┐
│  Step 2: Replay (On Any Solver)                             │
│  ────────────────────────────                               │
│                                                              │
│  OperationReplayer::replay(trace, solver1)                  │
│         ↓                                                    │
│  Creates actual SMT terms:                                  │
│   - FFIVar("p_x_low_limbs", solver1)                       │
│   - FFIConst("16384", solver1, 10)                         │
│   - term5 * term6                                           │
│   - term7 + term8                                           │
│   - ...                                                      │
│         ↓                                                    │
│  Vector of STerm formulas                                   │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

#### 1. `RecordingFF` - Field Element That Records

A drop-in replacement for `SymFF` that records operations instead of executing them:

```cpp
RecordingFF a(trace, "p_x_low_limbs");  // Records VAR
RecordingFF b(trace, 16384);             // Records CONST
RecordingFF c = a + b;                   // Records ADD
RecordingFF d = c * RecordingFF(trace, 2); // Records MUL
```

#### 2. `OperationTrace` - The Computation Graph

Stores all operations performed:

```cpp
struct Operation {
    OpKind kind;           // VAR, CONST, ADD, SUB, MUL, NEG
    size_t result_id;      // Unique ID for this operation's result
    size_t lhs_id, rhs_id; // Operand IDs
    std::variant<...> value; // For constants and variables
};

class OperationTrace {
    std::vector<Operation> operations;
    std::unordered_map<size_t, size_t> accumulator_results;
    // Maps accumulator index → operation ID
};
```

#### 3. `OperationReplayer` - Executes on a Solver

Takes a trace and a solver, produces actual SMT terms:

```cpp
auto results = OperationReplayer::replay(trace, &solver, is_ffi);
// Returns: map<operation_id, STerm>
```

### API Usage

#### Low-Level API (Maximum Control)

```cpp
// Step 1: Record once (no solver needed)
auto trace = record_translator_decomposition_relation();

// Step 2: Replay on any solver
Solver s1(...);
Solver s2(...);

std::vector<STerm> formulas1, vars1, names1;
replay_translator_decomposition_relation(*trace, &s1, "prefix1", true, 
                                         formulas1, vars1, names1);

std::vector<STerm> formulas2, vars2, names2;
replay_translator_decomposition_relation(*trace, &s2, "prefix2", true, 
                                         formulas2, vars2, names2);
// No interference between solvers!
```

#### High-Level API (Convenience)

```cpp
// Does record + replay in one call
Solver s(...);
std::vector<STerm> formulas, vars, names;

instantiate_translator_decomposition_relation_recorded(&s, "prefix", true,
                                                       formulas, vars, names);
```

## Benefits

### ✅ No Global State
- Relations no longer depend on `g_solver`
- Pure function: same inputs → same recording
- Easy to test and reason about

### ✅ Multiple Solvers Work
- Record once, replay on any number of solvers
- Each solver gets its own independent terms
- No interference or crashes

### ✅ Reusable Recordings
- Can cache the recording for multiple uses
- Don't need to run the relation multiple times
- Save computation time

### ✅ Thread Safe
- Each thread can have its own replay
- No shared global state
- Parallelizable tests

### ✅ Testable
- Can inspect the operation trace
- Verify what operations are performed
- Debug relation logic independently

## Migration Guide

### Old Code (Global Solver)

```cpp
// OLD: Uses global g_solver pointer
inline thread_local Solver* g_solver = nullptr;

void old_function(Solver* solver) {
    g_solver = solver;  // Set global state
    
    SymFF a;            // Uses g_solver internally
    SymFF b(1);         // Uses g_solver internally
    SymFF c = a + b;    // Uses g_solver internally
    
    // Problem: What if another thread changes g_solver?
}
```

### New Code (Recording)

```cpp
// NEW: No global state, recording-based

void new_function(Solver* solver) {
    auto trace = std::make_shared<OperationTrace>();
    
    RecordingFF a(trace, "var_a"); // Records operations
    RecordingFF b(trace, 1);        // Records operations
    RecordingFF c = a + b;          // Records operations
    
    // Replay when you need actual terms
    auto results = OperationReplayer::replay(*trace, solver, false);
}
```

## Testing

Run the new tests:

```bash
cd cpp/build-smt
./bin/smt_verification_tests --gtest_filter="*TranslatorRelationRecorder*"
```

Expected output:
```
[ RUN      ] TranslatorRelationRecorder.test_recording_mechanism
Recorded 1500+ operations
Recorded 48 accumulator results
Replayed to 48 formulas
SUCCESS: Recording and replay mechanism works!
[       OK ]

[ RUN      ] TranslatorRelationRecorder.test_multiple_solvers_no_interference
SUCCESS: Multiple solvers work without interference!
[       OK ]

[ RUN      ] TranslatorRelationRecorder.test_high_level_api
SUCCESS: High-level API works!
[       OK ]
```

## Security Considerations

⚠️ **Recording Operations Does NOT Change Security**

The recording mechanism is purely a refactoring of how operations are executed:

1. **Same Operations**: Records the EXACT same operations as before
2. **Same Formulas**: Replay produces identical SMT terms
3. **Same Verification**: Security properties are unchanged

What changed:
- **When** operations are bound to a solver (delayed until replay)
- **How** global state is managed (eliminated)

What didn't change:
- **What** operations are performed (identical)
- **What** formulas are generated (identical)
- **What** properties are verified (identical)

## Implementation Files

### Core Infrastructure
- `relation_operation_recorder.hpp` - Recording types and trace structure
- `relation_operation_recorder.cpp` - Replay implementation

### Translator Relations
- `translator_relations_recorder.hpp` - High-level API for translator relations
- `translator_relations_recorder.cpp` - Recording and replay for specific relations
- `translator_relations_recorder.test.cpp` - Tests demonstrating the mechanism

## Future Extensions

This mechanism can be extended to:

1. **Other Relations**: Apply to all relation types, not just translator
2. **Optimization**: Cache and reuse recordings
3. **Serialization**: Save/load traces for debugging
4. **Visualization**: Generate graphs of the computation
5. **JIT Compilation**: Compile traces to native code for faster execution

## Acknowledgments

This solves the issues documented in:
- `MULTIPLE_SOLVER_ISSUE.md`
- `TRANSLATOR_TEST_GUIDE.md`

The mechanism provides a clean, testable, and efficient way to verify relations
without global state or solver coupling.

