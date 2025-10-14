import { toArray } from '@aztec/foundation/iterable';
import type { AztecAsyncKVStore, AztecAsyncMap } from '@aztec/kv-store';
import { AztecAddress } from '@aztec/stdlib/aztec-address';
import type { DirectionalAppTaggingSecret, PreTag } from '@aztec/stdlib/logs';

/**
 * Data provider of tagging data used when syncing the logs as a recipient. The sender alternative of this class is
 * called SenderTaggingDataProvider. We have the providers separate for the sender and recipient because
 * the algorithms are completely disjoint and there is not data reuse between the 2.
 */
export class RecipientTaggingDataProvider {
  #store: AztecAsyncKVStore;
  #addressBook: AztecAsyncMap<string, true>;

  // TODO(benesjan): document and rename
  #lastUsedIndexes: AztecAsyncMap<string, number>;

  constructor(store: AztecAsyncKVStore) {
    this.#store = store;

    this.#addressBook = this.#store.openMap('address_book');
    this.#lastUsedIndexes = this.#store.openMap('last_used_indexes');
  }

  /**
   * Sets the last used indexes when looking for logs.
   * @param preTags - The pre tags containing the directional app tagging secrets and the indexes that are to be
   * updated in the db.
   * @throws If any two pre tags contain the same directional app tagging secret
   */
  setLastUsedIndexes(preTags: PreTag[]) {
    this.#assertUniqueSecrets(preTags);

    return Promise.all(preTags.map(({ secret, index }) => this.#lastUsedIndexes.set(secret.toString(), index)));
  }

  // It should never happen that we would receive any two pre tags on the input containing the same directional app
  // tagging secret as everywhere we always just apply the largest index. Hence this check is a good way to catch
  // bugs.
  #assertUniqueSecrets(preTags: PreTag[]): void {
    const secretStrings = preTags.map(({ secret }) => secret.toString());
    const uniqueSecrets = new Set(secretStrings);
    if (uniqueSecrets.size !== secretStrings.length) {
      throw new Error(`Duplicate secrets found when setting last used indexes as recipient`);
    }
  }

  /**
   * Returns the last used indexes when looking for logs.
   * @param secrets - The directional app tagging secrets to obtain the indexes for.
   * @returns The last used indexes for the given directional app tagging secrets, or undefined if have never yet found
   * a log for a given secret.
   */
  getLastUsedIndexes(secrets: DirectionalAppTaggingSecret[]): Promise<(number | undefined)[]> {
    return Promise.all(secrets.map(secret => this.#lastUsedIndexes.getAsync(secret.toString())));
  }

  resetNoteSyncData(): Promise<void> {
    return this.#store.transactionAsync(async () => {
      const keysForRecipients = await toArray(this.#lastUsedIndexes.keysAsync());
      await Promise.all(keysForRecipients.map(secret => this.#lastUsedIndexes.delete(secret)));
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
}
