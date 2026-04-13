#pragma once

#include "common/packet_handler.hpp"
#include "network/tarcap/packets/latest/get_pbft_sync_packet.hpp"

namespace ebla {
class PbftChain;
class PbftManager;
class DbStorage;
class VoteManager;
}  // namespace ebla

namespace ebla::network::tarcap {

class PbftSyncingState;

class GetPbftSyncPacketHandler : public PacketHandler {
 public:
  GetPbftSyncPacketHandler(const FullNodeConfig& conf, std::shared_ptr<PeersState> peers_state,
                           std::shared_ptr<TimePeriodPacketsStats> packets_stats,
                           std::shared_ptr<PbftSyncingState> pbft_syncing_state, std::shared_ptr<PbftManager> pbft_mgr,
                           std::shared_ptr<PbftChain> pbft_chain, std::shared_ptr<VoteManager> vote_mgr,
                           std::shared_ptr<DbStorage> db, const addr_t& node_addr, const std::string& logs_prefix = "");

  // Packet type that is processed by this handler
  static constexpr SubprotocolPacketType kPacketType_ = SubprotocolPacketType::kGetPbftSyncPacket;

 private:
  virtual void process(const threadpool::PacketData& packet_data, const std::shared_ptr<EblaPeer>& peer) override;

 protected:
  virtual void sendPbftBlocks(const std::shared_ptr<EblaPeer>& peer, PbftPeriod from_period,
                              size_t blocks_to_transfer, bool pbft_chain_synced);

  std::shared_ptr<PbftSyncingState> pbft_syncing_state_;
  std::shared_ptr<PbftManager> pbft_mgr_;
  std::shared_ptr<PbftChain> pbft_chain_;
  std::shared_ptr<VoteManager> vote_mgr_;
  std::shared_ptr<DbStorage> db_;
};

}  // namespace ebla::network::tarcap
