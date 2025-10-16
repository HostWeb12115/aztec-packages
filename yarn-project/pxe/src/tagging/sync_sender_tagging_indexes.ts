import type { AztecAddress } from '@aztec/stdlib/aztec-address';
import type { AztecNode } from '@aztec/stdlib/interfaces/server';
import type { DirectionalAppTaggingSecret, PreTag, TxScopedL2Log } from '@aztec/stdlib/logs';
import { TxHash, TxStatus } from '@aztec/stdlib/tx';

import type { SenderTaggingDataProvider } from '../storage/tagging_data_provider/sender_tagging_data_provider.js';
import { SiloedTag } from './siloed_tag.js';
import { Tag } from './tag.js';

// This window has to be as large as the largest expected number of logs emitted in a tx for a given directional app
// tagging secret. If we get more tag indexes consumed than this window, an error is thrown in `PXE::proveTx` function.
// This is set to a larger value than MAX_PRIVATE_LOGS_PER_TX (currently 32) because there could be more than
// MAX_PRIVATE_LOGS_PER_TX indexes consumed in case the logs are squashed. This happens when the log contains a note
// and the note is nullified in the same tx.
// Note: Set it to 95 because `e2e_pending_note_hashes_contract` test hit 95 indexes emitted and 100 makes
// `docs_examples.test.ts` fail as our JSON RPC doesn't seem to allow array with length 100. I (benesjan) think this
// window length should be fine.
export const WINDOW_LEN = 95;

/**
 * Syncs the highest finalized tagging index and pending tagging indexes for a given secret.
 * @param secret - The secret that's unique for (sender, recipient, contract) tuple while the direction of
 * sender -> recipient matters.
 * @param app - The address of the contract that the logs are tagged for. Needs to be provided because we perform
 * second round of siloing in this function which is necessary because kernels do it as well (they silo first field
 * of the private log which corresponds to the tag).
 * @remarks When syncing the indexes as sender we don't care about the log contents - we only care about the highest
 * pending and highest finalized indexes as that guides the next index choice when sending a log. The next index choice
 * is simply the highest pending index plus one (or finalized if pending is undefined).
 */
export async function syncSenderTaggingIndexes(
  secret: DirectionalAppTaggingSecret,
  app: AztecAddress,
  aztecNode: AztecNode,
  taggingDataProvider: SenderTaggingDataProvider,
): Promise<void> {
  const finalizedIndex = await taggingDataProvider.getLastFinalizedIndex(secret);

  let start = finalizedIndex === undefined ? 0 : finalizedIndex + 1;
  let end = start + WINDOW_LEN;

  let previousFinalizedIndex = finalizedIndex;
  let newFinalizedIndex = undefined;

  while (true) {
    // Load and store indexes for the current window. These indexes may already exist in the database if txs using
    // them were previously sent from this PXE. Any duplicates are handled by the tagging data provider.
    await loadAndStoreNewTaggingIndexes(secret, app, start, end, aztecNode, taggingDataProvider);

    // Retrieve all indexes within the current window from storage. For each secret-txHash pair, we only store the
    // highest index in the data provider. This means we may not get a transaction hash even if some of its indexes
    // fall within the window (specifically when 'end' is greater than those indexes). This is fine as those tx hashes
    // will simply be processed in subsequent iterations.
    const pendingTxHashes = await taggingDataProvider.getTxHashesOfPendingIndexes(secret, start, end);
    if (pendingTxHashes.length === 0) {
      break;
    }

    // Get receipts for all pending tx hashes and the finalized block number.
    const [receipts, { finalized }] = await Promise.all([
      Promise.all(pendingTxHashes.map(pendingTxHash => aztecNode.getTxReceipt(pendingTxHash))),
      aztecNode.getL2Tips(),
    ]);

    for (let i = 0; i < receipts.length; i++) {
      const receipt = receipts[i];
      const txHash = pendingTxHashes[i];

      if (receipt.status === TxStatus.SUCCESS && receipt.blockNumber && receipt.blockNumber <= finalized.number) {
        // Tx has been included in a block and the corresponding block is finalized --> we mark the indexes as
        // finalized.
        await taggingDataProvider.updateStatusToFinalized(txHash);
      } else if (
        receipt.status === TxStatus.DROPPED ||
        receipt.status === TxStatus.APP_LOGIC_REVERTED ||
        receipt.status === TxStatus.TEARDOWN_REVERTED ||
        receipt.status === TxStatus.BOTH_REVERTED
      ) {
        // Tx was dropped or reverted --> we drop the corresponding pending indexes.
        // TODO(#17615): Don't drop pending indexes corresponding to non-revertible phases.
        await taggingDataProvider.dropPendingIndexes(txHash);
      } else {
        // Tx is still pending or the corresponding block is not yet finalized --> we don't do anything.
      }
    }

    // We check if the finalized index has been updated.
    newFinalizedIndex = await taggingDataProvider.getLastFinalizedIndex(secret);
    if (previousFinalizedIndex !== newFinalizedIndex) {
      // A new finalized index was found, so we'll run the loop again. For example:
      // - Previous finalized index: 10
      // - New finalized index: 13
      // - Window length: 10
      //
      // In the last iteration, we processed indexes 11-20. To avoid reprocessing the same logs,
      // we'll only look at the new indexes 21-23:
      //
      //    Previous window: [11, 12, 13, 14, 15, 16, 17, 18, 19, 20]
      //    New window:                                             [21, 22, 23]

      const previousEnd = end;
      end = newFinalizedIndex! + 1 + WINDOW_LEN;
      start = previousEnd;
      previousFinalizedIndex = newFinalizedIndex;
    } else {
      break;
    }
  }
}

