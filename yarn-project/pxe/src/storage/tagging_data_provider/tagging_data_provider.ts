import { toArray } from '@aztec/foundation/iterable';
import type { AztecAsyncKVStore, AztecAsyncMap } from '@aztec/kv-store';
import { AztecAddress } from '@aztec/stdlib/aztec-address';
import type { DirectionalAppTaggingSecret, PreTag } from '@aztec/stdlib/logs';
import type { TxHash } from '@aztec/stdlib/tx';

export class TaggingDataProvider {
  #store: AztecAsyncKVStore;
  #addressBook: AztecAsyncMap<string, true>;

  // Stores the last used index for each directional app tagging secret. Taking into account whether we are
  // requesting the index as a sender or as a recipient because the sender and recipient can be in the same PXE.
  #lastPendingIndexesAsSenders: AztecAsyncMap<string, { index: number; txHash: TxHash }>;
  #lastFinalizedIndexesAsSenders: AztecAsyncMap<string, number>;
  #lastUsedIndexesAsRecipients: AztecAsyncMap<string, number>;

  // Stores pending tx hashes for each directional app tagging secret.
  #pendingTxHashes: AztecAsyncMap<string, TxHash[]>;

  constructor(store: AztecAsyncKVStore) {
    this.#store = store;

    this.#addressBook = this.#store.openMap('address_book');

    this.#lastPendingIndexesAsSenders = this.#store.openMap('last_pending_indexes_as_senders');
    this.#lastFinalizedIndexesAsSenders = this.#store.openMap('last_finalized_indexes_as_senders');
    this.#lastUsedIndexesAsRecipients = this.#store.openMap('last_used_indexes_as_recipients');

    this.#pendingTxHashes = this.#store.openMap('pending_tx_hashes');
  }

  /**
   * Sets the last pending indexes when sending a log.
   * @param preTags - The pre tags containing the directional app tagging secrets and the indexes that are to be
   * updated in the db.
   * @param txHash - The hash of the pending tx that use the given pre tags to compute private log tags.
   * @throws If any two pre tags contain the same directional app tagging secret
   */
  setLastPendingIndexesAsSender(preTags: PreTag[], txHash: TxHash) {
    this.#assertUniqueSecrets(preTags, 'sender');

    return Promise.all(
      preTags.map(({ secret, index }) => this.#lastPendingIndexesAsSenders.set(secret.toString(), { index, txHash })),
    );
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
        const prevIndex = await this.#lastFinalizedIndexesAsSenders.getAsync(secretStr);
        if (prevIndex !== undefined && index <= prevIndex) {
          throw new Error(`New finalized tagging index ${index} must be larger than previous index ${prevIndex}`);
        }
        return this.#lastFinalizedIndexesAsSenders.set(secretStr, index);
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

  async getPendingTxHashes(secret: DirectionalAppTaggingSecret): Promise<TxHash[]> {
    return (await this.#pendingTxHashes.getAsync(secret.toString())) ?? [];
  }

  /**
   * Returns the last pending index when sending a log with a given secret.
   * @param secret - The directional app tagging secret.
   * @returns The last pending index for the given directional app tagging secret, or undefined if not found.
   */
  async getScopedPendingIndexesAsSender(secret: DirectionalAppTaggingSecret): Promise<number | undefined> {}

  /**
   * Returns the last finalized index when sending a log with a given secret.
   * @param secret - The directional app tagging secret.
   * @returns The last finalized index for the given directional app tagging secret, or undefined if not found.
   */
  async getLastFinalizedIndexAsSender(secret: DirectionalAppTaggingSecret): Promise<number | undefined> {}

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
      const keysForSendersPending = await toArray(this.#lastPendingIndexesAsSenders.keysAsync());
      await Promise.all(keysForSendersPending.map(secret => this.#lastPendingIndexesAsSenders.delete(secret)));
      const keysForSendersFinalized = await toArray(this.#lastFinalizedIndexesAsSenders.keysAsync());
      await Promise.all(keysForSendersFinalized.map(secret => this.#lastFinalizedIndexesAsSenders.delete(secret)));
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
