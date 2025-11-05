import { CHECKPOINT_HEADER_SIZE_IN_BYTES } from '@aztec/constants';
import { EthAddress } from '@aztec/foundation/eth-address';
import { Fr } from '@aztec/foundation/fields';
import { updateInlineTestData } from '@aztec/foundation/testing/files';

import { AztecAddress } from '../aztec-address/index.js';
import { GasFees } from '../gas/gas_fees.js';
import { makeCheckpointHeader } from '../tests/factories.js';
import { CheckpointHeader } from './checkpoint_header.js';

describe('CheckpointHeader', () => {
  it('serializes to buffer and deserializes it back', () => {
    const header = makeCheckpointHeader(9870243);
    const buffer = header.toBuffer();
    expect(buffer.length).toBe(CHECKPOINT_HEADER_SIZE_IN_BYTES);
    const res = CheckpointHeader.fromBuffer(buffer);
    expect(res).toEqual(header);
  });

  it('computes header hash', () => {
    const header = CheckpointHeader.from({
      lastArchiveRoot: new Fr(123123),
      blobsHash: new Fr(886644),
      inHash: new Fr(335577),
      outHash: new Fr(996633),
      slotNumber: new Fr(1234),
      timestamp: 5678n,
      coinbase: EthAddress.fromNumber(2244),
      feeRecipient: AztecAddress.fromNumber(6688),
      gasFees: GasFees.from({ feePerDaGas: 100n, feePerL2Gas: 200n }),
      totalManaUsed: new Fr(151617),
    });
    const hash = header.hash();
    expect(hash).toMatchInlineSnapshot(`"0x0093db87a514e4d40b8e4644ca4b31e415512e4424bc40c03d621c8e6b74db29"`);

    // Run with AZTEC_GENERATE_TEST_DATA=1 to update noir test data.
    updateInlineTestData(
      'noir-projects/noir-protocol-circuits/crates/types/src/abis/checkpoint_header.nr',
      'checkpoint_header_hash_from_ts',
      hash.toString(),
    );
  });
});
