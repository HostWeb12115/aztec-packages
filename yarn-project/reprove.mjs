import { BBNativeRollupProver } from '@aztec/bb-prover';
import { PublicBaseRollupInputs } from '@aztec/stdlib/rollup';

import { readFile } from 'node:fs/promises';

const inputs = PublicBaseRollupInputs.fromBuffer(await readFile('./inputs'));
const prover = await BBNativeRollupProver.new({
  bbBinaryPath: process.env.BB_BINARY_PATH,
  bbWorkingDirectory: '/tmp/bb',
  acvmBinaryPath: process.env.ACVM_BINARY_PATH,
  acvmWorkingDirectory: '/tmp/acvm',
  bbSkipCleanup: true,
});
const res = await prover.getPublicBaseRollupProof(inputs);
console.log(res);
