// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Aztec Labs.
pragma solidity >=0.8.27;

import {Test} from "forge-std/Test.sol";
import {ChainTips, CompressedChainTips, ChainTipsLib} from "@aztec/core/libraries/compressed-data/Tips.sol";
import {SafeCast} from "@oz/utils/math/SafeCast.sol";

contract TipsWrapper {
  using ChainTipsLib for CompressedChainTips;

  function updatePendingCheckpointNumber(CompressedChainTips _compressedChainTips, uint256 _pendingCheckpointNumber)
    public
    pure
    returns (CompressedChainTips)
  {
    return _compressedChainTips.updatePendingCheckpointNumber(_pendingCheckpointNumber);
  }

  function updateProvenCheckpointNumber(CompressedChainTips _compressedChainTips, uint256 _provenCheckpointNumber)
    public
    pure
    returns (CompressedChainTips)
  {
    return _compressedChainTips.updateProvenCheckpointNumber(_provenCheckpointNumber);
  }
}

contract TipsTest is Test {
  using ChainTipsLib for CompressedChainTips;
  using ChainTipsLib for ChainTips;

  TipsWrapper public tipsWrapper = new TipsWrapper();

  function test_compress_uncompress(uint128 _pendingCheckpointNumber, uint128 _provenCheckpointNumber) public pure {
    ChainTips memory chainTips =
      ChainTips({pendingCheckpointNumber: _pendingCheckpointNumber, provenCheckpointNumber: _provenCheckpointNumber});

    CompressedChainTips compressedChainTips = chainTips.compress();
    ChainTips memory decompressedChainTips = compressedChainTips.decompress();

    assertEq(
      compressedChainTips.getPendingCheckpointNumber(), chainTips.pendingCheckpointNumber, "getPendingCheckpointNumber"
    );
    assertEq(
      compressedChainTips.getProvenCheckpointNumber(), chainTips.provenCheckpointNumber, "getProvenCheckpointNumber"
    );

    assertEq(
      decompressedChainTips.pendingCheckpointNumber,
      chainTips.pendingCheckpointNumber,
      "decompressed pendingCheckpointNumber"
    );
    assertEq(
      decompressedChainTips.provenCheckpointNumber,
      chainTips.provenCheckpointNumber,
      "decompressed provenCheckpointNumber"
    );
  }

  function test_updatePendingCheckpointNumber(uint128 _pendingCheckpointNumber, uint128 _provenCheckpointNumber)
    public
    pure
  {
    uint256 pendingCheckpointNumber = bound(_pendingCheckpointNumber, 0, type(uint128).max - 1);
    ChainTips memory a =
      ChainTips({pendingCheckpointNumber: pendingCheckpointNumber, provenCheckpointNumber: _provenCheckpointNumber});

    CompressedChainTips b = a.compress();
    CompressedChainTips c = b.updatePendingCheckpointNumber(pendingCheckpointNumber + 1);

    assertEq(c.getPendingCheckpointNumber(), pendingCheckpointNumber + 1, "c.getPendingCheckpointNumber");
    assertEq(c.getProvenCheckpointNumber(), _provenCheckpointNumber, "c.getProvenCheckpointNumber");
    assertEq(
      c.getPendingCheckpointNumber(),
      b.getPendingCheckpointNumber() + 1,
      "c.getPendingCheckpointNumber != b.getPendingCheckpointNumber + 1"
    );
  }

  function test_updatePendingCheckpointNumberOversized(
    uint256 _pendingCheckpointNumber,
    uint128 _provenCheckpointNumber
  ) public {
    ChainTips memory a = ChainTips({pendingCheckpointNumber: 0, provenCheckpointNumber: _provenCheckpointNumber});
    uint256 pendingCheckpointNumber = bound(_pendingCheckpointNumber, uint256(type(uint128).max) + 1, type(uint256).max);

    CompressedChainTips b = a.compress();
    vm.expectRevert(
      abi.encodeWithSelector(SafeCast.SafeCastOverflowedUintDowncast.selector, 128, pendingCheckpointNumber)
    );
    tipsWrapper.updatePendingCheckpointNumber(b, pendingCheckpointNumber);
  }

  function test_updateProvenCheckpointNumber(uint128 _pendingCheckpointNumber, uint128 _provenCheckpointNumber)
    public
    pure
  {
    uint256 provenCheckpointNumber = bound(_provenCheckpointNumber, 0, type(uint128).max - 1);
    ChainTips memory a =
      ChainTips({pendingCheckpointNumber: _pendingCheckpointNumber, provenCheckpointNumber: provenCheckpointNumber});

    CompressedChainTips b = a.compress();
    CompressedChainTips c = b.updateProvenCheckpointNumber(provenCheckpointNumber + 1);

    assertEq(c.getPendingCheckpointNumber(), _pendingCheckpointNumber, "c.getPendingCheckpointNumber");
    assertEq(c.getProvenCheckpointNumber(), provenCheckpointNumber + 1, "c.getProvenCheckpointNumber");
    assertEq(
      c.getProvenCheckpointNumber(),
      b.getProvenCheckpointNumber() + 1,
      "c.getProvenCheckpointNumber != b.getProvenCheckpointNumber + 1"
    );
  }

  function test_updateProvenCheckpointNumberOversized(
    uint128 _pendingCheckpointNumber,
    uint256 _provenCheckpointNumber
  ) public {
    ChainTips memory a = ChainTips({pendingCheckpointNumber: _pendingCheckpointNumber, provenCheckpointNumber: 0});
    uint256 provenCheckpointNumber = bound(_provenCheckpointNumber, uint256(type(uint128).max) + 1, type(uint256).max);

    CompressedChainTips b = a.compress();
    vm.expectRevert(
      abi.encodeWithSelector(SafeCast.SafeCastOverflowedUintDowncast.selector, 128, provenCheckpointNumber)
    );
    tipsWrapper.updateProvenCheckpointNumber(b, provenCheckpointNumber);
  }
}
