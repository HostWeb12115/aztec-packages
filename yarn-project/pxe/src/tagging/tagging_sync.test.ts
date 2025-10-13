import { Fr } from '@aztec/foundation/fields';
import { KeyStore } from '@aztec/key-store';
import { openTmpStore } from '@aztec/kv-store/lmdb-v2';
import { AztecAddress } from '@aztec/stdlib/aztec-address';
import type { AztecNode } from '@aztec/stdlib/interfaces/client';
import { PrivateLog, TxScopedL2Log } from '@aztec/stdlib/logs';
import { TxHash, TxStatus } from '@aztec/stdlib/tx';

import { jest } from '@jest/globals';
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
    jest.spyOn(contractDataProvider, 'getDebugContractName').mockImplementation(() => Promise.resolve('TestContract'));

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
    // Set up contract address
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

  describe('sequential tests', () => {
    beforeAll(async () => {
      await setUp();
    });

    it('step 1: highest finalized index is updated', async () => {
      // Create a tx hash for the log
      const txHash = TxHash.random();

      const finalizedBlockNumber = 15;

      // Create a log with tag index 3
      const index3Tag = await computeSiloedTagForIndex(3);

      // Mock getLogsByTags to return the log for tag index 3
      aztecNode.getLogsByTags.mockImplementation((tags: Fr[]) => {
        // Return empty arrays for all tags except the one at index 3
        return Promise.resolve(
          tags.map((tag: Fr) => (tag.equals(index3Tag.value) ? [makeLog(txHash, index3Tag.value)] : [])),
        );
      });

      // Mock getTxReceipt to return a successful, finalized tx
      aztecNode.getTxReceipt.mockResolvedValue({
        status: TxStatus.SUCCESS,
        blockNumber: finalizedBlockNumber - 1, // included in a block before the finalized block hence the tx is finalized
      } as any);

      // Mock getL2Tips to return a finalized block number >= the tx block number
      aztecNode.getL2Tips.mockResolvedValue({
        finalized: { number: finalizedBlockNumber },
      } as any);

      // Sync tagged logs
      await pxeOracleInterface.syncTaggedLogsAsSender(secret, contractAddress);

      // Verify the highest finalized index is updated to 3
      expect(await taggingDataProvider.getHighestFinalizedIndex(secret)).toBe(3);
      // Verify the highest used index also returns 3
      expect(await taggingDataProvider.getHighestUsedIndexAsSender(secret)).toBe(3);
    });

    it('step 2: pending log is synced', async () => {
      // Create a tx hash for the log
      const txHash = TxHash.random();

      const finalizedBlockNumber = 15;
      const pendingIndex = 5;

      // Create a log with tag index 3
      const index5Tag = await computeSiloedTagForIndex(pendingIndex);

      // Mock getLogsByTags to return the log for tag index 5
      aztecNode.getLogsByTags.mockImplementation((tags: Fr[]) => {
        // Return empty arrays for all tags except the one at index 3
        return Promise.resolve(
          tags.map((tag: Fr) => (tag.equals(index5Tag.value) ? [makeLog(txHash, index5Tag.value)] : [])),
        );
      });

      // Mock getTxReceipt to return a successful, finalized tx
      aztecNode.getTxReceipt.mockResolvedValue({
        status: TxStatus.SUCCESS,
        blockNumber: finalizedBlockNumber + 1,
      } as any);

      // Mock getL2Tips to return a finalized block number >= the tx block number
      aztecNode.getL2Tips.mockResolvedValue({
        finalized: { number: finalizedBlockNumber },
      } as any);

      // Sync tagged logs
      await pxeOracleInterface.syncTaggedLogsAsSender(secret, contractAddress);

      // Verify the highest finalized index is updated to 3
      expect(await taggingDataProvider.getHighestFinalizedIndex(secret)).toBe(3);
      // Verify the highest used index also returns 3
      expect(await taggingDataProvider.getHighestUsedIndexAsSender(secret)).toBe(pendingIndex);
    });
  });

  /**
   * This test verifies that when multiple logs use the same tag, we correctly identify and handle the finalized log.
   */
  it('handles pending and finalized logs found at the same index', async () => {
    await setUp();

    // Create a tx hash for the log
    const finalizedTxHash = TxHash.random();
    const pendingTxHash = TxHash.random();

    const finalizedBlockNumber = 15;

    // Create a log with tag index 3
    const index3Tag = await computeSiloedTagForIndex(3);

    // Mock getLogsByTags to return the log for tag index 3
    aztecNode.getLogsByTags.mockImplementation((tags: Fr[]) => {
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

    // Verify the highest finalized index is updated to 3
    expect(await taggingDataProvider.getHighestFinalizedIndex(secret)).toBe(3);
    // Verify the highest used index also returns 3
    expect(await taggingDataProvider.getHighestUsedIndexAsSender(secret)).toBe(3);
  });
});
