import { toArray } from '@aztec/foundation/iterable';
import type { AztecAsyncKVStore, AztecAsyncMap } from '@aztec/kv-store';
import { AztecAddress } from '@aztec/stdlib/aztec-address';
import type { DirectionalAppTaggingSecret, PreTag } from '@aztec/stdlib/logs';
import type { TxHash } from '@aztec/stdlib/tx';

export class TaggingDataProvider {
  #store: AztecAsyncKVStore;
  #addressBook: AztecAsyncMap<string, true>;

  // The following maps take into account whether we are requesting the index as a sender or as a recipient because
  // the sender and recipient can be in the same PXE.

  // Stores all the pending indexes for each directional app tagging secret. Pending here means that the tx that
  // contained the private logs with tags corresponding to these indexes has not been finalized yet.
  #pendingIndexesAsSenders: AztecAsyncMap<string, { index: number; txHash: TxHash }[]>;

  // Stores the highest finalized index for each directional app tagging secret. We care only about the highest index
  // because unlike the pending indexes, it will never happen that a finalized index would be removed and hence we
  // don't need to store the history.
  #highestFinalizedIndexesAsSenders: AztecAsyncMap<string, number>;

  // TODO(benesjan): document and rename
  #lastUsedIndexesAsRecipients: AztecAsyncMap<string, number>;

