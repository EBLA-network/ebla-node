#pragma once

#include "network/tarcap/packets_handlers/latest/common/exceptions.hpp"
#include "vote/pbft_vote.hpp"
#include "vote/votes_bundle_rlp.hpp"

namespace ebla::network::tarcap {

struct VotesBundlePacket {
  OptimizedPbftVotesBundle votes_bundle;

  RLP_FIELDS_DEFINE_INPLACE(votes_bundle)
};

}  // namespace ebla::network::tarcap
