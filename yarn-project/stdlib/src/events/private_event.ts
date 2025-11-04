import type { Fr } from '@aztec/foundation/fields';

import type { AztecAddress } from '../aztec-address/index.js';
import type { L2BlockHash } from '../block/block_hash.js';
import type { TxHash } from '../tx/tx_hash.js';

export type PrivateEvent = {
  msgContent: Fr[];
  blockNumber: number;
  blockHash: L2BlockHash;
  txHash: TxHash;
  recipient: AztecAddress;
};