async function loadAndStoreNewTaggingIndexes(
  secret: DirectionalAppTaggingSecret,
  app: AztecAddress,
  start: number,
  end: number,
  aztecNode: AztecNode,
  taggingDataProvider: SenderTaggingDataProvider,
) {
  // We compute the tags for the current window of indexes
  const preTagsForWindow: PreTag[] = Array(end - start + 1)
    .fill(0)
    .map((_, i) => ({ secret, index: start + i }));
  const siloedTagsForWindow = await Promise.all(
    preTagsForWindow.map(async preTag => SiloedTag.compute(await Tag.compute(preTag), app)),
  );

  const possibleLogs = await getPrivateLogsByTags(siloedTagsForWindow, aztecNode);

  // Now we want to find the highest index for a given [secret, txHash] pair.
  // txHash -> highest found index
  const highestIndexMap = new Map<string, number>();

  for (let i = 0; i < possibleLogs.length; i++) {
    const taggingIndex = preTagsForWindow[i].index;
    // There can be multiple logs for a given tag because it can happen that tags are reused (e.g. when sending a tx
    // from multiple wallets at the same time).
    const logsForTag = possibleLogs[i];

    for (const txScopedLog of logsForTag) {
      const key = txScopedLog.txHash.toString();
      highestIndexMap.set(key, Math.max(highestIndexMap.get(key) ?? 0, taggingIndex));
    }
  }

  // Now we iterate over the map, reconstruct the preTags and tx hash and store them in the db.
  for (const [txHashStr, highestIndex] of highestIndexMap.entries()) {
    const txHash = TxHash.fromString(txHashStr);
    await taggingDataProvider.storePendingIndexes([{ secret, index: highestIndex }], txHash);
  }
}

// TODO(#12656): Make this a public function on the AztecNode interface and remove the original getLogsByTags. This
// was not done yet as we were unsure about the API and we didn't want to introduce a breaking change.
async function getPrivateLogsByTags(tags: SiloedTag[], aztecNode: AztecNode): Promise<TxScopedL2Log[][]> {
  const tagsAsFr = tags.map(tag => tag.value);
  const allLogs = await aztecNode.getLogsByTags(tagsAsFr);
  return allLogs.map(logs => logs.filter(log => !log.isFromPublic));
}
