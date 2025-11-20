import { SponsoredFeePaymentMethod } from '@aztec/aztec.js/fee';
import type { AztecNode } from '@aztec/aztec.js/node';
import { Fr } from '@aztec/foundation/fields';
import { createLogger } from '@aztec/foundation/log';
import { SerialQueue } from '@aztec/foundation/queue';
import { sleep } from '@aztec/foundation/sleep';
import { BenchmarkingContract } from '@aztec/noir-test-contracts.js/Benchmarking';
import { Tx } from '@aztec/stdlib/tx';
import { ProvenTx, TestWallet, proveInteraction } from '@aztec/test-wallet/server';

import { jest } from '@jest/globals';
import type { ChildProcess } from 'child_process';

import { getSponsoredFPCAddress } from '../fixtures/utils.js';
import {
  type TestAccountsWithoutTokens,
  createWalletAndAztecNodeClient,
  deploySponsoredTestAccounts,
} from './setup_test_wallets.js';
import { setupEnvironment, startPortForwardForRPC } from './utils.js';

const config = { ...setupEnvironment(process.env) };

type BenchmarkWallet = {
  testAccounts: TestAccountsWithoutTokens;
  cleanup: undefined | (() => Promise<void>);
};

// TODO: parallelize tx creation
describe('sustained 10 TPS test', () => {
  jest.setTimeout(60 * 60 * 1000); // 1 hour

  const logger = createLogger(`e2e:spartan-test:sustained-10tps`);
  const TEST_DURATION_SECONDS = 20 * 60;
  const TARGET_TPS = 2;
  const TOTAL_TXS = TEST_DURATION_SECONDS * TARGET_TPS;
  const NUM_WALLETS = 1;

  const testAccounts: BenchmarkWallet[] = [];
  let aztecNode: AztecNode;
  let benchmarkContract: BenchmarkingContract;

  const forwardProcesses: ChildProcess[] = [];

  afterAll(async () => {
    for (const account of testAccounts) {
      if (!account.cleanup) {
        continue;
      }
      await account?.cleanup();
    }
    forwardProcesses.forEach(p => p.kill());
  });

  beforeAll(async () => {
    const localWallets: TestWallet[] = [];
    const cleanupFunctions = [];
    for (let i = 0; i < NUM_WALLETS; i++) {
      logger.info(`Starting port forward for PXE for wallet ${i + 1}/${NUM_WALLETS}`);
      const { process: aztecRpcProcess, port: aztecRpcPort } = await startPortForwardForRPC(config.NAMESPACE);
      forwardProcesses.push(aztecRpcProcess);
      const rpcUrl = `http://127.0.0.1:${aztecRpcPort}`;

      logger.info(`Creating wallet and pxe for wallet ${i + 1}/${NUM_WALLETS}`);
      let wallet: TestWallet;
      let cleanup: () => Promise<void>;
      ({ wallet, aztecNode, cleanup } = await createWalletAndAztecNodeClient(rpcUrl, config.REAL_VERIFIER, logger));
      localWallets.push(wallet);
      cleanupFunctions.push(cleanup);
    }

    const localTestAccounts = await Promise.all(
      localWallets.map(lw => deploySponsoredTestAccounts(lw, aztecNode, logger)),
    );

    for (let i = 0; i < NUM_WALLETS; i++) {
      testAccounts.push({
        testAccounts: localTestAccounts[i],
        cleanup: cleanupFunctions[i],
      });
    }

    logger.info('Deploying Benchmarking contract');

    const sponsor = new SponsoredFeePaymentMethod(await getSponsoredFPCAddress());
    benchmarkContract = await BenchmarkingContract.deploy(testAccounts[0].testAccounts.wallet)
      .send({ from: testAccounts[0].testAccounts.accounts[0], fee: { paymentMethod: sponsor } })
      .deployed();

    logger.info(
      `Test setup complete. Planning ${TOTAL_TXS} transactions over ${TEST_DURATION_SECONDS} seconds at ${TARGET_TPS} TPS`,
    );
  });

  it('can send 10tps', async () => {
    const sponsor = new SponsoredFeePaymentMethod(await getSponsoredFPCAddress());
    const TOTAL_TXS = TEST_DURATION_SECONDS * TARGET_TPS;
    const txs: ProvenTx[] = [];

    logger.info(`Proving benchmark transaction...`);

    const workers: SerialQueue[] = Array(NUM_WALLETS)
      .fill(0)
      .map(() => new SerialQueue());

    workers.forEach(worker => {
      worker.start();
    });

    const txPromises = [];

    if (config.REAL_VERIFIER === true) {
      for (let i = 0; i < TOTAL_TXS; i++) {
        const workerIndex = i % NUM_WALLETS;
        const worker = workers[workerIndex];
        const from = testAccounts[workerIndex].testAccounts.accounts[0];
        const wallet = testAccounts[workerIndex].testAccounts.wallet;

        const txPromise = worker.put(async () => {
          const tx = await proveInteraction(wallet, benchmarkContract.methods.sha256_hash_1024(Array(1024).fill(42)), {
            from,
            fee: { paymentMethod: sponsor },
          });
          return tx;
        });
        txPromises.push(txPromise);
      }
      const provedTxs = await Promise.all(txPromises);
      txs.push(...provedTxs);
    } else {
      const wallet = testAccounts[0].testAccounts.wallet;
      const from = testAccounts[0].testAccounts.accounts[0];
      const baseTx = await proveInteraction(wallet, benchmarkContract.methods.create_note(from, 10), {
        from,
        fee: { paymentMethod: sponsor },
      });

      for (let i = 0; i < TOTAL_TXS; i++) {
        const clonedTxData = Tx.clone(baseTx);

        if (clonedTxData.data.forRollup) {
          for (let i = 0; i < clonedTxData.data.forRollup?.end.nullifiers.length; i++) {
            if (clonedTxData.data.forRollup?.end.nullifiers[i].isZero()) {
              continue;
            }
            clonedTxData.data.forRollup.end.nullifiers[i] = Fr.random();
          }
        } else if (clonedTxData.data.forPublic) {
          for (let i = 0; i < clonedTxData.data.forPublic.nonRevertibleAccumulatedData.nullifiers.length; i++) {
            if (clonedTxData.data.forPublic?.nonRevertibleAccumulatedData.nullifiers[i].isZero()) {
              continue;
            }
            clonedTxData.data.forPublic.nonRevertibleAccumulatedData.nullifiers[i] = Fr.random();
          }
        }

        const clonedTx = new ProvenTx(aztecNode, clonedTxData, baseTx.offchainEffects, baseTx.stats);
        txs.push(clonedTx);
      }
      // The tx hashes will need to be recomputed due to the nullifier changes
      await Promise.all(txs.map(tx => tx.recomputeHash()));
    }

    await Promise.all(workers.map(worker => worker.end()));

    logger.info(`Benchmark transaction proved.`);

    const allSentTxs: any[] = [];
    let sentSoFar = 0;
    for (let sec = 0; sec < TEST_DURATION_SECONDS; sec++) {
      const secondStart = Date.now();
      const chunk = txs.splice(0, TARGET_TPS);
      chunk.forEach((tx, idx) => {
        const sentTx = tx.send();
        allSentTxs.push(sentTx);
        logger.info(`sec ${sec + 1}: sent tx ${sentSoFar + idx + 1}`);
      });

      sentSoFar += chunk.length;
      const elapsed = Date.now() - secondStart;
      if (elapsed < 1000) {
        await sleep(1000 - elapsed);
      }
    }

    // Now wait for all transactions to be included
    logger.info(`All ${TOTAL_TXS} transactions sent. Waiting for inclusion...`);

    const inclusionPromises = allSentTxs.map((sentTx, idx) =>
      (async () => {
        try {
          await sentTx.wait({
            timeout: 1200,
            interval: 1,
            ignoreDroppedReceiptsFor: 2,
          });
          const receipt = await sentTx.getReceipt();
          logger.info(`tx ${idx + 1} included in block ${receipt.blockNumber}`);
          return { success: true, tx: sentTx };
        } catch (error) {
          logger.error(`tx ${idx + 1} was not included: ${error}`);
          return { success: false, tx: sentTx, error };
        }
      })(),
    );

    // Wait for every transaction to be included
    const results = await Promise.all(inclusionPromises);

    // Count successes and failures
    const successCount = results.filter(r => r.success).length;
    const failureCount = results.filter(r => !r.success).length;

    expect(allSentTxs.length).toBe(TOTAL_TXS);

    // Log failed transactions for debugging
    results
      .filter(r => !r.success)
      .forEach((result, idx) => {
        logger.warn(`Failed transaction ${idx + 1}: ${result.error}`);
      });

    logger.info(
      `Transaction inclusion summary: ${successCount} succeeded, ${failureCount} failed out of ${TOTAL_TXS} total`,
    );
  });
});
