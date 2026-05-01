#pragma once

#include <unordered_map>

#include "common/types.hpp"

namespace ebla {

class PbftVote;

enum class FiveOfEightVotedBlockType { SoftVotedBlock, CertVotedBlock, NextVotedBlock, NextVotedNullBlock };

struct VerifiedVotes {
  struct StepVotes {
    std::unordered_map<blk_hash_t, std::pair<uint64_t, std::unordered_map<vote_hash_t, std::shared_ptr<PbftVote>>>>
        votes;
    std::unordered_map<addr_t, std::pair<std::shared_ptr<PbftVote>, std::shared_ptr<PbftVote>>> unique_voters;
  };

  // 5/8 voted blocks
  std::unordered_map<FiveOfEightVotedBlockType, std::pair<blk_hash_t, PbftStep>> five_of_eight_voted_blocks_;

  // Step votes
  std::map<PbftStep, StepVotes> step_votes;

  // Greatest step, for which there is at least t+1 next votes - it is used for lambda exponential backoff: Usually
  // when network gets stalled it is due to lack of 5/8 voting power and steps keep increasing. When new node joins
  // the network, it should catch up with the rest of nodes asap so we dont start exponentially backing of its lambda
  // if it's current step is far behind network_half_five_of_eight_step (at least half of quorum is at this step)
  PbftStep network_half_five_of_eight_step{0};
};

}  // namespace ebla