import { getInitialTestAccountsData } from '@aztec/accounts/testing';
import { type AztecNode, EthAddress, Fr, type NodeInfo, createAztecNodeClient, retryUntil } from '@aztec/aztec.js';
import {
  DefaultL1ContractsConfig,
  GovernanceProposerContract,
  type L1ContractAddresses,
  RegistryContract,
  RollupContract,
  createEthereumChain,
  createExtendedL1Client,
  createL1TxUtilsFromViemWallet,
  defaultL1TxUtilsConfig,
  deployL1Contract,
  deployRollupForUpgrade,
} from '@aztec/ethereum';
import { createLogger } from '@aztec/foundation/log';
import { GovernanceAbi } from '@aztec/l1-artifacts/GovernanceAbi';
import { RegisterNewRollupVersionPayloadAbi } from '@aztec/l1-artifacts/RegisterNewRollupVersionPayloadAbi';
import { RegisterNewRollupVersionPayloadBytecode } from '@aztec/l1-artifacts/RegisterNewRollupVersionPayloadBytecode';
import { TestERC20Abi } from '@aztec/l1-artifacts/TestERC20Abi';
import { getVKTreeRoot } from '@aztec/noir-protocol-circuits-types/vk-tree';
import { protocolContractsHash } from '@aztec/protocol-contracts';
import { defaultInitialAccountFeeJuice } from '@aztec/world-state/testing';

import { jest } from '@jest/globals';
import type { ChildProcess } from 'child_process';
import omit from 'lodash.omit';
import { encodeFunctionData, getAddress, getContract, parseEventLogs } from 'viem';

import { MNEMONIC } from '../fixtures/fixtures.js';
import {
  getSequencersConfig,
  rollAztecPods,
  setupEnvironment,
  startPortForwardForEthereum,
  startPortForwardForRPC,
  updateSequencersConfig,
} from './utils.js';

const config = setupEnvironment(process.env);

const debugLogger = createLogger('e2e:spartan-test:upgrade_rollup_version');

