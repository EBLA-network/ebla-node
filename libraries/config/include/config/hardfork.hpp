#pragma once

#include <libdevcore/CommonJS.h>

#include "common/encoding_rlp.hpp"
#include "common/types.hpp"

namespace ebla {
struct Redelegation {
  ebla::addr_t validator;
  ebla::addr_t delegator;
  ebla::uint256_t amount;
  HAS_RLP_FIELDS
};
Json::Value enc_json(const Redelegation& obj);
void dec_json(const Json::Value& json, Redelegation& obj);

// EBLA slashing / jailing configuration.
// Originally introduced by Taraxa's Magnolia hardfork; features are permanent in EBLA
// from block 0, so the hardfork gate has been removed. Only the runtime jail_time
// parameter remains.
struct SlashingConfig {
  uint64_t jail_time = 0;  // number of blocks a double-voter stays jailed

  HAS_RLP_FIELDS
};
Json::Value enc_json(const SlashingConfig& obj);
void dec_json(const Json::Value& json, SlashingConfig& obj);

// Permanent supply-cap / yield-curve parameters in EBLA.
// The original Taraxa Aspen hardfork block-number gates were removed in
// Phase 14.3 — both parts (minted-tokens DB and dynamic yield curve) are
// unconditional from block 0. Only the supply invariants remain.
struct AspenHardfork {
  ebla::uint256_t max_supply{"0x26C62AD77DC602DAE0000000"};  // 12 Billion
  ebla::uint256_t generated_rewards{0};

  HAS_RLP_FIELDS
};
Json::Value enc_json(const AspenHardfork& obj);
void dec_json(const Json::Value& json, AspenHardfork& obj);

// Keeping it for next HF
// struct BambooRedelegation {
//   ebla::addr_t validator;
//   ebla::uint256_t amount;
//   HAS_RLP_FIELDS
// };
// Json::Value enc_json(const BambooRedelegation& obj);
// void dec_json(const Json::Value& json, BambooRedelegation& obj);

// struct BambooHardfork {
//   uint64_t block_num{0};
//   std::vector<BambooRedelegation> redelegations;

//   HAS_RLP_FIELDS
// };
// Json::Value enc_json(const BambooHardfork& obj);
// void dec_json(const Json::Value& json, BambooHardfork& obj);

struct HardforksConfig {
  // disable it by default (set to max uint64)
  uint64_t fix_redelegate_block_num = -1;
  std::vector<Redelegation> redelegations;
  /*
   * @brief key is block number at which change is applied and value is new distribution interval.
   * Default distribution frequency is every block
   * To change rewards distribution frequency we should add a new element in map below.
   * For example {{101, 20}, {201, 10}} means:
   * 1. for blocks [1,100] we are distributing rewards every block
   * 2. for blocks [101, 200] rewards are distributed every 20 block. On blocks 120, 140, etc.
   * 3. for blocks after 201 rewards are distributed every 10 block. On blocks 210, 220, etc.
   */
  using RewardsDistributionMap = std::map<uint64_t, uint32_t>;
  RewardsDistributionMap rewards_distribution_frequency;

  // Slashing / validator deletion rules (permanent in EBLA from block 0):
  // 1. Validators are deleted only after the last delegator confirms undelegation and
  //    total_stake == 0, rewards_pool == 0, undelegations_count == 0.
  // 2. Fee rewards go to the DAG block creator's commission pool, not directly to the
  //    PBFT block creator.
  // 3. Double-voting slashes: the validator is jailed for `jail_time` blocks and cannot
  //    participate in consensus.
  SlashingConfig slashing;

  // Aspen hardfork implements new yield curve
  AspenHardfork aspen_hf;

  HAS_RLP_FIELDS
};

Json::Value enc_json(const HardforksConfig& obj);
void dec_json(const Json::Value& json, HardforksConfig& obj);
}  // namespace ebla
