import type { AztecAddress } from '@aztec/stdlib/aztec-address';
import type { AztecNode } from '@aztec/stdlib/interfaces/server';
import type { DirectionalAppTaggingSecret, PreTag, TxScopedL2Log } from '@aztec/stdlib/logs';
import { TxHash } from '@aztec/stdlib/tx';

import type { SenderTaggingDataProvider } from '../../storage/tagging_data_provider/sender_tagging_data_provider.js';
import { SiloedTag } from '../siloed_tag.js';
import { Tag } from '../tag.js';

/**
 * Loads tagging indexes from the Aztec node and stores them in the tagging data provider.
 * @remarks This function is one of two places by which a pending index can get to the tagging data provider. The other
 * place is when a tx is being sent from this PXE.
 * @param secret - The directional app tagging secret that's unique for (sender, recipient, contract) tuple.
 * @param app - The address of the contract that the logs are tagged for. Used for siloing tags to match
 * kernel circuit behavior.
 * @param start - The starting index (inclusive) of the window to process.
 * @param end - The ending index (exclusive) of the window to process.
 * @param aztecNode - The Aztec node instance to query for logs.
 * @param taggingDataProvider - The data provider to store pending indexes.
 */
export async function loadAndStoreNewTaggingIndexes(
  secret: DirectionalAppTaggingSecret,
  app: AztecAddress,
  start: number,
  end: number,
  aztecNode: AztecNode,
  taggingDataProvider: SenderTaggingDataProvider,
) {
  // We compute the tags for the current window of indexes
  const preTagsForWindow: PreTag[] = Array(end - start)
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
