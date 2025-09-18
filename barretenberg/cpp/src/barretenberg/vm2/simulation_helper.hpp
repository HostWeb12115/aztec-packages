#pragma once

#include "barretenberg/vm2/common/avm_inputs.hpp"
#include "barretenberg/vm2/simulation/events/events_container.hpp"
#include <cstdint>

namespace bb::avm2 {

class AvmSimulationHelper {
  public:
    AvmSimulationHelper(ExecutionHints hints, const PublicInputs& publicInputs)
        : hints(std::move(hints))
        , publicDataWrites(publicInputs.accumulatedData.publicDataWrites)
        , publicDataWritesLength(publicInputs.accumulatedDataArrayLengths.publicDataWrites)
    {}

    // Full simulation with event collection.
    simulation::EventsContainer simulate();

    // Fast simulation without event collection.
    void simulate_fast();

  private:
    template <typename S> simulation::EventsContainer simulate_with_settings();

    ExecutionHints hints;
    // The only portion of the public inputs that we need in simulation.
    // Required to generate some ff_gt events at the end of the simulation in order to
    // constrain that leaf slots are sorted in order of public data writes for squashing.
    std::array<PublicDataWrite, MAX_TOTAL_PUBLIC_DATA_UPDATE_REQUESTS_PER_TX> publicDataWrites;
    uint32_t publicDataWritesLength;
};

} // namespace bb::avm2
