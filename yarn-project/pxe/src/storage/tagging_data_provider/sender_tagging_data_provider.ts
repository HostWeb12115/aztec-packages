import { toArray } from '@aztec/foundation/iterable';
import type { AztecAsyncKVStore, AztecAsyncMap } from '@aztec/kv-store';
import type { DirectionalAppTaggingSecret, PreTag } from '@aztec/stdlib/logs';
import { TxHash } from '@aztec/stdlib/tx';

/**
 * Data provider of tagging data used when syncing the sender tagging indexes. The recipient alternative of this class is
 * called RecipientTaggingDataProvider. We have the providers separate for the sender and recipient because
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

  // Stores the highest finalized index for each directional app tagging secret. We care only about the highest index
  // because unlike the pending indexes, it will never happen that a finalized index would be removed and hence we
  // don't need to store the history.
  #highestFinalizedIndexes: AztecAsyncMap<string, number>;

  constructor(store: AztecAsyncKVStore) {
    this.#store = store;

    this.#pendingIndexes = this.#store.openMap('pending_indexes');
    this.#highestFinalizedIndexes = this.#store.openMap('highest_finalized_indexes');
  }

  /**
   * Updates pending indexes as sender when sending a log. Ignores the update if the same preTag + txHash combination
   * already exists.
   * @param preTags - The pre tags containing the directional app tagging secrets and the indexes that are to be
   * updated in the db.
   * @param txHash - The hash of the pending tx that use the given pre tags to compute private log tags.
   * @throws If any two pre tags contain the same directional app tagging secret. This is enforced because we care
   * only about the highest index for a given secret that was used in the tx. Hence this check is a good way to catch
   * bugs.
   */
  async updatePendingIndexes(preTags: PreTag[], txHash: TxHash) {
    this.#assertUniqueSecrets(preTags);

    for (const { secret, index } of preTags) {
      const secretStr = secret.toString();
      const existing = (await this.#pendingIndexes.getAsync(secretStr)) ?? [];

      // Check if this exact preTag + txHash combination already exists
      const alreadyExists = existing.some(entry => entry.index === index && entry.txHash === txHash.toString());

      if (!alreadyExists) {
        await this.#pendingIndexes.set(secretStr, [...existing, { index, txHash: txHash.toString() }]);
      }
    }
  }

  async getTxHashesOfPendingIndexesForRangeForSecret(
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
   * Sets the last finalized indexes when sending a log.
   * @param preTags - The pre tags containing the directional app tagging secrets and the indexes that are to be
   * updated in the db.
   * @throws If any two pre tags contain the same directional app tagging secret
   * @throws If any index is smaller than or equal to the previously stored index
   */
  async setLastFinalizedIndexes(preTags: PreTag[]) {
    this.#assertUniqueSecrets(preTags);

    await Promise.all(
      preTags.map(async ({ secret, index }) => {
        const secretStr = secret.toString();
        const prevIndex = await this.#highestFinalizedIndexes.getAsync(secretStr);
        if (prevIndex !== undefined && index <= prevIndex) {
          throw new Error(`New finalized tagging index ${index} must be larger than previous index ${prevIndex}`);
        }
        return this.#highestFinalizedIndexes.set(secretStr, index);
      }),
    );
  }

  // It should never happen that we would receive any two pre tags on the input containing the same directional app
  // tagging secret as everywhere we always just apply the largest index. Hence this check is a good way to catch
  // bugs.
  #assertUniqueSecrets(preTags: PreTag[]): void {
    const secretStrings = preTags.map(({ secret }) => secret.toString());
    const uniqueSecrets = new Set(secretStrings);
    if (uniqueSecrets.size !== secretStrings.length) {
      throw new Error(`Duplicate secrets found when setting last used indexes as sender`);
    }
  }

  /**
   * Returns the highest finalized index for a given secret.
   * @param secret - The secret to get the highest finalized index for.
   * @returns The highest seen finalized index for the given secret.
   */
  getHighestFinalizedIndex(secret: DirectionalAppTaggingSecret): Promise<number | undefined> {
    return this.#highestFinalizedIndexes.getAsync(secret.toString());
  }

  async getHighestUsedIndex(secret: DirectionalAppTaggingSecret): Promise<number | undefined> {
    const highestFinalizedIndex = await this.#highestFinalizedIndexes.getAsync(secret.toString());
    const pendingTxScopedIndexes = (await this.#pendingIndexes.getAsync(secret.toString())) ?? [];
    const pendingIndexes = pendingTxScopedIndexes.map(entry => entry.index);

    if (pendingTxScopedIndexes.length === 0) {
      return highestFinalizedIndex;
    }

    const highestPendingIndex = Math.max(...pendingIndexes);
    if (highestFinalizedIndex !== undefined && highestPendingIndex <= highestFinalizedIndex) {
      throw new Error(
        `Highest pending index ${highestPendingIndex} is lower than or equal to highest finalized index ${highestFinalizedIndex}. This is a bug and should never happen!`,
      );
    }

    return highestPendingIndex;
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
          // It could happen that the newly discovered finalized index is smaller than the current one because there
          // might have been other pending tx with a higher finalized index in this round of syncing. For this reason
          // we store the higher one.
          const currentFinalized = await this.#highestFinalizedIndexes.getAsync(secret);
          const newFinalized = Math.max(currentFinalized ?? 0, matchingIndexes[0]);
          await this.#highestFinalizedIndexes.set(secret, newFinalized);

          // We store the remaining items with a higher index in pending.
          const remainingItems = pendingData.filter(item => item.txHash.toString() !== txHashStr);
          const remainingItemsOfHigherIndex = remainingItems.filter(item => item.index > newFinalized);

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
