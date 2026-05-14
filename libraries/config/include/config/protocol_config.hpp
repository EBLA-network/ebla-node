// libraries/config/include/config/protocol_config.hpp
#pragma once

#include <libdevcore/CommonJS.h>

#include "common/encoding_rlp.hpp"
#include "common/types.hpp"

namespace ebla {

// EBLA slashing / jailing configuration.
// Active from block 0. `jail_time` is the number of blocks a double-voter
// is barred from participating in consensus after a successful slashing.
struct SlashingConfig {
  uint64_t jail_time = 0;  // number of blocks a double-voter stays jailed

  HAS_RLP_FIELDS
};
Json::Value enc_json(const SlashingConfig& obj);
void dec_json(const Json::Value& json, SlashingConfig& obj);

// Permanent EBLA supply-cap parameters. Active from block 0.
// - max_supply: 12B EBLA hard cap, enforced on every block by processBlockReward.
// - generated_rewards: counter of rewards minted post-genesis, used by the
//   supply-cap invariant (max_supply >= genesis_balances_sum + generated_rewards).
struct SupplyConfig {
  ebla::uint256_t max_supply{"0x26C62AD77DC602DAE0000000"};  // 12 Billion EBLA — DO NOT CHANGE
  ebla::uint256_t generated_rewards{0};

  HAS_RLP_FIELDS
};
Json::Value enc_json(const SupplyConfig& obj);
void dec_json(const Json::Value& json, SupplyConfig& obj);

// Permanent EBLA protocol parameters set at genesis and unchanged for the
// lifetime of the chain. Holds the rewards-distribution cadence schedule,
// slashing rules, and supply-cap invariants.
//
// RLP wire format: field declaration order is load-bearing.
// The Go-side mirror struct (ebla-evm/ebla/state/chain_config/chain_config.go
// :: ProtocolConfig) MUST declare its fields in the same order.
// Do not reorder.
struct ProtocolConfig {
  /*
   * @brief key is block number at which change is applied and value is new distribution interval.
   * Default distribution frequency is every block.
   * To change rewards distribution frequency we should add a new element in map below.
   * For example {{101, 20}, {201, 10}} means:
   * 1. for blocks [1,100] we are distributing rewards every block
   * 2. for blocks [101, 200] rewards are distributed every 20 blocks (120, 140, etc.)
   * 3. for blocks after 201 rewards are distributed every 10 blocks (210, 220, etc.)
   */
  using RewardsDistributionMap = std::map<uint64_t, uint32_t>;
  RewardsDistributionMap rewards_distribution_frequency;

  // Slashing / validator deletion rules (active from block 0):
  // 1. Validators are deleted only after the last delegator confirms undelegation and
  //    total_stake == 0, rewards_pool == 0, undelegations_count == 0.
  // 2. Fee rewards go to the DAG block creator's commission pool, not directly to the
  //    PBFT block creator.
  // 3. Double-voting slashes: the validator is jailed for `jail_time` blocks and cannot
  //    participate in consensus.
  SlashingConfig slashing;

  // Permanent 12B EBLA supply cap and accumulated-rewards counter.
  // Read every block by processBlockReward() to enforce the issuance cap.
  SupplyConfig supply;

  HAS_RLP_FIELDS
};

Json::Value enc_json(const ProtocolConfig& obj);
void dec_json(const Json::Value& json, ProtocolConfig& obj);

}  // namespace ebla