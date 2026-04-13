#pragma once

#include "network/tarcap/packets_handlers/latest/common/packet_handler.hpp"

namespace ebla::network::tarcap {

class IGetPillarVotesBundlePacketHandler : public PacketHandler {
 public:
  IGetPillarVotesBundlePacketHandler(const FullNodeConfig& conf, std::shared_ptr<PeersState> peers_state,
                                     std::shared_ptr<TimePeriodPacketsStats> packets_stats, const addr_t& node_addr,
                                     const std::string& logs_prefix);

  virtual void requestPillarVotesBundle(PbftPeriod period, const blk_hash_t& pillar_block_hash,
                                        const std::shared_ptr<EblaPeer>& peer) = 0;
};

}  // namespace ebla::network::tarcap
