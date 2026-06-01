#pragma once

#include "network/eblacap/packets_handlers/latest/get_pbft_sync_packet_handler.hpp"

namespace ebla::network::eblacap::v4 {

class PbftSyncingState;

class GetPbftSyncPacketHandler : public eblacap::GetPbftSyncPacketHandler {
 public:
  using eblacap::GetPbftSyncPacketHandler::GetPbftSyncPacketHandler;

 private:
  virtual void process(const threadpool::PacketData& packet_data, const std::shared_ptr<EblaPeer>& peer) override;
};

}  // namespace ebla::network::eblacap::v4
