#pragma once

#include <json/json.h>

#include "common/types.hpp"

namespace ebla {

struct PbftConfig {
  uint32_t lambda_ms = 0;
  uint32_t committee_size = 0;
  uint32_t number_of_proposers = 20;
  uint32_t dag_blocks_size = 0;
  uint32_t ghost_path_move_back = 0;
  uint64_t gas_limit = 0;

  // === EBLA ADDITION (Layer 2 - Stuck Round Recovery) ===
  // Maximum number of PBFT rounds to retry an invalid starting value before
  // forcing a NULL next-vote to advance the period. This struct field is the
  // C++ single source of truth (mirrors the `number_of_proposers = 20`
  // convention — in-class default, no genesis.cpp override). The four
  // genesis JSONs must keep "max_stuck_rounds": "0x32" in lockstep — see
  // BUG-31 (lambda_ms drift) for the precedent.
  // Must be >= 10 (enforced in dec_json) so legitimate consensus has a chance
  // to converge before recovery trips.
  // See EBLA-SEC-001.1 §3.2.
  uint32_t max_stuck_rounds = 50;
  // === END EBLA ADDITION ===

  bytes rlp() const;
};
Json::Value enc_json(PbftConfig const& obj);
void dec_json(Json::Value const& json, PbftConfig& obj);

}  // namespace ebla