  constructor(store: AztecAsyncKVStore) {
    this.#store = store;

    this.#addressBook = this.#store.openMap('address_book');

    this.#pendingIndexesAsSenders = this.#store.openMap('pending_indexes_as_senders');
    this.#highestFinalizedIndexesAsSenders = this.#store.openMap('highest_finalized_indexes_as_senders');
    this.#lastUsedIndexesAsRecipients = this.#store.openMap('last_used_indexes_as_recipients');
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
  async updatePendingIndexesAsSender(preTags: PreTag[], txHash: TxHash) {
    this.#assertUniqueSecrets(preTags, 'sender');

    for (const { secret, index } of preTags) {
      const secretStr = secret.toString();
      const existing = (await this.#pendingIndexesAsSenders.getAsync(secretStr)) ?? [];

      // Check if this exact preTag + txHash combination already exists
      const alreadyExists = existing.some(entry => entry.index === index && entry.txHash.equals(txHash));

      if (!alreadyExists) {
        await this.#pendingIndexesAsSenders.set(secretStr, [...existing, { index, txHash }]);
      }
    }
  }

  async getTxHashesOfPendingIndexesForRangeForSecretAsSender(
    secret: DirectionalAppTaggingSecret,
    startIndex: number,
    endIndex: number,
  ): Promise<TxHash[]> {
    const secretStr = secret.toString();
    const existing = (await this.#pendingIndexesAsSenders.getAsync(secretStr)) ?? [];
    const txHashes = existing
      .filter(entry => entry.index >= startIndex && entry.index < endIndex)
      .map(entry => entry.txHash);
    return Array.from(new Set(txHashes));
  }

  /**
   * Sets the last finalized indexes when sending a log.
   * @param preTags - The pre tags containing the directional app tagging secrets and the indexes that are to be
   * updated in the db.
   * @throws If any two pre tags contain the same directional app tagging secret
   * @throws If any index is smaller than or equal to the previously stored index
   */
  async setLastFinalizedIndexesAsSender(preTags: PreTag[]) {
    this.#assertUniqueSecrets(preTags, 'sender');

    await Promise.all(
      preTags.map(async ({ secret, index }) => {
        const secretStr = secret.toString();
        const prevIndex = await this.#highestFinalizedIndexesAsSenders.getAsync(secretStr);
        if (prevIndex !== undefined && index <= prevIndex) {
          throw new Error(`New finalized tagging index ${index} must be larger than previous index ${prevIndex}`);
        }
        return this.#highestFinalizedIndexesAsSenders.set(secretStr, index);
      }),
    );
  }

  /**
   * Sets the last used indexes when looking for logs.
   * @param preTags - The pre tags containing the directional app tagging secrets and the indexes that are to be
   * updated in the db.
   * @throws If any two pre tags contain the same directional app tagging secret
   */
  setLastUsedIndexesAsRecipient(preTags: PreTag[]) {
    this.#assertUniqueSecrets(preTags, 'recipient');

    return Promise.all(
      preTags.map(({ secret, index }) => this.#lastUsedIndexesAsRecipients.set(secret.toString(), index)),
    );
  }

  // It should never happen that we would receive any two pre tags on the input containing the same directional app
  // tagging secret as everywhere we always just apply the largest index. Hence this check is a good way to catch
  // bugs.
  #assertUniqueSecrets(preTags: PreTag[], role: 'sender' | 'recipient'): void {
    const secretStrings = preTags.map(({ secret }) => secret.toString());
    const uniqueSecrets = new Set(secretStrings);
    if (uniqueSecrets.size !== secretStrings.length) {
      throw new Error(`Duplicate secrets found when setting last used indexes as ${role}`);
    }
  }

  /**
   * Returns the highest finalized index for a given secret.
   * @param secret - The secret to get the highest finalized index for.
   * @returns The highest seen finalized index for the given secret.
   */
  getHighestFinalizedIndex(secret: DirectionalAppTaggingSecret): Promise<number | undefined> {
    return this.#highestFinalizedIndexesAsSenders.getAsync(secret.toString());
  }

  async getHighestUsedIndexAsSender(secret: DirectionalAppTaggingSecret): Promise<number | undefined> {
    const highestFinalizedIndex = await this.#highestFinalizedIndexesAsSenders.getAsync(secret.toString());
    const pendingTxScopedIndexes = (await this.#pendingIndexesAsSenders.getAsync(secret.toString())) ?? [];
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
    const allSecrets = await toArray(this.#pendingIndexesAsSenders.keysAsync());

    for (const secret of allSecrets) {
      const pendingData = await this.#pendingIndexesAsSenders.getAsync(secret);
      if (pendingData) {
        const filtered = pendingData.filter(item => item.txHash.toString() !== txHashStr);
        if (filtered.length === 0) {
          await this.#pendingIndexesAsSenders.delete(secret);
        } else {
          await this.#pendingIndexesAsSenders.set(secret, filtered);
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
    const allSecrets = await toArray(this.#pendingIndexesAsSenders.keysAsync());

    for (const secret of allSecrets) {
      const pendingData = await this.#pendingIndexesAsSenders.getAsync(secret);
      if (pendingData) {
        const matchingIndexes = pendingData
          .filter(item => item.txHash.toString() === txHashStr)
          .map(item => item.index);
        if (matchingIndexes.length === 1) {
          // It could happen that the newly discovered finalized index is smaller than the current one because there
          // might have been other pending tx with a higher finalized index in this round of syncing. For this reason
          // we store the higher one.
          const currentFinalized = await this.#highestFinalizedIndexesAsSenders.getAsync(secret);
          const newFinalized = Math.max(currentFinalized ?? 0, matchingIndexes[0]);
          await this.#highestFinalizedIndexesAsSenders.set(secret, newFinalized);

          // We store the remaining items with a higher index in pending.
          const remainingItems = pendingData.filter(item => item.txHash.toString() !== txHashStr);
          const remainingItemsOfHigherIndex = remainingItems.filter(item => item.index > newFinalized);

          if (remainingItemsOfHigherIndex.length === 0) {
            await this.#pendingIndexesAsSenders.delete(secret);
          } else {
            await this.#pendingIndexesAsSenders.set(secret, remainingItemsOfHigherIndex);
          }
        } else if (matchingIndexes.length > 1) {
          // We should always just store the highest pending index for a given tx hash and secret because the lower
          // values are irrelevant.
          throw new Error(`Multiple pending indexes found for tx hash ${txHashStr} and secret ${secret}`);
        }
      }
    }
  }

  /**
   * Returns the last used indexes when looking for logs as a recipient.
   * @param secrets - The directional app tagging secrets to obtain the indexes for.
   * @returns The last used indexes for the given directional app tagging secrets, or undefined if have never yet found
   * a log for a given secret.
   */
  getLastUsedIndexesAsRecipient(secrets: DirectionalAppTaggingSecret[]): Promise<(number | undefined)[]> {
    return Promise.all(secrets.map(secret => this.#lastUsedIndexesAsRecipients.getAsync(secret.toString())));
  }

  resetNoteSyncData(): Promise<void> {
    return this.#store.transactionAsync(async () => {
      const keysForSendersPending = await toArray(this.#pendingIndexesAsSenders.keysAsync());
      await Promise.all(keysForSendersPending.map(secret => this.#pendingIndexesAsSenders.delete(secret)));
      const keysForSendersFinalized = await toArray(this.#highestFinalizedIndexesAsSenders.keysAsync());
      await Promise.all(keysForSendersFinalized.map(secret => this.#highestFinalizedIndexesAsSenders.delete(secret)));
      const keysForRecipients = await toArray(this.#lastUsedIndexesAsRecipients.keysAsync());
      await Promise.all(keysForRecipients.map(secret => this.#lastUsedIndexesAsRecipients.delete(secret)));
    });
  }

  async addSenderAddress(address: AztecAddress): Promise<boolean> {
    if (await this.#addressBook.hasAsync(address.toString())) {
      return false;
    }

    await this.#addressBook.set(address.toString(), true);

    return true;
  }

  async getSenderAddresses(): Promise<AztecAddress[]> {
    return (await toArray(this.#addressBook.keysAsync())).map(AztecAddress.fromString);
  }

  async removeSenderAddress(address: AztecAddress): Promise<boolean> {
    if (!(await this.#addressBook.hasAsync(address.toString()))) {
      return false;
    }

    await this.#addressBook.delete(address.toString());

    return true;
  }

  async getSize() {
    const addressesCount = (await toArray(this.#addressBook.keysAsync())).length;
    // All keys are addresses
    return 3 * addressesCount * AztecAddress.SIZE_IN_BYTES;
  }
}
