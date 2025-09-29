> [!WARNING]
> :warning: **<https://github.com/AztecProtocol/barretenberg> is a mirror-only repository, please see <https://github.com/AztecProtocol/aztec-packages> for active development.** :warning:

# Barretenberg

Barretenberg (or `bb` for short) is an optimized elliptic curve library for the BN254 curve and an advanced zero-knowledge proof system.

## What is Barretenberg?

- **Zero-Knowledge Proving System**: Implements Ultra Honk and other cutting-edge proving systems
- **High Performance**: Written in C++ with assembly optimizations for efficient proof generation
- **Multiple Interfaces**: Available as a CLI tool (`bb`), TypeScript/JavaScript library, and WASM module
- **Ethereum Compatible**: Generates Solidity verifiers for on-chain proof verification
- **Noir Backend**: Powers the Noir programming language's proving capabilities

## Caution

Audit preparation has begun, which will derisk usage, however currently:
> [!CAUTION]
> **This code is unaudited and contains novel ZK cryptography, be sure you're informed of the risks!**

In its current state, barretenberg is best used for non-financial or in-development software.

### Installation

Install the `bb` CLI tool using `bbup`:

```bash
curl -L https://raw.githubusercontent.com/AztecProtocol/aztec-packages/master/barretenberg/bbup/install | bash
bbup
```

For JavaScript/TypeScript projects:
```bash
npm install @aztec/bb.js
```

### Quick Start

The barretenberg proving system is used to generate and verify zero-knowledge proofs. Here's a basic example:

```bash
# Generate a proof (requires compiled Noir artifacts)
bb prove -b ./target/circuit.json -w ./target/witness.gz -o ./proof

# Verify the proof
bb verify -p ./proof -k ./target/vk
```

See the [CLI Reference](./docs/versioned_docs/version-v3.0.0-nightly.20250925/bb-cli-reference.md) for complete command documentation.

## Development

See [the developer README](./README.dev.md) for building from source and contributing.
