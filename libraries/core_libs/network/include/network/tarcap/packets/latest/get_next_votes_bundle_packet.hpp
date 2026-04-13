#pragma once

#include "common/encoding_rlp.hpp"
#include "common/types.hpp"

namespace ebla::network::tarcap {

struct GetNextVotesBundlePacket {
  PbftPeriod peer_pbft_period;
  PbftRound peer_pbft_round;

  RLP_FIELDS_DEFINE_INPLACE(peer_pbft_period, peer_pbft_round)
};

}  // namespace ebla::network::tarcap
