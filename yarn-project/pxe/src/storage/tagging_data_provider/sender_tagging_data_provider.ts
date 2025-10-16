import { toArray } from '@aztec/foundation/iterable';
import type { AztecAsyncKVStore, AztecAsyncMap } from '@aztec/kv-store';
import type { DirectionalAppTaggingSecret, PreTag } from '@aztec/stdlib/logs';
import { TxHash } from '@aztec/stdlib/tx';

/**
 * Data provider of tagging data used when syncing the sender tagging indexes. The recipient alternative of this class
 * is called RecipientTaggingDataProvider. We have the providers separate for the sender and recipient because
 * the algorithms are completely disjoint and there is not data reuse between the 2.
 */
export class SenderTaggingDataProvider {
  #store: AztecAsyncKVStore;

  // Stores the pending indexes for each directional app tagging secret. Pending here means that the tx that
  // contained the private logs with tags corresponding to these indexes has not been finalized yet. We don't store
  // just the highest index because it could happen that some of the transactions are dropped and then we need
  // the information about the lower pending indexes. But we store just one index per secret-txHash pair as only
  // the highest index in the given tx is relevant for future index choice when sending a private log.
  #pendingIndexes: AztecAsyncMap<string, { index: number; txHash: string }[]>;

  // Stores the last (highest) finalized index for each directional app tagging secret. We care only about the last
  // index because unlike the pending indexes, it will never happen that a finalized index would be removed and hence
  // we don't need to store the history.
  #lastFinalizedIndexes: AztecAsyncMap<string, number>;

  constructor(store: AztecAsyncKVStore) {
    this.#store = store;

    this.#pendingIndexes = this.#store.openMap('pending_indexes');
    this.#lastFinalizedIndexes = this.#store.openMap('last_finalized_indexes');
  }