// This test works through the entire governance process, from proposal creation to execution
describe('spartan_upgrade_rollup_version', () => {
  let aztecNode: AztecNode;
  let nodeInfo: NodeInfo;
  let ETHEREUM_HOSTS: string[];
  let originalL1ContractAddresses: L1ContractAddresses;
  const forwardProcesses: ChildProcess[] = [];
  jest.setTimeout(3 * 60 * 60 * 1000); // Governance flow can take a while

  afterAll(() => {
    forwardProcesses.forEach(p => p.kill());
  });

  beforeAll(async () => {
    const { process: aztecRpcProcess, port: aztecRpcPort } = await startPortForwardForRPC(config.NAMESPACE);
    const { process: ethereumProcess, port: ethereumPort } = await startPortForwardForEthereum(config.NAMESPACE);
    forwardProcesses.push(aztecRpcProcess);
    forwardProcesses.push(ethereumProcess);

    const nodeUrl = `http://127.0.0.1:${aztecRpcPort}`;
    const ethereumUrl = `http://127.0.0.1:${ethereumPort}`;

    aztecNode = createAztecNodeClient(nodeUrl);
    nodeInfo = await aztecNode.getNodeInfo();
    ETHEREUM_HOSTS = [ethereumUrl];

    originalL1ContractAddresses = omit(nodeInfo.l1ContractAddresses, [
      'slashFactoryAddress',
      'stakingAssetHandlerAddress',
      'feeAssetHandlerAddress',
    ]);
  });

  it('should upgrade the rollup version', async () => {
    const chain = createEthereumChain(ETHEREUM_HOSTS, nodeInfo.l1ChainId);
    const l1Client = createExtendedL1Client(ETHEREUM_HOSTS, MNEMONIC, chain.chainInfo);
    debugLogger.info(`L1 Client address: ${l1Client.account.address}`);

    // Just grab the genesis archive root from the old rollup instead of recomputing
    const initialTestAccounts = await getInitialTestAccountsData();
    // const { genesisArchiveRoot, fundingNeeded } = await getGenesisValues(
    //   initialTestAccounts.map(({ address }) => address),
    // );

    const genesisArchiveRoot = Fr.fromHexString(
      await new RollupContract(
        l1Client,
        originalL1ContractAddresses.rollupAddress.toString(),
      ).getGenesisArchiveTreeRoot(),
    );

    const rollup = new RollupContract(l1Client, originalL1ContractAddresses.rollupAddress.toString());
    const { rollup: newRollup } = await deployRollupForUpgrade(
      l1Client,
      {
        salt: Math.floor(Math.random() * 1000000),
        vkTreeRoot: getVKTreeRoot(),
        protocolContractsHash,
        genesisArchiveRoot,
        ethereumSlotDuration: 12,
        aztecSlotDuration: 24,
        aztecEpochDuration: 4,
        aztecProofSubmissionEpochs: 1,
        lagInEpochs: DefaultL1ContractsConfig.lagInEpochs,
        aztecTargetCommitteeSize: 48,
        slashingQuorum: 5,
        slashingRoundSizeInEpochs: 2,
        slashingLifetimeInRounds: 5,
        slashingExecutionDelayInRounds: 0,
        slashingVetoer: EthAddress.ZERO,
        slashingDisableDuration: DefaultL1ContractsConfig.slashingDisableDuration,
        localEjectionThreshold: DefaultL1ContractsConfig.localEjectionThreshold,
        manaTarget: BigInt(100e6),
        provingCostPerMana: BigInt(100),
        feeJuicePortalInitialBalance: BigInt(initialTestAccounts.length) * defaultInitialAccountFeeJuice.toBigInt(),
        realVerifier: false,
        exitDelaySeconds: DefaultL1ContractsConfig.exitDelaySeconds,
        slasherFlavor: DefaultL1ContractsConfig.slasherFlavor,
        slashingOffsetInRounds: DefaultL1ContractsConfig.slashingOffsetInRounds,
        slashAmountSmall: DefaultL1ContractsConfig.slashAmountSmall,
        slashAmountMedium: DefaultL1ContractsConfig.slashAmountMedium,
        slashAmountLarge: DefaultL1ContractsConfig.slashAmountLarge,
      },
      originalL1ContractAddresses.registryAddress,
      debugLogger,
      defaultL1TxUtilsConfig,
    );

    // Safeguard against deploying the same version twice (since it will fail)
    // We can remove this once https://linear.app/aztec-labs/issue/TMNT-139/version-at-deployment is resolved
    // See yarn-project/ethereum/src/deploy_l1_contracts.ts L:666
    const currentCanonical = await RegistryContract.collectAddresses(
      l1Client,
      originalL1ContractAddresses.registryAddress,
      'canonical',
    );
    const currentVer = await new RollupContract(l1Client, currentCanonical.rollupAddress.toString()).getVersion();
    const targetVer = await newRollup.getVersion();
    if (currentVer === targetVer) {
      debugLogger.info(`Already at target version ${targetVer}; skipping execute.`);
      expect(true).toBe(true);
      return;
    }

    // Deploy governance payload to register the NEW rollup version in the registry
    const { address: payloadAddress } = await deployL1Contract(
      l1Client,
      RegisterNewRollupVersionPayloadAbi,
      RegisterNewRollupVersionPayloadBytecode,
      [originalL1ContractAddresses.registryAddress.toString(), newRollup.address],
    );
    debugLogger.info(`RegisterNewRollupVersionPayload deployed at ${payloadAddress.toString()}`);

    // Point sequencers at the payload so they vote for it
    await updateSequencersConfig(config, { governanceProposerPayload: payloadAddress });
    try {
      const configs = await getSequencersConfig(config);
      debugLogger.info(`Sequencer configs applied; count=${configs.length}`);
    } catch (e) {
      debugLogger.warn(`Unable to fetch sequencer configs: ${e}`);
    }

    // Wait for quorum and the round transition, then submit round winner via Governance Proposer
    const governanceProposer = new GovernanceProposerContract(
      l1Client,
      nodeInfo.l1ContractAddresses.governanceProposerAddress.toString(),
    );

    const govInfo = async () => {
      const slot = await rollup.getSlotNumber();
      const round = await governanceProposer.computeRound(slot);
      const info = await governanceProposer.getRoundInfo(nodeInfo.l1ContractAddresses.rollupAddress.toString(), round);
      const leaderVotes = await governanceProposer.getPayloadSignals(
        nodeInfo.l1ContractAddresses.rollupAddress.toString(),
        round,
        info.payloadWithMostSignals,
      );
      return { slot, round, info, leaderVotes } as const;
    };

    const quorumSize = await governanceProposer.getQuorumSize();
    debugLogger.info(`Governance proposer quorum size: ${quorumSize}`);

    // Wait until payload has quorum
    while (true) {
      const { round, leaderVotes } = await govInfo();
      debugLogger.info(`Votes for leader payload: ${leaderVotes}/${quorumSize} (round ${round})`);
      if (leaderVotes >= quorumSize) {
        break;
      }
      await new Promise(r => setTimeout(r, 12_000));
    }

    // Wait for next round so the proposal becomes executable by proposer
    let { round } = await govInfo();
    const executableRound = round;
    while (round === executableRound) {
      await new Promise(r => setTimeout(r, 12_500));
      ({ round } = await govInfo());
    }

    const l1TxUtils = createL1TxUtilsFromViemWallet(l1Client, { logger: debugLogger });
    const { receipt: proposerReceipt, proposalId } = await governanceProposer.submitRoundWinner(
      executableRound,
      l1TxUtils,
    );
    debugLogger.info(`submitRoundWinner receipt status: ${proposerReceipt.status}`);
    const fromBlock = proposerReceipt.blockNumber;

    const governance = getContract({
      address: getAddress(nodeInfo.l1ContractAddresses.governanceAddress.toString()),
      abi: GovernanceAbi,
      client: l1Client,
    });

    // Helper to tally votes for this proposalId since submission
    const tallyVotes = async () => {
      try {
        const logs = await l1Client.getLogs({ address: governance.address, fromBlock, toBlock: 'latest' });
        const parsed = parseEventLogs({ abi: GovernanceAbi, logs });
        const votes = parsed.filter(
          (
            l,
          ): l is (typeof parsed)[number] & {
            eventName: 'VoteCast';
            args: { proposalId: bigint; voter: `0x${string}`; support: boolean; amount: bigint };
          } => l.eventName === 'VoteCast' && typeof l.args === 'object' && 'proposalId' in l.args,
        );
        const votesForThis = votes.filter(v => v.args.proposalId === proposalId);
        const yea = votesForThis.filter(v => v.args.support === true).reduce((acc, v) => acc + v.args.amount, 0n);
        const nay = votesForThis.filter(v => v.args.support === false).reduce((acc, v) => acc + v.args.amount, 0n);
        debugLogger.info(`VoteCast so far for proposal ${proposalId}: yea=${yea} nay=${nay} (events=${votes.length})`);
      } catch (e) {
        debugLogger.warn(`tallyVotes failed: ${String(e)}`);
      }
    };

    const proposal = await governance.read.getProposal([proposalId]);
    const creation: bigint = proposal.creation;
    const cfg = proposal.config;
    const votingDelay: bigint = cfg.votingDelay;
    const votingDuration: bigint = cfg.votingDuration;
    const executionDelay: bigint = cfg.executionDelay;
    const governanceConfig = await governance.read.getConfiguration();
    debugLogger.info(
      `Governance config: minimumVotes=${governanceConfig.minimumVotes} votingDelay=${votingDelay} votingDuration=${votingDuration} executionDelay=${executionDelay}`,
    );

    const timeToActive = creation + votingDelay;
    const timeToQueued = timeToActive + votingDuration;

    const nowTs = async () => BigInt((await l1Client.getBlock()).timestamp);

    // Wait for Active
    while ((await nowTs()) < timeToActive) {
      const remaining = Number(timeToActive - (await nowTs()));
      debugLogger.info(`Waiting for proposal Active phase. Seconds remaining: ${remaining}`);
      await new Promise(r => setTimeout(r, 5_000));
    }
    debugLogger.info('Proposal is Active (voting started)');

    // Mint + deposit enough staking tokens to exceed minimumVotes, then vote once
    try {
      const stakingToken = getContract({
        address: getAddress(nodeInfo.l1ContractAddresses.stakingAssetAddress.toString()),
        abi: TestERC20Abi,
        client: l1Client,
      });
      const minVotes = governanceConfig.minimumVotes as bigint;
      const buffer = minVotes / 10n; // add 10% buffer
      const depositAmount = minVotes + buffer;
      // Mint to self (TestERC20 has mint(address,uint256))
      const mintData = encodeFunctionData({
        abi: TestERC20Abi,
        functionName: 'mint',
        args: [l1Client.account.address, depositAmount],
      });
      await l1TxUtils.sendAndMonitorTransaction({ to: stakingToken.address, data: mintData });
      // Approve Governance
      const approveData = encodeFunctionData({
        abi: TestERC20Abi,
        functionName: 'approve',
        args: [governance.address, depositAmount],
      });
      await l1TxUtils.sendAndMonitorTransaction({ to: stakingToken.address, data: approveData });
      // Deposit into Governance
      const depositData = encodeFunctionData({
        abi: GovernanceAbi,
        functionName: 'deposit',
        args: [l1Client.account.address, depositAmount],
      });
      await l1TxUtils.sendAndMonitorTransaction({ to: governance.address, data: depositData });
      debugLogger.info(`Deposited staking tokens: ${depositAmount}`);
    } catch (e) {
      debugLogger.warn(`Mint/approve/deposit failed (continuing): ${String(e)}`);
    }

    // Ensure at least one new L1 block after Active to avoid NotInPast
    try {
      const before = await l1Client.getBlockNumber();
      const maxTries = 5;
      let tries = 0;
      while ((await l1Client.getBlockNumber()) === before && tries < maxTries) {
        try {
          await l1TxUtils.sendAndMonitorTransaction({ to: l1Client.account.address, value: 1n });
        } catch {
          await new Promise(r => setTimeout(r, 12_000));
        }
        tries++;
      }
    } catch {
      // ignore
    }

    try {
      // Log voter power and total power (prefer now views, fallback to past timestamp)
      const nowBlock = await l1Client.getBlock();
      let voterPower: bigint;
      let totalPower: bigint;
      try {
        voterPower = await governance.read.powerNow([l1Client.account.address]);
        totalPower = await governance.read.totalPowerNow();
      } catch {
        const pastTs = nowBlock.timestamp - 1n;
        voterPower = await governance.read.powerAt([l1Client.account.address, pastTs]);
        totalPower = await governance.read.totalPowerAt([pastTs]);
      }
      debugLogger.info(`Voter power: ${voterPower}; total power: ${totalPower}`);

      const voteRollup = new RollupContract(l1Client, nodeInfo.l1ContractAddresses.rollupAddress.toString());
      debugLogger.info(`Casting local vote for proposal ${proposalId}`);
      const voteResult = await voteRollup.vote(l1TxUtils, proposalId);
      debugLogger.info(`Local vote tx sent: ${voteResult.receipt?.transactionHash ?? 'unknown hash'}`);

      // Log ballot after voting
      try {
        const ballot = await governance.read.getBallot([proposalId, l1Client.account.address]);
        debugLogger.info(`Local ballot: yea=${ballot.yea} nay=${ballot.nay}`);
      } catch {
        // ignore ballot read failure
      }
    } catch (e) {
      debugLogger.warn(`Local vote attempt failed (non-fatal): ${String(e)}`);
    }

    // While in Active, poll until minimumVotes are met or voting window ends
    try {
      while ((await nowTs()) < timeToQueued) {
        const _p = await governance.read.getProposal([proposalId]);
        // Log cached state and timestamps as we progress
        debugLogger.info(`Proposal state during voting: ${_p.cachedState}`);
        await tallyVotes();
        const remaining = Number(timeToQueued - (await nowTs()));
        debugLogger.info(`Voting in progress. Seconds until voting ends: ${remaining}`);
        await new Promise(r => setTimeout(r, 15_000));
      }
    } catch {
      // ignore read failures while polling
    }

    // Wait precisely until Executable, then attempt execute once
    const startOfExecutable = creation + votingDelay + votingDuration + executionDelay;
    while ((await nowTs()) < startOfExecutable) {
      const remaining = Number(startOfExecutable - (await nowTs()));
      debugLogger.info(`Waiting to become Executable. Seconds remaining: ${remaining}`);
      await tallyVotes();
      await new Promise(r => setTimeout(r, 10_000));
    }

    // Confirm state is Executable (ProposalState.Executable == 3)
    try {
      const normalizeState = (s: unknown): bigint => {
        try {
          if (typeof s === 'bigint') {
            return s;
          }
          if (typeof s === 'number' || typeof s === 'string') {
            return BigInt(s);
          }
        } catch {
          /* ignore */
        }
        return 0n;
      };

      let stateRaw = await governance.read.getProposalState([proposalId]);
      let state = normalizeState(stateRaw);
      while (state !== 3n) {
        debugLogger.info(`Current proposal state: ${state}`);
        await new Promise(r => setTimeout(r, 5_000));
        stateRaw = await governance.read.getProposalState([proposalId]);
        state = normalizeState(stateRaw);
      }
      debugLogger.info('Proposal state is Executable');
    } catch {
      // ignore read failures and continue to execute attempt
    }

    // Use the proposal id returned by submitRoundWinner
    const maxGovernanceWaitMs = 20 * 60 * 1000; // 20 minutes safety window
    const startWait = Date.now();
    // Loop trying execute; ignore failures until timeout
    while (Date.now() - startWait < maxGovernanceWaitMs) {
      try {
        const data = encodeFunctionData({ abi: GovernanceAbi, functionName: 'execute', args: [proposalId] });
        const { receipt } = await l1TxUtils.sendAndMonitorTransaction({ to: governance.address, data });
        if (receipt.status === 'success') {
          debugLogger.info('Governance execute succeeded');
          break;
        }
      } catch (err) {
        debugLogger.warn(`Governance execute attempt failed: ${String(err)}`);
        // Not executable yet or reverted; wait and retry
      }
      await new Promise(r => setTimeout(r, 15_000));
    }

    const newAddresses = await newRollup.getRollupAddresses();

    const newCanonicalAddresses = await RegistryContract.collectAddresses(
      l1Client,
      originalL1ContractAddresses.registryAddress,
      'canonical',
    );

    const pick = <T, K extends readonly (keyof T)[]>(obj: T, keys: K) =>
      keys.reduce((acc, k) => ({ ...acc, [k]: obj[k] }), {} as Pick<T, K[number]>);

    const keys = [
      // preserved (non-versioned)
      'registryAddress',
      'governanceAddress',
      'governanceProposerAddress',
      'gseAddress',
      'rewardDistributorAddress',
      'feeJuiceAddress',
      'stakingAssetAddress',
      // updated (versioned)
      'rollupAddress',
      'inboxAddress',
      'outboxAddress',
      'feeJuicePortalAddress',
    ] as const;

    const expectedProjection = {
      // preserved
      registryAddress: originalL1ContractAddresses.registryAddress,
      governanceAddress: originalL1ContractAddresses.governanceAddress,
      governanceProposerAddress: originalL1ContractAddresses.governanceProposerAddress,
      gseAddress: originalL1ContractAddresses.gseAddress,
      rewardDistributorAddress: originalL1ContractAddresses.rewardDistributorAddress,
      feeJuiceAddress: originalL1ContractAddresses.feeJuiceAddress,
      stakingAssetAddress: originalL1ContractAddresses.stakingAssetAddress,
      // updated
      rollupAddress: newAddresses.rollupAddress,
      inboxAddress: newAddresses.inboxAddress,
      outboxAddress: newAddresses.outboxAddress,
      feeJuicePortalAddress: newAddresses.feeJuicePortalAddress,
    };

    expect(pick(newCanonicalAddresses, keys)).toEqual(expectedProjection);

    const oldVersion = await new RollupContract(
      l1Client,
      originalL1ContractAddresses.rollupAddress.toString(),
    ).getVersion();
    const newVersion = await new RollupContract(l1Client, newCanonicalAddresses.rollupAddress.toString()).getVersion();

    debugLogger.info(`oldVersion: ${oldVersion}, address: ${originalL1ContractAddresses.rollupAddress}`);
    debugLogger.info(`newVersion: ${newVersion}, address: ${newCanonicalAddresses.rollupAddress}`);
    expect(oldVersion).not.toEqual(newVersion);

    await expect(
      RegistryContract.collectAddresses(l1Client, originalL1ContractAddresses.registryAddress, oldVersion),
    ).resolves.toEqual(originalL1ContractAddresses);

    await expect(
      RegistryContract.collectAddresses(l1Client, originalL1ContractAddresses.registryAddress, newVersion),
    ).resolves.toEqual(newCanonicalAddresses);

    try {
      await rollAztecPods(config.NAMESPACE);
    } catch (err) {
      debugLogger.warn(`rollAztecPods failed (continuing): ${String(err)}`);
    }

    // Reconnect to the node via RPC after pods restart
    const { process: aztecRpcProcess2, port: aztecRpcPort2 } = await startPortForwardForRPC(config.NAMESPACE);
    forwardProcesses.push(aztecRpcProcess2);
    const nodeUrl2 = `http://127.0.0.1:${aztecRpcPort2}`;
    aztecNode = createAztecNodeClient(nodeUrl2);

    const newNodeInfo = await aztecNode.getNodeInfo();

    // Reapply proposer payload so sequencers re-signal after restart
    try {
      await updateSequencersConfig(config, { governanceProposerPayload: payloadAddress });
      const configs = await getSequencersConfig(config);
      debugLogger.info(`Sequencer configs re-applied: ${JSON.stringify(configs)}`);
    } catch (err) {
      debugLogger.warn(`Failed to reapply proposer payload (continuing): ${String(err)}`);
    }

    debugLogger.info(`newNodeInfo: ${JSON.stringify(newNodeInfo)}`);
    debugLogger.info(`originalL1ContractAddresses: ${JSON.stringify(originalL1ContractAddresses)}`);
    debugLogger.info(`newCanonicalAddresses: ${JSON.stringify(newCanonicalAddresses)}`);
    expect(newNodeInfo.l1ContractAddresses.rollupAddress).toEqual(newCanonicalAddresses.rollupAddress);

    const l2Tips = await newRollup.getTips();
    await expect(
      retryUntil(
        async () => {
          const tips = await newRollup.getTips();
          return tips.provenBlockNumber > l2Tips.provenBlockNumber;
        },
        'new rollup should be building/proving blocks',
        // +5 just as a buffer
        (config.AZTEC_PROOF_SUBMISSION_WINDOW + 5) * config.AZTEC_SLOT_DURATION,
        5,
      ),
    ).resolves.toBe(true);
  });
});
