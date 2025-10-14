import { Fr } from '@aztec/foundation/fields';
import { KeyStore } from '@aztec/key-store';
import { openTmpStore } from '@aztec/kv-store/lmdb-v2';
import { AztecAddress } from '@aztec/stdlib/aztec-address';
import type { AztecNode } from '@aztec/stdlib/interfaces/client';
import { PrivateLog, TxScopedL2Log } from '@aztec/stdlib/logs';
import { TxHash, TxStatus } from '@aztec/stdlib/tx';

import { type MockProxy, mock } from 'jest-mock-extended';

import { PXEOracleInterface } from '../contract_function_simulator/pxe_oracle_interface.js';
import { AddressDataProvider } from '../storage/address_data_provider/address_data_provider.js';
import { CapsuleDataProvider } from '../storage/capsule_data_provider/capsule_data_provider.js';
import { ContractDataProvider } from '../storage/contract_data_provider/contract_data_provider.js';
import { NoteDataProvider } from '../storage/note_data_provider/note_data_provider.js';
import { PrivateEventDataProvider } from '../storage/private_event_data_provider/private_event_data_provider.js';
import { SyncDataProvider } from '../storage/sync_data_provider/sync_data_provider.js';
import { TaggingDataProvider } from '../storage/tagging_data_provider/tagging_data_provider.js';
import { DirectionalAppTaggingSecret, SiloedTag, Tag } from '../tagging/index.js';

