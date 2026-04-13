#pragma once

#include "packet_handler.hpp"
#include "pillar_chain/pillar_chain_manager.hpp"

namespace ebla::network::tarcap {

class ExtPillarVotePacketHandler : public PacketHandler {
 public:
  ExtPillarVotePacketHandler(const FullNodeConfig& conf, std::shared_ptr<PeersState> peers_state,
                             std::shared_ptr<TimePeriodPacketsStats> packets_stats,
                             std::shared_ptr<pillar_chain::PillarChainManager> pillar_chain_manager,
                             const addr_t& node_addr, const std::string& log_channel);

 protected:
  bool processPillarVote(const std::shared_ptr<PillarVote>& vote, const std::shared_ptr<EblaPeer>& peer);

 protected:
  std::shared_ptr<pillar_chain::PillarChainManager> pillar_chain_manager_;
};

}  // namespace ebla::network::tarcap