  /**
   * Stores pending indexes.
   * @remarks Ignores the index if the same preTag + txHash combination already exists in the db with the same index.
   * This is expected to happen because whenever we start sync we start from the last finalized index and we can have
   * pending indexes already stored from previous syncs.
   * @param preTags - The pre-tags containing the directional app tagging secrets and the indexes that are to be
   * stored in the db.
   * @param txHash - The hash of the pending tx that used the given pre-tags to compute private log tags.
   * @throws If any two pre-tags contain the same directional app tagging secret. This is enforced because we care
   * only about the highest index for a given secret that was used in the tx. Hence this check is a good way to catch
   * bugs.
   * @throws If a secret + txHash pair already exists in the db with a different index value. It should never happen
   * that we would attempt to store a different index for a given secret-txHash pair because we always store just the
   * highest index for a given secret-txHash pair. Hence this is a good way to catch bugs.
   * @throws If the newly stored pending index is lower than or equal to the last finalized index for the same secret.
   * This is enforced because this should never happen if the syncing is done correctly as we look for logs from higher
   * indexes than finalized ones.
   */
  async storePendingIndexes(preTags: PreTag[], txHash: TxHash) {
    // The secrets in pre-tags should be unique because we always store just the highest index per given secret-txHash
    // pair. Below we check that this is the case.
    const secretsSet = new Set(preTags.map(preTag => preTag.secret.toString()));
    if (secretsSet.size !== preTags.length) {
      throw new Error(`Duplicate secrets found when storing pending indexes`);
    }

    for (const { secret, index } of preTags) {
      const secretStr = secret.toString();
      const existing = (await this.#pendingIndexes.getAsync(secretStr)) ?? [];

      // Throw if the new pending index is lower than or equal to the last finalized index
      const lastFinalizedIndex = await this.#lastFinalizedIndexes.getAsync(secretStr);
      if (lastFinalizedIndex !== undefined && index <= lastFinalizedIndex) {
        throw new Error(
          `Cannot store pending index ${index} for secret ${secretStr}: ` +
            `it is lower than or equal to the last finalized index ${lastFinalizedIndex}`,
        );
      }

      // Check if this secret + txHash combination already exists
      const existingEntry = existing.find(entry => entry.txHash === txHash.toString());

      if (existingEntry) {
        // If it exists with a different index, throw an error
        if (existingEntry.index !== index) {
          throw new Error(
            `Cannot store index ${index} for secret ${secretStr} and txHash ${txHash.toString()}: ` +
              `a different index ${existingEntry.index} already exists for this secret-txHash pair`,
          );
        }
        // If it exists with the same index, ignore the update (no-op)
      } else {
        // If it doesn't exist, add it
        await this.#pendingIndexes.set(secretStr, [...existing, { index, txHash: txHash.toString() }]);
      }
    }
  }

  /**
   * Returns the transaction hashes of all pending transactions that contain indexes within a specified range
   * for a given directional app tagging secret.
   * @param secret - The directional app tagging secret to query pending indexes for.
   * @param startIndex - The lower bound of the index range (inclusive).
   * @param endIndex - The upper bound of the index range (exclusive).
   * @returns An array of unique transaction hashes for pending transactions that contain indexes in the range
   * [startIndex, endIndex). Returns an empty array if no pending indexes exist in the range.
   */
  async getTxHashesOfPendingIndexes(
    secret: DirectionalAppTaggingSecret,
    startIndex: number,
    endIndex: number,
  ): Promise<TxHash[]> {
    const secretStr = secret.toString();
    const existing = (await this.#pendingIndexes.getAsync(secretStr)) ?? [];
    const txHashes = existing
      .filter(entry => entry.index >= startIndex && entry.index < endIndex)
      .map(entry => entry.txHash);
    return Array.from(new Set(txHashes)).map(TxHash.fromString);
  }

  /**
   * Returns the last (highest) finalized index for a given secret.
   * @param secret - The secret to get the last finalized index for.
   * @returns The last (highest) finalized index for the given secret.
   */
  getLastFinalizedIndex(secret: DirectionalAppTaggingSecret): Promise<number | undefined> {
    return this.#lastFinalizedIndexes.getAsync(secret.toString());
  }

  /**
   * Returns the last used index for a given directional app tagging secret, considering both finalized and pending
   * indexes.
   * @param secret - The directional app tagging secret to query the last used index for.
   * @returns The last used index.
   */
  async getLastUsedIndex(secret: DirectionalAppTaggingSecret): Promise<number | undefined> {
    const lastFinalizedIndex = await this.#lastFinalizedIndexes.getAsync(secret.toString());
    const pendingTxScopedIndexes = (await this.#pendingIndexes.getAsync(secret.toString())) ?? [];
    const pendingIndexes = pendingTxScopedIndexes.map(entry => entry.index);

    if (pendingTxScopedIndexes.length === 0) {
      return lastFinalizedIndex;
    }

    // As the last used index we return the highest one from the pending indexes. Note that this might not technically
    // be the last used index in case we sent logs from multiple PXEs in parallel but that's just a detail.
    return Math.max(...pendingIndexes);
  }

  /**
   * A tx that contained private logs with tags corresponding to some of the indexes returned from this data provider
   * has been dropped so we delete the corresponding pending indexes. This results in the indexes being reused which
   * result in tx linkability but we do not worry about that for now.
   * @param txHash - The hash of the tx to drop the pending indexes for.
   */
  async dropPendingIndexes(txHash: TxHash) {
    const txHashStr = txHash.toString();
    const allSecrets = await toArray(this.#pendingIndexes.keysAsync());

    for (const secret of allSecrets) {
      const pendingData = await this.#pendingIndexes.getAsync(secret);
      if (pendingData) {
        const filtered = pendingData.filter(item => item.txHash.toString() !== txHashStr);
        if (filtered.length === 0) {
          await this.#pendingIndexes.delete(secret);
        } else {
          await this.#pendingIndexes.set(secret, filtered);
        }
      }
    }
  }

  /**
   * A tx that contained private logs with tags corresponding to some of the indexes returned from this data provider
   * has been finalized so we update the corresponding pending indexes to be finalized (we move them from pending map
   * to finalized map).
   * @param txHash - The hash of the tx to update the status of.
   */
  async updateStatusToFinalized(txHash: TxHash) {
    const txHashStr = txHash.toString();
    const allSecrets = await toArray(this.#pendingIndexes.keysAsync());

    for (const secret of allSecrets) {
      const pendingData = await this.#pendingIndexes.getAsync(secret);
      if (pendingData) {
        const matchingIndexes = pendingData
          .filter(item => item.txHash.toString() === txHashStr)
          .map(item => item.index);
        if (matchingIndexes.length === 1) {
          let currentFinalized = await this.#lastFinalizedIndexes.getAsync(secret);
          const newFinalized = matchingIndexes[0];

          // It could happen that the newly discovered finalized index is smaller than the current one because there
          // might have been other pending txs with a higher finalized index in this round of syncing. For this reason
          // we store the new index only if it's higher than the current one.
          if (newFinalized > (currentFinalized ?? 0)) {
            await this.#lastFinalizedIndexes.set(secret, newFinalized);
            currentFinalized = newFinalized;
          }

          // We prune the no longer necessary pending data.
          const remainingItemsOfHigherIndex = pendingData.filter(item => item.index > (currentFinalized ?? 0));
          if (remainingItemsOfHigherIndex.length === 0) {
            await this.#pendingIndexes.delete(secret);
          } else {
            await this.#pendingIndexes.set(secret, remainingItemsOfHigherIndex);
          }
        } else if (matchingIndexes.length > 1) {
          // We should always just store the highest pending index for a given tx hash and secret because the lower
          // values are irrelevant.
          throw new Error(`Multiple pending indexes found for tx hash ${txHashStr} and secret ${secret}`);
        }
      }
    }
  }
}