describe('TaggingSync', () => {
  let aztecNode: MockProxy<AztecNode>;

  let addressDataProvider: AddressDataProvider;
  let privateEventDataProvider: PrivateEventDataProvider;
  let contractDataProvider: ContractDataProvider;
  let noteDataProvider: NoteDataProvider;
  let syncDataProvider: SyncDataProvider;
  let taggingDataProvider: TaggingDataProvider;
  let capsuleDataProvider: CapsuleDataProvider;
  let keyStore: KeyStore;

  let pxeOracleInterface: PXEOracleInterface;

  // Contract address and secret to be used on the input of the syncTaggedLogsAsSender function.
  let contractAddress: AztecAddress;
  let secret: DirectionalAppTaggingSecret;

  async function computeSiloedTagForIndex(index: number) {
    const tag = await Tag.compute({ secret, index });
    return SiloedTag.compute(tag, contractAddress);
  }

  function makeLog(txHash: TxHash, tag: Fr) {
    return new TxScopedL2Log(txHash, 0, 0, 0, PrivateLog.random(tag));
  }

  async function setUp() {
    const store = await openTmpStore('test');
    aztecNode = mock<AztecNode>();
    contractDataProvider = new ContractDataProvider(store);

    addressDataProvider = new AddressDataProvider(store);
    privateEventDataProvider = new PrivateEventDataProvider(store);
    noteDataProvider = await NoteDataProvider.create(store);
    syncDataProvider = new SyncDataProvider(store);
    taggingDataProvider = new TaggingDataProvider(store);
    capsuleDataProvider = new CapsuleDataProvider(store);
    keyStore = new KeyStore(store);
    pxeOracleInterface = new PXEOracleInterface(
      aztecNode,
      keyStore,
      contractDataProvider,
      noteDataProvider,
      capsuleDataProvider,
      syncDataProvider,
      taggingDataProvider,
      addressDataProvider,
      privateEventDataProvider,
    );

    contractAddress = await AztecAddress.random();
    secret = DirectionalAppTaggingSecret.fromString(Fr.random().toString());
  }

  it('no new logs found for a given secret', async () => {
    await setUp();

    aztecNode.getLogsByTags.mockImplementation((tags: Fr[]) => {
      // No log found for any tag
      return Promise.resolve(tags.map((_tag: Fr) => []));
    });

    await pxeOracleInterface.syncTaggedLogsAsSender(secret, contractAddress);

    // Highest used and finalized indexes should stay undefined
    expect(await taggingDataProvider.getHighestUsedIndexAsSender(secret)).toBeUndefined();
    expect(await taggingDataProvider.getHighestFinalizedIndex(secret)).toBeUndefined();
  });

  // These tests need to be run together in sequence.
  describe('sequential tests', () => {
    const finalizedIndexStep1 = 3;
    let pendingTxHash: TxHash;

    beforeAll(async () => {
      await setUp();
    });

    it('step 1: highest finalized index is updated', async () => {
      const finalizedBlockNumber = 15;

      // Create a log with tag index 3
      const index3Tag = await computeSiloedTagForIndex(finalizedIndexStep1);

      aztecNode.getLogsByTags.mockImplementation((tags: Fr[]) => {
        // Return empty arrays for all tags except the one at index 3
        return Promise.resolve(
          tags.map((tag: Fr) => (tag.equals(index3Tag.value) ? [makeLog(TxHash.random(), index3Tag.value)] : [])),
        );
      });

      // Mock getTxReceipt to return a successful, finalized tx (finalized because it is included in a block before
      // the finalized block)
      aztecNode.getTxReceipt.mockResolvedValue({
        status: TxStatus.SUCCESS,
        blockNumber: finalizedBlockNumber - 1,
      } as any);

      // Mock getL2Tips to return a finalized block number >= the tx block number
      aztecNode.getL2Tips.mockResolvedValue({
        finalized: { number: finalizedBlockNumber },
      } as any);

      await pxeOracleInterface.syncTaggedLogsAsSender(secret, contractAddress);

      // Verify the highest finalized index is updated to 3
      expect(await taggingDataProvider.getHighestFinalizedIndex(secret)).toBe(finalizedIndexStep1);
      // Verify the highest used index also returns 3 (when there is no higher pending index the highest used index is
      // the highest finalized index).
      expect(await taggingDataProvider.getHighestUsedIndexAsSender(secret)).toBe(finalizedIndexStep1);
    });

    it('step 2: pending log is synced', async () => {
      pendingTxHash = TxHash.random();

      const finalizedBlockNumber = 15;
      const pendingIndex = 5;

      // Create a log with tag index 5
      const index5Tag = await computeSiloedTagForIndex(pendingIndex);

      aztecNode.getLogsByTags.mockImplementation((tags: Fr[]) => {
        // Return empty arrays for all tags except the one at index 5
        return Promise.resolve(
          tags.map((tag: Fr) => (tag.equals(index5Tag.value) ? [makeLog(pendingTxHash, index5Tag.value)] : [])),
        );
      });

      // Mock getTxReceipt to return a successful but still pending tx
      aztecNode.getTxReceipt.mockResolvedValue({
        status: TxStatus.SUCCESS,
        blockNumber: finalizedBlockNumber + 1,
      } as any);

      aztecNode.getL2Tips.mockResolvedValue({
        finalized: { number: finalizedBlockNumber },
      } as any);

      await pxeOracleInterface.syncTaggedLogsAsSender(secret, contractAddress);

      // Verify the highest finalized index was not updated
      expect(await taggingDataProvider.getHighestFinalizedIndex(secret)).toBe(finalizedIndexStep1);
      // Verify the highest used index was updated to the pending index
      expect(await taggingDataProvider.getHighestUsedIndexAsSender(secret)).toBe(pendingIndex);
    });

    it('step 3: syncs logs across 2 windows', async () => {
      // Move finalized block into the future
      const newFinalizedBlockNumber = 20;

      const newHighestFinalizedIndex = 7;
      const newHighestUsedIndex = 16;

      // Create tx hashes for new logs
      const index7TxHash = TxHash.random();
      const index16TxHash = TxHash.random();

      // Create tags for multiple indices across 2 windows
      const index5Tag = await computeSiloedTagForIndex(5); // Previously pending, now finalized
      const index7Tag = await computeSiloedTagForIndex(newHighestFinalizedIndex); // New finalized log
      const index16Tag = await computeSiloedTagForIndex(newHighestUsedIndex); // New pending log

      // Mock getLogsByTags to return logs for multiple indices
      aztecNode.getLogsByTags.mockImplementation((tags: Fr[]) => {
        return Promise.resolve(
          tags.map((tag: Fr) => {
            if (tag.equals(index5Tag.value)) {
              return [makeLog(pendingTxHash, index5Tag.value)];
            } else if (tag.equals(index7Tag.value)) {
              return [makeLog(index7TxHash, index7Tag.value)];
            } else if (tag.equals(index16Tag.value)) {
              return [makeLog(index16TxHash, index16Tag.value)];
            }
            return [];
          }),
        );
      });

      // Mock getTxReceipt to return appropriate statuses
      aztecNode.getTxReceipt.mockImplementation((hash: TxHash) => {
        if (hash.equals(pendingTxHash)) {
          // The previously pending tx (index 5) is now finalized
          return {
            status: TxStatus.SUCCESS,
            blockNumber: newFinalizedBlockNumber - 3,
          } as any;
        } else if (hash.equals(index7TxHash)) {
          // This tx (index 7) is finalized
          return {
            status: TxStatus.SUCCESS,
            blockNumber: newFinalizedBlockNumber - 2,
          } as any;
        } else if (hash.equals(index16TxHash)) {
          // This tx (index 16) is pending
          return {
            status: TxStatus.SUCCESS,
            blockNumber: newFinalizedBlockNumber + 2,
          } as any;
        } else {
          throw new Error(`Unexpected tx hash: ${hash.toString()}`);
        }
      });

      // Mock getL2Tips with the new finalized block number
      aztecNode.getL2Tips.mockResolvedValue({
        finalized: { number: newFinalizedBlockNumber },
      } as any);

      await pxeOracleInterface.syncTaggedLogsAsSender(secret, contractAddress);

      expect(await taggingDataProvider.getHighestFinalizedIndex(secret)).toBe(newHighestFinalizedIndex);
      expect(await taggingDataProvider.getHighestUsedIndexAsSender(secret)).toBe(newHighestUsedIndex);
    });
  });

  /**
   * This test verifies that when multiple logs use the same tag, we correctly bump the finalized index. With this
   * test we make sure we don't accidentally ignore the duplicate log.
   */
  it('handles pending and finalized logs found at the same index', async () => {
    await setUp();

    const finalizedTxHash = TxHash.random();
    const pendingTxHash = TxHash.random();

    const finalizedBlockNumber = 15;
    const pendingAndFinalizedIndex = 3;

    const index3Tag = await computeSiloedTagForIndex(pendingAndFinalizedIndex);

    aztecNode.getLogsByTags.mockImplementation((tags: Fr[]) => {
      // Return both the pending and finalized logs for the tag at index 3
      return Promise.resolve(
        tags.map((tag: Fr) =>
          tag.equals(index3Tag.value)
            ? [makeLog(pendingTxHash, index3Tag.value), makeLog(finalizedTxHash, index3Tag.value)]
            : [],
        ),
      );
    });

    aztecNode.getTxReceipt.mockImplementation((hash: TxHash) => {
      if (hash.equals(finalizedTxHash)) {
        return {
          status: TxStatus.SUCCESS,
          blockNumber: finalizedBlockNumber - 1, // Finalized tx
        } as any;
      } else if (hash.equals(pendingTxHash)) {
        return {
          status: TxStatus.SUCCESS,
          blockNumber: finalizedBlockNumber + 1, // Pending tx
        } as any;
      } else {
        throw new Error(`Unexpected tx hash: ${hash.toString()}`);
      }
    });

    aztecNode.getL2Tips.mockResolvedValue({
      finalized: { number: finalizedBlockNumber },
    } as any);

    // Sync tagged logs
    await pxeOracleInterface.syncTaggedLogsAsSender(secret, contractAddress);

    // Verify that both highest finalized and highest used were set to the pending and finalized index
    expect(await taggingDataProvider.getHighestFinalizedIndex(secret)).toBe(pendingAndFinalizedIndex);
    expect(await taggingDataProvider.getHighestUsedIndexAsSender(secret)).toBe(pendingAndFinalizedIndex);
  });
});
